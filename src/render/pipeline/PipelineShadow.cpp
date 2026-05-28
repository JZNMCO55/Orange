// Pipeline::Impl 的 shadow / light UBO 系列实现：EnsureShadowMap /
// ComputeLightViewProj / ComputeCascadeViewProjs / UpdateLightUbo /
// UpdatePointLightsUbo / RecordShadowPass。directional light 走 CSM
// （GAP-2026-05-27-cascaded-shadow-maps；C1：Tex2DArray + cascadeCount=1
// 默认零回归基线，多 cascade 真实拟合留 C2）；point light 上限 kMaxPointLights
// 走独立 UBO；spot light 走 spotShadowArray Tex2DArray 各 caster 占一层。

#include "PipelineImpl.h"

#include "orange/engine/core/Profiler.h"
#include "orange/engine/render/Camera.h"
#include "orange/engine/scene/TransformComponent.h"
#include "orange/engine/scene/World.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Orange::Engine::Render
{

using namespace PipelineDetail;

bool Pipeline::Impl::EnsureShadowMap()
{
    const std::uint32_t targetRes = shadowConfig.mapResolution > 0
                                        ? shadowConfig.mapResolution : 1024u;
    if (shadowMap && shadowMapResolution == targetRes)
    {
        return true;
    }
    if (renderDevice == nullptr)
    {
        return false;
    }
    renderDevice->WaitIdle();
    for (auto& v : shadowMapLayerViews) { v.reset(); }
    shadowMap.reset();

    auto& rhi = renderDevice->GetRhiDevice();

    // CSM Tex2DArray（kMaxCascades layer）：仿 EnsureSpotShadowArray 模式。
    // cascadeCount=1 默认时仅 layer 0 被渲染消费，其他 layer Clear 到 1.0
    // 不影响采样（pbr.frag cascade selection 退化恒返回 0）。
    Orange::Rhi::TextureDesc t{};
    t.mWidth       = targetRes;
    t.mHeight      = targetRes;
    t.mFormat      = Orange::Rhi::TextureFormat::D32Float;
    t.mDimension   = Orange::Rhi::TextureDimension::Tex2D;   // 多层 2D array
    t.mArrayLayers = kMaxCascades;
    t.mUsage       = Orange::Rhi::TextureUsage::DepthStencil
                   | Orange::Rhi::TextureUsage::Sampled;
    auto tex = rhi.CreateTexture(t);
    if (!tex)
    {
        ORANGE_LOG_ERROR("Pipeline: shadow map array CreateTexture 失败 ({}x{}x{} D32Float)",
                         targetRes, targetRes, kMaxCascades);
        return false;
    }
    shadowMap                      = std::move(tex);
    shadowMapResolution            = targetRes;
    shadowMapLayoutShaderReadOnly  = false;

    // per-layer Tex2D depth view（cascade i 作 depth attachment 单独渲）。
    for (std::uint32_t i = 0; i < kMaxCascades; ++i)
    {
        Orange::Rhi::TextureViewDesc vd{};
        vd.mViewDimension  = Orange::Rhi::TextureDimension::Tex2D;
        vd.mBaseMipLevel   = 0;
        vd.mLevelCount     = 1;
        vd.mBaseArrayLayer = i;
        vd.mLayerCount     = 1;
        auto view = rhi.CreateTextureView(*shadowMap, vd);
        if (!view)
        {
            ORANGE_LOG_ERROR("Pipeline: shadow map array CreateTextureView(layer={}) 失败", i);
            return false;
        }
        shadowMapLayerViews[i] = std::move(view);
    }

    // 把 main desc set 的 binding 0 重新指向新 shadow array 的默认 view
    //（Tex2DArray view）。binding 1 已在 EnsureMainDescriptorSetBinding 时
    // 绑过 lightUbo。
    if (mainDescSet && hdrSampler)
    {
        Orange::Rhi::DescriptorWrite write{};
        write.mBinding             = 0;
        write.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
        write.mImageInfo.mpTexture = shadowMap.get();
        write.mImageInfo.mpSampler = hdrSampler.get();  // 与 HDR sampler 共用一个 linear sampler
        rhi.UpdateDescriptorSet(*mainDescSet, &write, 1);
    }
    return true;
}

glm::mat4 Pipeline::Impl::ComputeLightViewProj(const glm::vec3& lightWorldDir) const
{
    // 0.x 简化：scene bbox 假定为 ±10 单位的立方体（足够覆盖 sample
    // 的 plane + cube + sphere）。后续接 RenderScene 提供的 bbox。
    const glm::vec3 lightDir = glm::normalize(lightWorldDir);
    const glm::vec3 sceneCenter(0.0f);
    constexpr float kHalfExtent = 10.0f;

    // 把"光源位置"放在 sceneCenter - lightDir * 2 * halfExtent，让 view
    // 看向 sceneCenter；ortho 视锥按 ±halfExtent 包住整个 scene。
    const glm::vec3 lightPos = sceneCenter - lightDir * (2.0f * kHalfExtent);
    glm::vec3       up       = glm::vec3(0.0f, 1.0f, 0.0f);
    if (std::abs(lightDir.y) > 0.99f)
    {
        // 光照接近垂直 → 切到 Z 轴避免奇异。
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    const glm::mat4 view = glm::lookAt(lightPos, sceneCenter, up);

    // 手写 Vulkan-style ortho（与 Camera::Orthographic 同公式）：z ∈ [0, 1]、
    // y-flip。glm::ortho 的 z 输出是 OpenGL [-1, 1]，会被 Vulkan 近平面 z=0
    // 裁掉一半 frustum，shadow caster 写不进 shadow map → plane 上看不到
    // 任何阴影。这里直接构造正确矩阵。
    constexpr float zNear = 0.1f;
    constexpr float zFar  = 4.0f * kHalfExtent;
    glm::mat4 proj(1.0f);
    proj[0][0] =  1.0f / kHalfExtent;
    proj[1][1] = -1.0f / kHalfExtent;       // y-flip 到 Vulkan NDC
    proj[2][2] =  1.0f / (zNear - zFar);    // z ∈ [0, 1]（near 远 → 0）
    proj[3][2] =  zNear / (zNear - zFar);
    return proj * view;
}

void Pipeline::Impl::ComputeCascadeViewProjs(const glm::vec3& lightWorldDir,
                                              const glm::mat4& cameraView,
                                              const glm::mat4& cameraProj)
{
    const std::uint32_t cascadeCount = std::min<std::uint32_t>(
        std::max<std::uint32_t>(shadowConfig.cascadeCount, 1u), kMaxCascades);

    // ─── C1 退化路径：cascadeCount=1 = 历史 ±10 ortho box 行为（零回归调试用）
    if (cascadeCount == 1)
    {
        const glm::mat4 singleVp = ComputeLightViewProj(lightWorldDir);
        for (auto& m : cascadeViewProjs) { m = singleVp; }
        cascadeNdcSplits  = glm::vec4(1.0f);
        cascadePcssScales = glm::vec4(1.0f);
        return;
    }

    // ─── C2 真 CSM 路径（GAP-2026-05-27-cascaded-shadow-maps）─────────────
    const glm::vec3 lightDir = glm::normalize(lightWorldDir);
    glm::vec3       up       = glm::vec3(0.0f, 1.0f, 0.0f);
    if (std::abs(lightDir.y) > 0.99f)
    {
        up = glm::vec3(0.0f, 0.0f, 1.0f);   // 光接近垂直 → 切 Z 轴避奇异
    }

    // 1. 从相机 proj 反推 near/far（Vulkan z[0,1] perspective 约定）：
    //    Camera::Perspective 写 proj[2][2] = far/(near-far)，proj[3][2] = near*far/(near-far)
    //    → near = proj[3][2] / proj[2][2]，far = proj[3][2] / (proj[2][2] + 1)
    const float camNear = cameraProj[3][2] / cameraProj[2][2];
    const float camFar  = cameraProj[3][2] / (cameraProj[2][2] + 1.0f);

    // 2. Practical PSSM split（λ=0.5 mix log + uniform，Engel/Dimitrov GPU Pro 同款）
    constexpr float kLambda = 0.5f;
    float splitDist[kMaxCascades + 1]{};
    splitDist[0] = camNear;
    for (std::uint32_t i = 1; i < cascadeCount; ++i)
    {
        const float p         = static_cast<float>(i) / static_cast<float>(cascadeCount);
        const float logSplit  = camNear * std::pow(camFar / camNear, p);
        const float unifSplit = camNear + (camFar - camNear) * p;
        splitDist[i] = kLambda * logSplit + (1.0f - kLambda) * unifSplit;
    }
    splitDist[cascadeCount] = camFar;

    // 3. NDC 8 角点 → world，留作各 cascade slice 沿 ray lerp 的 base
    const glm::mat4 invViewProj = glm::inverse(cameraProj * cameraView);
    glm::vec3       worldCornersNear[4]{};
    glm::vec3       worldCornersFar[4]{};
    {
        int idx = 0;
        for (int y = -1; y <= 1; y += 2)
        {
            for (int x = -1; x <= 1; x += 2)
            {
                const glm::vec4 nNdc = invViewProj * glm::vec4(float(x), float(y), 0.0f, 1.0f);
                const glm::vec4 fNdc = invViewProj * glm::vec4(float(x), float(y), 1.0f, 1.0f);
                worldCornersNear[idx] = glm::vec3(nNdc) / nNdc.w;
                worldCornersFar[idx]  = glm::vec3(fNdc) / fNdc.w;
                ++idx;
            }
        }
    }

    // 4. 单一 lightView（camera 在 world 原点 + lookAt(+lightDir)）—— 所有 cascade
    //    共享此矩阵；texel snap 在此 lightView 坐标系下做，让相机平移只移球心、
    //    不改 cascade extent → shadow texel 恒定，消 shimmer。
    const glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), lightDir, up);

    const float res          = static_cast<float>(shadowMapResolution > 0 ? shadowMapResolution : 1024u);
    float       cascade0Diam = 1.0f;   // cascade 0 的 ortho extent（= 2 * sphereRadius），给 PCSS scale 用

    // 5. 每 cascade：bounding sphere 求 fit（旋转不变 → 相机仅旋转时 sphere 不变
    //    → snap-on-center 让 shadow edge 稳定，不抖）。
    for (std::uint32_t c = 0; c < cascadeCount; ++c)
    {
        const float nearI = splitDist[c];
        const float farI  = splitDist[c + 1];

        // slice 8 角点 = lerp(camera near/far, α 沿 ray) —— world ray 线性、深
        // 度也线性，故 α = (depthFromNear) / (totalDepth)
        const float alphaNear = (nearI - camNear) / (camFar - camNear);
        const float alphaFar  = (farI  - camNear) / (camFar - camNear);

        glm::vec3 sliceCorners[8];
        for (int i = 0; i < 4; ++i)
        {
            sliceCorners[i]     = glm::mix(worldCornersNear[i], worldCornersFar[i], alphaNear);
            sliceCorners[i + 4] = glm::mix(worldCornersNear[i], worldCornersFar[i], alphaFar);
        }

        // bounding sphere of slice corners
        glm::vec3 sphereCenter(0.0f);
        for (const auto& sc : sliceCorners) { sphereCenter += sc; }
        sphereCenter /= 8.0f;
        float sphereRadius = 0.0f;
        for (const auto& sc : sliceCorners)
        {
            sphereRadius = std::max(sphereRadius, glm::length(sc - sphereCenter));
        }
        // 半径向上量化到 1/16 单位 —— 让 sphereRadius 在相机微动时仅离散跳动，
        // 配合 snap-on-center 实现完整 anti-shimmer。
        sphereRadius = std::ceil(sphereRadius * 16.0f) / 16.0f;

        // 球心转 light view → snap xy 到 shadow texel 网格
        glm::vec3   centerLV  = glm::vec3(lightView * glm::vec4(sphereCenter, 1.0f));
        const float texelSize = 2.0f * sphereRadius / res;
        centerLV.x = std::floor(centerLV.x / texelSize) * texelSize;
        centerLV.y = std::floor(centerLV.y / texelSize) * texelSize;

        // ortho z range：centerLV.z 为负（slice 在 light view -z 方向）；正向
        // 距离（zNear/zFar）= 取反。frontPad 在球前面（更靠近 light camera）多
        // 留一段，捕捉 caster 在 slice 与 light 之间的部分；backPad 给点 self-
        // shadow safety。Pad 用 sphereRadius 倍数自适应不同 cascade 尺度。
        const float frontPad = sphereRadius * 5.0f;
        const float backPad  = sphereRadius * 0.5f;
        const float zNear    = -(centerLV.z + sphereRadius + frontPad);
        const float zFar     = -(centerLV.z - sphereRadius - backPad);

        // 手写 Vulkan-style ortho（z[0,1] + y-flip）。中心 (centerLV.xy)：
        //   x_ndc = (x_view - centerLV.x) / sphereRadius
        //   y_ndc = -(y_view - centerLV.y) / sphereRadius  （y-flip）
        glm::mat4 ortho(1.0f);
        ortho[0][0] =  1.0f / sphereRadius;
        ortho[1][1] = -1.0f / sphereRadius;
        ortho[2][2] =  1.0f / (zNear - zFar);
        ortho[3][0] = -centerLV.x / sphereRadius;
        ortho[3][1] =  centerLV.y / sphereRadius;
        ortho[3][2] =  zNear / (zNear - zFar);

        cascadeViewProjs[c] = ortho * lightView;

        const float diam = 2.0f * sphereRadius;
        if (c == 0) { cascade0Diam = diam; }
        cascadePcssScales[c] = (diam > 1e-6f) ? (cascade0Diam / diam) : 1.0f;
    }

    // 6. cascadeNdcSplits[i] = cascade i 远端在主相机投影下的 NDC z
    //    pbr.frag 按 gl_FragCoord.z 与之比较选 cascade。Vulkan z[0,1]：
    //    ndcZ = -proj[2][2] + proj[3][2] / splitDist  （viewZ = -splitDist 代入）
    //    验：splitDist=near → 0；splitDist=far → 1
    cascadeNdcSplits = glm::vec4(1.0f);   // default 1.0 防 cascade 选择越界
    for (std::uint32_t c = 0; c + 1 < cascadeCount; ++c)
    {
        const float viewDepth = splitDist[c + 1];
        cascadeNdcSplits[c] = -cameraProj[2][2] + cameraProj[3][2] / viewDepth;
    }

    // 7. cascadeCount 之外的 slot 用最后一个有效 cascade 填，让 pbr.frag 在
    //    cascade 选择越界（极罕见 / 浮点边界）时有合理 fallback
    for (std::uint32_t c = cascadeCount; c < kMaxCascades; ++c)
    {
        cascadeViewProjs[c]  = cascadeViewProjs[cascadeCount - 1];
        cascadePcssScales[c] = cascadePcssScales[cascadeCount - 1];
    }
}

void Pipeline::Impl::UpdateLightUbo(const DirectionalLight* light,
                                    const glm::vec3&        lightWorldDir,
                                    const glm::vec3&        cameraWorldPos,
                                    const glm::vec3&        iblTintIntensity)
{
    if (!lightUbo)
    {
        return;
    }
    LightUboData data{};
    // CSM additive：lightViewProj 字段写 cascade 0 作 backward-compat alias，
    // 老 shader（toon / rim_light / water_basic / 等）仍可按 `uLightViewProj`
    // 读到等效矩阵。pbr.frag 真正消费 cascadeViewProj[] + cascadeNdcSplits。
    data.lightViewProj = cascadeViewProjs[0];
    for (std::uint32_t i = 0; i < kMaxCascades; ++i)
    {
        data.cascadeViewProj[i] = cascadeViewProjs[i];
    }
    data.cascadeNdcSplits  = cascadeNdcSplits;
    data.cascadePcssScales = cascadePcssScales;
    // CSM cascade tint overlay flag（sample 18 polish；shipping 永远 0）
    data.debugFlags = glm::vec4(shadowConfig.debugCascadeTint ? 1.0f : 0.0f,
                                0.0f, 0.0f, 0.0f);

    if (light != nullptr)
    {
        data.lightDirIntensity = glm::vec4(lightWorldDir, light->intensity);
        data.lightColor        = glm::vec4(light->color, 0.0f);
    }
    else
    {
        // neutral light：方向斜下、白光、单位强度。toon / rim_light 在
        // 没真光的场景仍能给出"基线"光照（与 sample 03/04 视觉一致）。
        data.lightDirIntensity = glm::vec4(0.3f, -1.0f, 0.4f, 1.0f);
        data.lightColor        = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    }
    // shadowParams.w（GAP-2026-05-27 C3）：cascadeCount 喂给 pbr.frag，决定
    // cross-cascade blend 是否触发（仅 csmCascade < cascadeCount-1 时才与下一
    // cascade smoothstep blend；最后 cascade 的 filler slot 不参与，省 5% 多余采样）。
    const float cascadeCountForShader = static_cast<float>(std::min<std::uint32_t>(
        std::max<std::uint32_t>(shadowConfig.cascadeCount, 1u), kMaxCascades));
    data.shadowParams = glm::vec4(static_cast<float>(shadowConfig.pcfKernelRadius),
                                  shadowConfig.depthBias,
                                  shadowConfig.pcssLightSize,  // z = PCSS 半影尺度（0=关）
                                  cascadeCountForShader);      // w = CSM cascadeCount
    data.cameraWorldPos = glm::vec4(cameraWorldPos, 0.0f);
    data.frameInfo      = glm::vec4(frameTime, 0.0f, 0.0f, 0.0f);
    data.iblFactor      = glm::vec4(iblTintIntensity, 0.0f);

    void* mapped = lightUbo->Map();
    if (mapped == nullptr)
    {
        ORANGE_LOG_ERROR("Pipeline: lightUbo Map 失败");
        return;
    }
    std::memcpy(mapped, &data, sizeof(data));
    lightUbo->Unmap();
}

void Pipeline::Impl::UpdatePointLightsUbo(Orange::Engine::World& world)
{
    if (!pointLightsUbo) { return; }

    PointLightsUboData data{};
    std::uint32_t      count = 0;
    pointShadowCount = 0;  // 本帧 cube shadow caster 计数

    auto& reg = world.Registry();
    // entt 不要求 TransformComponent 同步存在；缺 Transform 视为 origin。
    auto view = reg.view<PointLight>();
    bool warnedOverflow = false;
    for (auto entity : view)
    {
        if (count >= kMaxPointLights)
        {
            if (!warnedOverflow)
            {
                ORANGE_LOG_WARN("Pipeline: scene has more than {} PointLights; "
                                "extras ignored.", kMaxPointLights);
                warnedOverflow = true;
            }
            continue;
        }
        const auto& pl = view.get<PointLight>(entity);
        glm::vec3 pos{0.0f};
        if (const auto* tc = reg.try_get<Orange::Engine::Scene::TransformComponent>(entity))
        {
            pos = tc->position;
        }

        // cube shadow index：castsShadow 的 point 先到先得分配 0..N-1，超出
        // kMaxPointShadowCasters 退化无阴影。存 lightPos + far(=range) 供
        // RecordPointShadowPass 构 6 面矩阵。
        float shadowIndex = -1.0f;
        if (pl.castsShadow && pointShadowCount < kMaxPointShadowCasters)
        {
            const std::uint32_t idx = pointShadowCount;
            pointShadowLightPos[idx] = pos;
            pointShadowFar[idx]      = pl.range > kPointShadowNear
                                           ? pl.range : (kPointShadowNear + 0.01f);
            shadowIndex = static_cast<float>(idx);
            ++pointShadowCount;
        }

        data.lights[count].posRange       = glm::vec4(pos, pl.range);
        data.lights[count].colorIntensity = glm::vec4(pl.color, pl.intensity);
        data.lights[count].shadowParams   = glm::vec4(shadowIndex, 0.0f, 0.0f, 0.0f);
        ++count;
    }
    data.countPad.x = count;

    void* mapped = pointLightsUbo->Map();
    if (mapped == nullptr)
    {
        ORANGE_LOG_ERROR("Pipeline: pointLightsUbo Map 失败");
        return;
    }
    std::memcpy(mapped, &data, sizeof(data));
    pointLightsUbo->Unmap();
}

void Pipeline::Impl::UpdateSpotLightsUbo(Orange::Engine::World& world)
{
    if (!spotLightsUbo) { return; }

    SpotLightsUboData data{};
    std::uint32_t     count = 0;
    spotShadowCount = 0;  // 本帧 shadow-casting spot 计数，下方分配 index

    auto& reg = world.Registry();
    auto  view = reg.view<SpotLight>();
    bool  warnedOverflow = false;
    for (auto entity : view)
    {
        if (count >= kMaxSpotLights)
        {
            if (!warnedOverflow)
            {
                ORANGE_LOG_WARN("Pipeline: scene has more than {} SpotLights; "
                                "extras ignored.", kMaxSpotLights);
                warnedOverflow = true;
            }
            continue;
        }
        const auto& sl = view.get<SpotLight>(entity);

        glm::vec3 pos{0.0f};
        glm::vec3 dir = kSpotLightLocalForward;  // 缺 Transform → 默认向下
        if (const auto* tc = reg.try_get<Orange::Engine::Scene::TransformComponent>(entity))
        {
            pos = tc->position;
            dir = ComputeSpotLightWorldDir(tc->rotation);
        }

        // 锥角软边：smoothstep(cosOuter, cosInner, dot)。GLSL smoothstep 要求
        // edge0 < edge1，故 host 端保证 cosInner > cosOuter（即 innerAngle <
        // outerAngle）+ 一个 epsilon 防 edge0==edge1 除零。
        const float outerCos = std::cos(sl.outerConeAngle);
        float       innerCos = std::cos(std::min(sl.innerConeAngle, sl.outerConeAngle));
        innerCos = std::max(innerCos, outerCos + 1e-4f);

        // shadow index：castsShadow 的 spot 按先到先得分配 0..N-1，超出
        // kMaxSpotShadowCasters 的退化为无阴影（index = -1）。同步算 light
        // view-proj 存进 spotShadowMatrices，供 RecordSpotShadowPass 渲 + 写
        // spotShadowUbo 供 pbr.frag 采样。
        float shadowIndex = -1.0f;
        if (sl.castsShadow && spotShadowCount < kMaxSpotShadowCasters)
        {
            const std::uint32_t idx = spotShadowCount;
            spotShadowMatrices[idx] =
                ComputeSpotLightViewProj(pos, dir, sl.outerConeAngle, sl.range);
            shadowIndex = static_cast<float>(idx);
            ++spotShadowCount;
        }

        data.lights[count].posRange       = glm::vec4(pos, sl.range);
        data.lights[count].dirCosOuter    = glm::vec4(dir, outerCos);
        data.lights[count].colorIntensity = glm::vec4(sl.color, sl.intensity);
        data.lights[count].cosInnerShadow = glm::vec4(innerCos, shadowIndex, 0.0f, 0.0f);
        ++count;
    }
    data.countPad.x = count;

    void* mapped = spotLightsUbo->Map();
    if (mapped == nullptr)
    {
        ORANGE_LOG_ERROR("Pipeline: spotLightsUbo Map 失败");
        return;
    }
    std::memcpy(mapped, &data, sizeof(data));
    spotLightsUbo->Unmap();

    // spot shadow matrices UBO（pbr.frag 按 shadow index 采样时用）。
    if (spotShadowUbo)
    {
        SpotShadowUboData sd{};
        sd.countPad.x = spotShadowCount;
        for (std::uint32_t i = 0; i < spotShadowCount; ++i)
        {
            sd.lightViewProj[i] = spotShadowMatrices[i];
        }
        void* sm = spotShadowUbo->Map();
        if (sm != nullptr)
        {
            std::memcpy(sm, &sd, sizeof(sd));
            spotShadowUbo->Unmap();
        }
        else
        {
            ORANGE_LOG_ERROR("Pipeline: spotShadowUbo Map 失败");
        }
    }
}

glm::mat4 Pipeline::Impl::ComputeSpotLightViewProj(const glm::vec3& pos,
                                                   const glm::vec3& dir,
                                                   float            outerConeAngle,
                                                   float            range) const
{
    const glm::vec3 d  = glm::normalize(dir);
    glm::vec3       up = glm::vec3(0.0f, 1.0f, 0.0f);
    if (std::abs(d.y) > 0.99f)
    {
        up = glm::vec3(0.0f, 0.0f, 1.0f);  // 锥光接近垂直 → 切 Z 轴避奇异
    }
    const glm::mat4 view = glm::lookAt(pos, pos + d, up);

    // fov = 2 × 外锥半角；钳 < π 防 tan 爆。aspect=1（方形 shadow map）。
    const float fovY  = std::min(2.0f * outerConeAngle, 3.0f);
    const float zNear = 0.05f;
    const float zFar  = std::max(range, zNear + 0.01f);
    const glm::mat4 proj = Camera::Perspective(fovY, 1.0f, zNear, zFar).projection;
    return proj * view;
}

bool Pipeline::Impl::EnsureSpotShadowArray()
{
    const std::uint32_t targetRes = shadowConfig.mapResolution > 0
                                        ? shadowConfig.mapResolution : 1024u;
    if (spotShadowArray && spotShadowArrayResolution == targetRes)
    {
        return true;
    }
    if (renderDevice == nullptr)
    {
        return false;
    }
    renderDevice->WaitIdle();
    for (auto& v : spotShadowLayerViews) { v.reset(); }
    spotShadowArray.reset();

    auto& rhi = renderDevice->GetRhiDevice();

    Orange::Rhi::TextureDesc t{};
    t.mWidth       = targetRes;
    t.mHeight      = targetRes;
    t.mFormat      = Orange::Rhi::TextureFormat::D32Float;
    t.mDimension   = Orange::Rhi::TextureDimension::Tex2D;  // 多层 2D array
    t.mArrayLayers = kMaxSpotShadowCasters;
    t.mUsage       = Orange::Rhi::TextureUsage::DepthStencil
                   | Orange::Rhi::TextureUsage::Sampled;
    auto tex = rhi.CreateTexture(t);
    if (!tex)
    {
        ORANGE_LOG_ERROR("Pipeline: spot shadow array CreateTexture 失败 ({}x{}x{} D32Float)",
                         targetRes, targetRes, kMaxSpotShadowCasters);
        return false;
    }
    spotShadowArray = std::move(tex);

    // per-layer Tex2D depth view —— 各层作 depth attachment 单独渲。
    for (std::uint32_t i = 0; i < kMaxSpotShadowCasters; ++i)
    {
        Orange::Rhi::TextureViewDesc vd{};
        vd.mViewDimension  = Orange::Rhi::TextureDimension::Tex2D;
        vd.mBaseMipLevel   = 0;
        vd.mLevelCount     = 1;
        vd.mBaseArrayLayer = i;
        vd.mLayerCount     = 1;
        auto view = rhi.CreateTextureView(*spotShadowArray, vd);
        if (!view)
        {
            ORANGE_LOG_ERROR("Pipeline: spot shadow array CreateTextureView(layer={}) 失败", i);
            return false;
        }
        spotShadowLayerViews[i] = std::move(view);
    }

    spotShadowArrayResolution           = targetRes;
    spotShadowArrayLayoutShaderReadOnly = false;

    // binding 7 = sampler2DArray uSpotShadowMaps；用整张 array 默认 view。
    if (mainDescSet && hdrSampler)
    {
        Orange::Rhi::DescriptorWrite w{};
        w.mBinding             = 7;
        w.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
        w.mImageInfo.mpTexture = spotShadowArray.get();
        w.mImageInfo.mpSampler = hdrSampler.get();
        rhi.UpdateDescriptorSet(*mainDescSet, &w, 1);
    }
    return true;
}

bool Pipeline::Impl::RecordSpotShadowPass()
{
    ORANGE_PROFILE_SCOPE("SpotShadow");
    if (!spotShadowArray || offscreenCmd == nullptr || !shadowCasterPipeline)
    {
        return false;
    }
    auto& cmd = *offscreenCmd;

    // 整张 array 一次 transition（TransitionTexture 覆盖所有 array layer）。
    const auto fromLayout = spotShadowArrayLayoutShaderReadOnly
        ? Orange::Rhi::TextureLayout::ShaderReadOnly
        : Orange::Rhi::TextureLayout::Undefined;
    cmd.TransitionTexture(*spotShadowArray, fromLayout,
                          Orange::Rhi::TextureLayout::DepthStencilAttachment);

    // 逐层（= 逐 shadow-casting spot）渲 depth-only。spotShadowCount 之外的
    // 层仍 Clear 到 1.0（远深度 = 全亮，等价无阴影），避免残留上一帧。
    for (std::uint32_t layer = 0; layer < kMaxSpotShadowCasters; ++layer)
    {
        Orange::Rhi::DepthStencilAttachment depth{};
        depth.mpView        = spotShadowLayerViews[layer].get();
        depth.mDepthLoadOp  = Orange::Rhi::LoadOp::Clear;
        depth.mDepthStoreOp = Orange::Rhi::StoreOp::Store;
        depth.mClear.mDepth = 1.0f;

        Orange::Rhi::RenderingDesc rd{};
        rd.mRenderArea.mWidth  = spotShadowArrayResolution;
        rd.mRenderArea.mHeight = spotShadowArrayResolution;
        rd.mDepthStencil       = depth;
        cmd.BeginRendering(rd);

        Orange::Rhi::RHIViewport vp{};
        vp.mWidth    = static_cast<float>(spotShadowArrayResolution);
        vp.mHeight   = static_cast<float>(spotShadowArrayResolution);
        vp.mMinDepth = 0.0f;
        vp.mMaxDepth = 1.0f;
        cmd.SetViewport(vp);
        Orange::Rhi::RHIScissor sc{};
        sc.mWidth  = spotShadowArrayResolution;
        sc.mHeight = spotShadowArrayResolution;
        cmd.SetScissor(sc);

        if (layer < spotShadowCount)
        {
            cmd.BindGraphicsPipeline(*shadowCasterPipeline);
            const glm::mat4& lightVP = spotShadowMatrices[layer];

            for (const auto& drawable : scene.Drawables())
            {
                if (!drawable.castsShadow || !drawable.mesh.IsValid())
                {
                    continue;
                }
                auto cacheIt = meshCache.find(drawable.mesh.Value());
                if (cacheIt == meshCache.end())
                {
                    continue;
                }
                const auto& gpu = cacheIt->second;

                struct ShadowCasterPush { glm::mat4 lightVP; glm::mat4 model; };
                ShadowCasterPush pc{};
                pc.lightVP = lightVP;
                pc.model   = drawable.worldMatrix;
                cmd.SetPushConstants(Orange::Rhi::ShaderStage::Vertex,
                                     0, static_cast<std::uint32_t>(sizeof(pc)),
                                     &pc);

                cmd.BindVertexBuffer(0, *gpu.vertexBuffer, 0);
                cmd.BindIndexBuffer(*gpu.indexBuffer, 0, Orange::Rhi::IndexFormat::UInt32);
                cmd.DrawIndexed(gpu.indexCount, 1, 0, 0, 0);
            }
        }

        cmd.EndRendering();
    }

    cmd.TransitionTexture(*spotShadowArray,
                          Orange::Rhi::TextureLayout::DepthStencilAttachment,
                          Orange::Rhi::TextureLayout::ShaderReadOnly);
    spotShadowArrayLayoutShaderReadOnly = true;
    return true;
}

bool Pipeline::Impl::RecordShadowPass(const DirectionalLight* light)
{
    ORANGE_PROFILE_SCOPE("Shadow");
    if (!shadowMap || offscreenCmd == nullptr)
    {
        return false;
    }
    auto& cmd = *offscreenCmd;

    // 整张 array 一次 transition（TransitionTexture 覆盖所有 layer）。
    const auto fromLayout = shadowMapLayoutShaderReadOnly
        ? Orange::Rhi::TextureLayout::ShaderReadOnly
        : Orange::Rhi::TextureLayout::Undefined;
    cmd.TransitionTexture(*shadowMap, fromLayout,
                          Orange::Rhi::TextureLayout::DepthStencilAttachment);

    // 光源不投影 / 缺失时——清完深度 = 1.0 即"远深度"，shadow_pcf 取
    // currentDepth <= 1.0 → 总是 1（全亮），等价于"无阴影"。
    const bool runCaster = (light != nullptr && light->castsShadow && shadowCasterPipeline);
    const std::uint32_t cascadeCount = std::min<std::uint32_t>(
        std::max<std::uint32_t>(shadowConfig.cascadeCount, 1u), kMaxCascades);

    // 逐 cascade layer 渲 depth-only。cascadeCount 之外的 layer 仍 Clear 到
    // 1.0（远深度 = 全亮，等价无阴影），避免残留 + 满足 ShaderReadOnly 入参。
    for (std::uint32_t layer = 0; layer < kMaxCascades; ++layer)
    {
        Orange::Rhi::DepthStencilAttachment depth{};
        depth.mpView        = shadowMapLayerViews[layer].get();
        depth.mDepthLoadOp  = Orange::Rhi::LoadOp::Clear;
        depth.mDepthStoreOp = Orange::Rhi::StoreOp::Store;
        depth.mClear.mDepth = 1.0f;

        Orange::Rhi::RenderingDesc rd{};
        rd.mRenderArea.mWidth  = shadowMapResolution;
        rd.mRenderArea.mHeight = shadowMapResolution;
        rd.mDepthStencil       = depth;
        cmd.BeginRendering(rd);

        Orange::Rhi::RHIViewport vp{};
        vp.mWidth    = static_cast<float>(shadowMapResolution);
        vp.mHeight   = static_cast<float>(shadowMapResolution);
        vp.mMinDepth = 0.0f;
        vp.mMaxDepth = 1.0f;
        cmd.SetViewport(vp);
        Orange::Rhi::RHIScissor sc{};
        sc.mWidth  = shadowMapResolution;
        sc.mHeight = shadowMapResolution;
        cmd.SetScissor(sc);

        if (runCaster && layer < cascadeCount)
        {
            cmd.BindGraphicsPipeline(*shadowCasterPipeline);
            const glm::mat4& lightVP = cascadeViewProjs[layer];

            for (const auto& drawable : scene.Drawables())
            {
                if (!drawable.castsShadow)
                {
                    continue;  // 主 pass 仍会绘，只是不进 shadow map
                }
                if (!drawable.mesh.IsValid())
                {
                    continue;
                }
                auto cacheIt = meshCache.find(drawable.mesh.Value());
                if (cacheIt == meshCache.end())
                {
                    continue;
                }
                const auto& gpu = cacheIt->second;

                // shadow_caster 的 push constant：uLightViewProj(64) + uModel(64) = 128 B
                struct ShadowCasterPush { glm::mat4 lightVP; glm::mat4 model; };
                ShadowCasterPush data{};
                data.lightVP = lightVP;
                data.model   = drawable.worldMatrix;
                cmd.SetPushConstants(Orange::Rhi::ShaderStage::Vertex,
                                     0, static_cast<std::uint32_t>(sizeof(data)),
                                     &data);

                cmd.BindVertexBuffer(0, *gpu.vertexBuffer, 0);
                cmd.BindIndexBuffer(*gpu.indexBuffer, 0, Orange::Rhi::IndexFormat::UInt32);
                cmd.DrawIndexed(gpu.indexCount, 1, 0, 0, 0);
            }
        }

        cmd.EndRendering();
    }

    cmd.TransitionTexture(*shadowMap,
                          Orange::Rhi::TextureLayout::DepthStencilAttachment,
                          Orange::Rhi::TextureLayout::ShaderReadOnly);
    shadowMapLayoutShaderReadOnly = true;
    return true;
}

bool Pipeline::Impl::EnsurePointShadowCube()
{
    const std::uint32_t targetRes = shadowConfig.mapResolution > 0
                                        ? shadowConfig.mapResolution : 1024u;
    if (pointShadowCubes[0] && pointShadowCubeResolution == targetRes)
    {
        return true;
    }
    if (renderDevice == nullptr)
    {
        return false;
    }
    renderDevice->WaitIdle();
    for (auto& v : pointShadowFaceViews) { v.reset(); }
    for (auto& c : pointShadowCubes)     { c.reset(); }

    auto& rhi = renderDevice->GetRhiDevice();

    // N 个独立 6-layer TexCube（非 cubeArray，免 imageCubeArray feature）。
    for (std::uint32_t c = 0; c < kMaxPointShadowCasters; ++c)
    {
        Orange::Rhi::TextureDesc t{};
        t.mWidth       = targetRes;
        t.mHeight      = targetRes;
        t.mFormat      = Orange::Rhi::TextureFormat::D32Float;
        t.mDimension   = Orange::Rhi::TextureDimension::TexCube;  // 6 layer
        t.mArrayLayers = 6u;
        t.mUsage       = Orange::Rhi::TextureUsage::DepthStencil
                       | Orange::Rhi::TextureUsage::Sampled;
        auto tex = rhi.CreateTexture(t);
        if (!tex)
        {
            ORANGE_LOG_ERROR("Pipeline: point shadow cube[{}] CreateTexture 失败 ({}x{} D32Float)",
                             c, targetRes, targetRes);
            return false;
        }
        pointShadowCubes[c] = std::move(tex);

        // per-face Tex2D depth view，flat index = c*6 + face。
        for (std::uint32_t face = 0; face < 6u; ++face)
        {
            Orange::Rhi::TextureViewDesc vd{};
            vd.mViewDimension  = Orange::Rhi::TextureDimension::Tex2D;
            vd.mBaseMipLevel   = 0;
            vd.mLevelCount     = 1;
            vd.mBaseArrayLayer = face;
            vd.mLayerCount     = 1;
            auto view = rhi.CreateTextureView(*pointShadowCubes[c], vd);
            if (!view)
            {
                ORANGE_LOG_ERROR("Pipeline: point shadow cube[{}] CreateTextureView(face={}) 失败",
                                 c, face);
                return false;
            }
            pointShadowFaceViews[c * 6u + face] = std::move(view);
        }

        // binding (kPointShadowBinding0 + c) = samplerCube uPointShadowCube<c>。
        if (mainDescSet && hdrSampler)
        {
            Orange::Rhi::DescriptorWrite w{};
            w.mBinding             = kPointShadowBinding0 + c;
            w.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            w.mImageInfo.mpTexture = pointShadowCubes[c].get();
            w.mImageInfo.mpSampler = hdrSampler.get();
            rhi.UpdateDescriptorSet(*mainDescSet, &w, 1);
        }
    }

    pointShadowCubeResolution           = targetRes;
    pointShadowCubeLayoutShaderReadOnly = false;
    return true;
}

bool Pipeline::Impl::RecordPointShadowPass()
{
    ORANGE_PROFILE_SCOPE("PointShadow");
    if (!pointShadowCubes[0] || offscreenCmd == nullptr || !shadowCasterPipeline)
    {
        return false;
    }
    auto& cmd = *offscreenCmd;

    // cube face 朝向 / up（标准 cubemap 约定 +X/-X/+Y/-Y/+Z/-Z）。配合
    // Camera::Perspective 的 Vulkan y-flip，使采样方向 D 命中渲染时对应
    // texel。RecordSpotShadowPass 同款 per-face depth attachment。
    static const glm::vec3 kFaceDir[6] = {
        { 1.0f,  0.0f,  0.0f}, {-1.0f,  0.0f,  0.0f},
        { 0.0f,  1.0f,  0.0f}, { 0.0f, -1.0f,  0.0f},
        { 0.0f,  0.0f,  1.0f}, { 0.0f,  0.0f, -1.0f},
    };
    static const glm::vec3 kFaceUp[6] = {
        { 0.0f, -1.0f,  0.0f}, { 0.0f, -1.0f,  0.0f},
        { 0.0f,  0.0f,  1.0f}, { 0.0f,  0.0f, -1.0f},
        { 0.0f, -1.0f,  0.0f}, { 0.0f, -1.0f,  0.0f},
    };
    constexpr float kFovY90 = 1.57079632679489661923f;  // 每 face 90° fov（= π/2 弧度）

    const auto fromLayout = pointShadowCubeLayoutShaderReadOnly
        ? Orange::Rhi::TextureLayout::ShaderReadOnly
        : Orange::Rhi::TextureLayout::Undefined;

    // 逐 cube（= 逐 shadow-casting point）。caster >= pointShadowCount 的 cube
    // 仍 Clear 各 face 到 1.0（远深度），保证它在 ShaderReadOnly 且无残留。
    for (std::uint32_t caster = 0; caster < kMaxPointShadowCasters; ++caster)
    {
        cmd.TransitionTexture(*pointShadowCubes[caster], fromLayout,
                              Orange::Rhi::TextureLayout::DepthStencilAttachment);

        for (std::uint32_t face = 0; face < 6u; ++face)
        {
            Orange::Rhi::DepthStencilAttachment depth{};
            depth.mpView        = pointShadowFaceViews[caster * 6u + face].get();
            depth.mDepthLoadOp  = Orange::Rhi::LoadOp::Clear;
            depth.mDepthStoreOp = Orange::Rhi::StoreOp::Store;
            depth.mClear.mDepth = 1.0f;

            Orange::Rhi::RenderingDesc rd{};
            rd.mRenderArea.mWidth  = pointShadowCubeResolution;
            rd.mRenderArea.mHeight = pointShadowCubeResolution;
            rd.mDepthStencil       = depth;
            cmd.BeginRendering(rd);

            Orange::Rhi::RHIViewport vp{};
            vp.mWidth    = static_cast<float>(pointShadowCubeResolution);
            vp.mHeight   = static_cast<float>(pointShadowCubeResolution);
            vp.mMinDepth = 0.0f;
            vp.mMaxDepth = 1.0f;
            cmd.SetViewport(vp);
            Orange::Rhi::RHIScissor sc{};
            sc.mWidth  = pointShadowCubeResolution;
            sc.mHeight = pointShadowCubeResolution;
            cmd.SetScissor(sc);

            if (caster < pointShadowCount)
            {
                const glm::vec3 lp   = pointShadowLightPos[caster];
                const float     zFar = pointShadowFar[caster];
                const glm::mat4 view = glm::lookAt(lp, lp + kFaceDir[face], kFaceUp[face]);
                // cube 专用 perspective：Vulkan z[0,1] 但 **不 y-flip**。硬件
                // cubemap 采样按固定约定把方向 → (face, uv)，配合标准 face
                // 朝向/up 向量时要求渲染端用「不翻 y」的投影；若沿用主帧的
                // Camera::Perspective（p[1][1]=-f y-flip），采样方向会命中
                // 上下镜像的 texel → 非 -Z 主轴方向（如头顶点光照水平地面、
                // 压 -Y 面）阴影完全错位。z 行不受 y-flip 影响，故距离重建
                // （SamplePointShadow）保持有效。fov=90° → cot(45°)=1。
                const float     fcot = 1.0f / std::tan(kFovY90 * 0.5f);
                glm::mat4       proj(0.0f);
                proj[0][0] = fcot;
                proj[1][1] = fcot;  // 不取负 —— 与主帧 Camera::Perspective 的关键区别
                proj[2][2] = zFar / (kPointShadowNear - zFar);
                proj[2][3] = -1.0f;
                proj[3][2] = (kPointShadowNear * zFar) / (kPointShadowNear - zFar);
                const glm::mat4 lightVP = proj * view;

                cmd.BindGraphicsPipeline(*shadowCasterPipeline);
                for (const auto& drawable : scene.Drawables())
                {
                    if (!drawable.castsShadow || !drawable.mesh.IsValid())
                    {
                        continue;
                    }
                    auto cacheIt = meshCache.find(drawable.mesh.Value());
                    if (cacheIt == meshCache.end())
                    {
                        continue;
                    }
                    const auto& gpu = cacheIt->second;

                    struct ShadowCasterPush { glm::mat4 lightVP; glm::mat4 model; };
                    ShadowCasterPush pc{};
                    pc.lightVP = lightVP;
                    pc.model   = drawable.worldMatrix;
                    cmd.SetPushConstants(Orange::Rhi::ShaderStage::Vertex,
                                         0, static_cast<std::uint32_t>(sizeof(pc)),
                                         &pc);

                    cmd.BindVertexBuffer(0, *gpu.vertexBuffer, 0);
                    cmd.BindIndexBuffer(*gpu.indexBuffer, 0, Orange::Rhi::IndexFormat::UInt32);
                    cmd.DrawIndexed(gpu.indexCount, 1, 0, 0, 0);
                }
            }

            cmd.EndRendering();
        }

        cmd.TransitionTexture(*pointShadowCubes[caster],
                              Orange::Rhi::TextureLayout::DepthStencilAttachment,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
    }

    pointShadowCubeLayoutShaderReadOnly = true;
    return true;
}

}  // namespace Orange::Engine::Render
