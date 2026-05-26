// Pipeline::Impl 的 shadow / light UBO 系列实现：EnsureShadowMap /
// ComputeLightViewProj / UpdateLightUbo / UpdatePointLightsUbo /
// RecordShadowPass。directional light + 简化 ortho frustum；point light 上
// 限 kMaxPointLights 走独立 UBO。

#include "PipelineImpl.h"

#include "orange/engine/core/Profiler.h"
#include "orange/engine/scene/TransformComponent.h"
#include "orange/engine/scene/World.h"

#include <glm/gtc/matrix_transform.hpp>

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
    shadowMap.reset();

    Orange::Rhi::TextureDesc t{};
    t.mWidth     = targetRes;
    t.mHeight    = targetRes;
    t.mFormat    = Orange::Rhi::TextureFormat::D32Float;
    t.mUsage     = Orange::Rhi::TextureUsage::DepthStencil
                 | Orange::Rhi::TextureUsage::Sampled;
    auto tex = renderDevice->GetRhiDevice().CreateTexture(t);
    if (!tex)
    {
        ORANGE_LOG_ERROR("Pipeline: shadow map CreateTexture 失败 ({}x{} D32Float)",
                         targetRes, targetRes);
        return false;
    }
    shadowMap                      = std::move(tex);
    shadowMapResolution            = targetRes;
    shadowMapLayoutShaderReadOnly  = false;

    // 把 main desc set 的 binding 0 重新指向新 shadow view。binding 1 已
    // 在 EnsureMainDescriptorSetBinding 时绑过 lightUbo。
    if (mainDescSet && hdrSampler)
    {
        Orange::Rhi::DescriptorWrite write{};
        write.mBinding             = 0;
        write.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
        write.mImageInfo.mpTexture = shadowMap.get();
        write.mImageInfo.mpSampler = hdrSampler.get();  // 与 HDR sampler 共用一个 linear sampler
        renderDevice->GetRhiDevice().UpdateDescriptorSet(*mainDescSet, &write, 1);
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

void Pipeline::Impl::UpdateLightUbo(const DirectionalLight* light,
                                    const glm::vec3&        lightWorldDir,
                                    const glm::mat4&        lightViewProj,
                                    const glm::vec3&        cameraWorldPos,
                                    const glm::vec3&        iblTintIntensity)
{
    if (!lightUbo)
    {
        return;
    }
    LightUboData data{};
    data.lightViewProj = lightViewProj;
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
    data.shadowParams = glm::vec4(static_cast<float>(shadowConfig.pcfKernelRadius),
                                  shadowConfig.depthBias,
                                  0.0f, 0.0f);
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
        data.lights[count].posRange       = glm::vec4(pos, pl.range);
        data.lights[count].colorIntensity = glm::vec4(pl.color, pl.intensity);
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

        data.lights[count].posRange       = glm::vec4(pos, sl.range);
        data.lights[count].dirCosOuter    = glm::vec4(dir, outerCos);
        data.lights[count].colorIntensity = glm::vec4(sl.color, sl.intensity);
        // shadow index 占位 -1（G1 无阴影；G2 透视阴影落地后由 shadow pass 写）。
        data.lights[count].cosInnerShadow = glm::vec4(innerCos, -1.0f, 0.0f, 0.0f);
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
}

bool Pipeline::Impl::RecordShadowPass(const DirectionalLight* light,
                                      const glm::mat4& lightViewProj)
{
    ORANGE_PROFILE_SCOPE("Shadow");
    if (!shadowMap || offscreenCmd == nullptr)
    {
        return false;
    }
    auto& cmd = *offscreenCmd;

    const auto fromLayout = shadowMapLayoutShaderReadOnly
        ? Orange::Rhi::TextureLayout::ShaderReadOnly
        : Orange::Rhi::TextureLayout::Undefined;
    cmd.TransitionTexture(*shadowMap, fromLayout,
                          Orange::Rhi::TextureLayout::DepthStencilAttachment);

    Orange::Rhi::DepthStencilAttachment depth{};
    depth.mpView          = shadowMap->GetDefaultView();
    depth.mDepthLoadOp    = Orange::Rhi::LoadOp::Clear;
    depth.mDepthStoreOp   = Orange::Rhi::StoreOp::Store;
    depth.mClear.mDepth   = 1.0f;

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

    // 光源不投影 / 缺失时——清完深度 = 1.0 即"远深度"，shadow_pcf 取
    // currentDepth <= 1.0 → 总是 1（全亮），等价于"无阴影"。
    const bool runCaster = (light != nullptr && light->castsShadow && shadowCasterPipeline);
    if (runCaster)
    {
        cmd.BindGraphicsPipeline(*shadowCasterPipeline);

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
            data.lightVP = lightViewProj;
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

    cmd.TransitionTexture(*shadowMap,
                          Orange::Rhi::TextureLayout::DepthStencilAttachment,
                          Orange::Rhi::TextureLayout::ShaderReadOnly);
    shadowMapLayoutShaderReadOnly = true;
    return true;
}

}  // namespace Orange::Engine::Render
