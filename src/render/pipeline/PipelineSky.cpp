// Pipeline 的 sky 系列实现：EnsureSkyDescSet / RecordSkyPass（cubemap 走样
// 烘焙 env cube）/ RecordProceduralSkyPass（无 cubemap fallback，
// procedural_sky.frag 内部三色调色板 + sun disc）。

#include "PipelineImpl.h"

#include "orange/engine/core/Profiler.h"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace Orange::Engine::Render
{

    bool Pipeline::Impl::EnsureSkyDescSet()
    {
        if (renderDevice == nullptr || skyPipeline == nullptr || skyLayout == nullptr || hdrSampler == nullptr || bakedEnvCube == nullptr)
        {
            return false;
        }

        auto& rhi = renderDevice->GetRhiDevice();

        // 池只建一次（1 个 set，长寿命）。后续仅 rewrite binding。
        if (!skyPool)
        {
            Orange::Rhi::DescriptorPoolDesc poolDesc{};
            poolDesc.mMaxSets = 1;
            Orange::Rhi::DescriptorPoolSize sz{};
            sz.mType  = Orange::Rhi::DescriptorType::CombinedImageSampler;
            sz.mCount = 1;
            poolDesc.mPoolSizes.push_back(sz);
            poolDesc.mpDebugName = "orange_engine.sky.pool";
            skyPool              = rhi.CreateDescriptorPool(poolDesc);
            if (!skyPool)
            {
                ORANGE_LOG_ERROR("Pipeline: sky CreateDescriptorPool 失败");
                return false;
            }
        }

        // bakedEnvCube 重建（BakeIblFromWorld 重跑、首次烘焙完成）→ 重新分配
        // set 并写 binding 0。同款"绑定缓存"惯例参 EnsureGodRaysSet。
        if (skySet == nullptr || skySetBoundCube != bakedEnvCube.get())
        {
            skySet.reset();
            auto set = rhi.AllocateDescriptorSet(*skyPool, *skyLayout);
            if (!set)
            {
                ORANGE_LOG_ERROR("Pipeline: sky AllocateDescriptorSet 失败");
                return false;
            }
            Orange::Rhi::DescriptorWrite w{};
            w.mBinding             = 0;
            w.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            w.mImageInfo.mpTexture = bakedEnvCube.get();
            w.mImageInfo.mpSampler = hdrSampler.get();
            rhi.UpdateDescriptorSet(*set, &w, 1);

            skySet          = std::move(set);
            skySetBoundCube = bakedEnvCube.get();
        }
        return true;
    }

    bool Pipeline::Impl::RecordSkyPass(const glm::mat4& invViewProj,
                                       const glm::vec3& cameraPos,
                                       const glm::vec3& tint,
                                       float            intensity)
    {
        ORANGE_PROFILE_SCOPE("Sky");
        if (offscreenCmd == nullptr || hdrColor == nullptr || skyPipeline == nullptr)
        {
            return false;
        }
        if (!EnsureSkyDescSet())
        {
            return false; // 无 bakedEnvCube 或资源未就绪
        }

        auto& cmd = *offscreenCmd;

        // hdrColor 当前 layout：上一帧末尾通常翻在 ShaderReadOnly（passthrough /
        // bloom 末尾的契约）；首帧 hdrLayoutShaderReadOnly == false（Undefined）。
        const auto fromLayout = hdrLayoutShaderReadOnly
                                    ? Orange::Rhi::TextureLayout::ShaderReadOnly
                                    : Orange::Rhi::TextureLayout::Undefined;
        cmd.TransitionTexture(*hdrColor, fromLayout,
                              Orange::Rhi::TextureLayout::ColorAttachment);
        hdrLayoutShaderReadOnly = false;

        Orange::Rhi::ColorAttachment att{};
        att.mpView   = hdrColor->GetDefaultView();
        att.mLoadOp  = Orange::Rhi::LoadOp::Clear;
        att.mStoreOp = Orange::Rhi::StoreOp::Store;
        // v1.3.0 中性化：用 sceneClearColor 字段（公共 API SetSceneClearColor
        // 控制）取代硬写值。sky shader 会全屏覆盖，此处 clear 仅用于驱动 spec
        // requirement，不影响最终视觉；但与主 pass clear 用同一字段值能让
        // sky 关 / 烘焙失败 fallback 路径视觉与主 pass clear 一致。
        att.mClear.mColor[0] = sceneClearColor.x;
        att.mClear.mColor[1] = sceneClearColor.y;
        att.mClear.mColor[2] = sceneClearColor.z;
        att.mClear.mColor[3] = 1.0f;

        Orange::Rhi::RenderingDesc rd{};
        rd.mRenderArea.mWidth  = hdrWidth;
        rd.mRenderArea.mHeight = hdrHeight;
        rd.mColorAttachments.push_back(att);
        // 不挂 depth attachment —— sky 不消费 / 不修改 depth；主 pass 自家
        // BeginRendering 会以 LoadOp::Clear depth = 1.0 重建 sceneDepth。

        cmd.BeginRendering(rd);

        Orange::Rhi::RHIViewport vp{};
        vp.mWidth    = static_cast<float>(hdrWidth);
        vp.mHeight   = static_cast<float>(hdrHeight);
        vp.mMinDepth = 0.0f;
        vp.mMaxDepth = 1.0f;
        cmd.SetViewport(vp);
        Orange::Rhi::RHIScissor sc{};
        sc.mWidth  = hdrWidth;
        sc.mHeight = hdrHeight;
        cmd.SetScissor(sc);

        cmd.BindGraphicsPipeline(*skyPipeline);
        cmd.SetDescriptorSet(0, *skySet);

        // push constant 96 字节：mat4 invVP + vec3 cameraPos + float intensity
        // + vec3 tint + float pad；与 sky.frag.glsl push_constant block 严格对齐。
        struct SkyPush
        {
            glm::mat4 invViewProj;
            glm::vec3 cameraPos;
            float     intensity;
            glm::vec3 tint;
            float     pad0;
        };
        static_assert(sizeof(SkyPush) == 96,
                      "SkyPush must match sky.frag push_constant block (96 B).");

        SkyPush push{};
        push.invViewProj = invViewProj;
        push.cameraPos   = cameraPos;
        push.intensity   = intensity;
        push.tint        = tint;
        cmd.SetPushConstants(Orange::Rhi::ShaderStage::Fragment, 0,
                             sizeof(SkyPush), &push);

        cmd.Draw(3, 1, 0, 0);
        cmd.EndRendering();

        // hdrColor 留在 ColorAttachment —— 主 pass 入口接 LoadOp::Load 直接
        // 沿用本帧 sky 输出作背景。
        return true;
    }

    bool Pipeline::Impl::RecordProceduralSkyPass(const glm::mat4& invViewProj,
                                                 const glm::vec3& cameraPos,
                                                 const glm::vec3& sunDir,
                                                 const glm::vec3& sunColor,
                                                 float            sunIntensity)
    {
        ORANGE_PROFILE_SCOPE("Sky");
        if (offscreenCmd == nullptr || hdrColor == nullptr || proceduralSkyPipeline == nullptr)
        {
            return false;
        }

        auto& cmd = *offscreenCmd;

        // hdrColor transition：与 RecordSkyPass 同款入口契约（上一帧通常翻在
        // ShaderReadOnly；首帧 Undefined）。
        const auto fromLayout = hdrLayoutShaderReadOnly
                                    ? Orange::Rhi::TextureLayout::ShaderReadOnly
                                    : Orange::Rhi::TextureLayout::Undefined;
        cmd.TransitionTexture(*hdrColor, fromLayout,
                              Orange::Rhi::TextureLayout::ColorAttachment);
        hdrLayoutShaderReadOnly = false;

        // BUG-2026-05-19-vk-cmd-begin-rendering-crash-on-intel-iris-xe workaround：
        // procedural sky pipeline 声明了 depth attachment format（见 SetupRhiResources），
        // RenderingDesc 必须对齐挂 sceneDepth；否则 Intel Iris Xe ICD 走 color-only
        // dynamic rendering 路径 deref 0x3B0 段错误。本 transition / attachment
        // 是 pure workaround，shader 不消费 depth、depth test / write 均关。NV 上
        // 行为不变（with-depth 路径也 well-tested）。
        const auto fromDepthLayout = sceneDepthLayoutShaderReadOnly
                                         ? Orange::Rhi::TextureLayout::ShaderReadOnly
                                         : Orange::Rhi::TextureLayout::Undefined;
        cmd.TransitionTexture(*sceneDepth, fromDepthLayout,
                              Orange::Rhi::TextureLayout::DepthStencilAttachment);
        sceneDepthLayoutShaderReadOnly = false;

        Orange::Rhi::ColorAttachment att{};
        att.mpView   = hdrColor->GetDefaultView();
        att.mLoadOp  = Orange::Rhi::LoadOp::Clear;
        att.mStoreOp = Orange::Rhi::StoreOp::Store;
        // v1.3.0 中性化：用 sceneClearColor 字段（公共 API SetSceneClearColor
        // 控制）。procedural sky 路径与 cubemap sky 路径走同一字段，关 sky /
        // fallback 时与主 pass clear 视觉一致。
        att.mClear.mColor[0] = sceneClearColor.x;
        att.mClear.mColor[1] = sceneClearColor.y;
        att.mClear.mColor[2] = sceneClearColor.z;
        att.mClear.mColor[3] = 1.0f;

        // Dummy depth attachment（pipeline 已声明 D32 format，此处必须配对）。
        // LoadOp=Clear 把 depth 清到 far（1.0），与主 pass 行为同款 —— 主 pass 之
        // 后接的 RecordOffscreenPass 仍会 Clear depth，本 sky pass 写的 depth 值
        // 被主 pass 抹掉，纯占位无副作用。StoreOp=Store 让主 pass 入口 transition
        // 起点合法（也可 DontCare，但 Store 与现有 pattern 一致）。
        Orange::Rhi::DepthStencilAttachment depthAtt{};
        depthAtt.mpView        = sceneDepth->GetDefaultView();
        depthAtt.mDepthLoadOp  = Orange::Rhi::LoadOp::Clear;
        depthAtt.mDepthStoreOp = Orange::Rhi::StoreOp::Store;
        depthAtt.mClear.mDepth = 1.0f;

        Orange::Rhi::RenderingDesc rd{};
        rd.mRenderArea.mWidth  = hdrWidth;
        rd.mRenderArea.mHeight = hdrHeight;
        rd.mColorAttachments.push_back(att);
        rd.mDepthStencil = depthAtt;

        cmd.BeginRendering(rd);

        Orange::Rhi::RHIViewport vp{};
        vp.mWidth    = static_cast<float>(hdrWidth);
        vp.mHeight   = static_cast<float>(hdrHeight);
        vp.mMinDepth = 0.0f;
        vp.mMaxDepth = 1.0f;
        cmd.SetViewport(vp);
        Orange::Rhi::RHIScissor sc{};
        sc.mWidth  = hdrWidth;
        sc.mHeight = hdrHeight;
        cmd.SetScissor(sc);

        cmd.BindGraphicsPipeline(*proceduralSkyPipeline);

        // push 112 字节，必须与 procedural_sky.frag.glsl 内 push_constant block
        // 严格对齐（mat4 + 3 组 vec3+float，std430 自然 16B 对齐）。zenith /
        // horizon palette 在 shader 内 hardcode（v0.x baseline），未来扩展时
        // 升级为 push 字段或 UBO。
        struct ProceduralSkyPush
        {
            glm::mat4 invViewProj;  // 64
            glm::vec3 cameraPos;    // 12
            float     pad0;         //  4
            glm::vec3 sunDir;       // 12
            float     sunSize;      //  4
            glm::vec3 sunColor;     // 12
            float     sunIntensity; //  4
        };
        static_assert(sizeof(ProceduralSkyPush) == 112,
                      "ProceduralSkyPush must match procedural_sky.frag push_constant (112 B).");

        ProceduralSkyPush push{};
        push.invViewProj = invViewProj;
        push.cameraPos   = cameraPos;
        push.sunDir      = sunDir;
        // sunSize 是 disc 阈值的 cos 值，越接近 1 = 越小。0.9995 ≈ 视角 ~1.8°，
        // 比真实太阳（~0.53°）偏大，编辑器观感优先 —— 用户能清楚看到一个圆盘
        // 而非"针尖"。
        push.sunSize      = 0.9995f;
        push.sunColor     = sunColor;
        push.sunIntensity = sunIntensity;

        cmd.SetPushConstants(Orange::Rhi::ShaderStage::Fragment, 0,
                             sizeof(ProceduralSkyPush), &push);

        cmd.Draw(3, 1, 0, 0);
        cmd.EndRendering();

        // hdrColor 留在 ColorAttachment，主 pass 接 LoadOp::Load。
        return true;
    }

} // namespace Orange::Engine::Render
