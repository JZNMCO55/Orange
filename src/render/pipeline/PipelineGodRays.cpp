// Pipeline 的 god rays 系列实现：EnsureGodRaysSet / RecordGodRaysPass。
// 复用 bloomLayout 单 binding sampler（绑 sceneDepth）+ 加性 blend 到 HDR；
// push constant 与 src/render/builtin_shaders/god_rays.frag.glsl 的
// push_constant block 严格 64 B 对齐。

#include "PipelineImpl.h"

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cstdint>

namespace Orange::Engine::Render
{

bool Pipeline::Impl::EnsureGodRaysSet()
{
    if (renderDevice == nullptr || sceneDepth == nullptr || hdrSampler == nullptr
        || godRaysPipeline == nullptr || bloomLayout == nullptr)
    {
        return false;
    }

    auto& rhi = renderDevice->GetRhiDevice();

    // 池只建一次（只有 1 个 set，长寿命）。后续仅刷新 binding。
    if (!godRaysPool)
    {
        Orange::Rhi::DescriptorPoolDesc poolDesc{};
        poolDesc.mMaxSets = 1;
        Orange::Rhi::DescriptorPoolSize sz{};
        sz.mType  = Orange::Rhi::DescriptorType::CombinedImageSampler;
        sz.mCount = 1;
        poolDesc.mPoolSizes.push_back(sz);
        poolDesc.mpDebugName = "orange_engine.god_rays.pool";
        godRaysPool = rhi.CreateDescriptorPool(poolDesc);
        if (!godRaysPool)
        {
            ORANGE_LOG_ERROR("Pipeline: god rays CreateDescriptorPool 失败");
            return false;
        }
    }

    // sceneDepth 重建后（OnResize / 首次） → 重新分配 set 并写 binding。
    if (godRaysSet == nullptr || godRaysSetBoundDepth != sceneDepth.get())
    {
        godRaysSet.reset();
        auto set = rhi.AllocateDescriptorSet(*godRaysPool, *bloomLayout);
        if (!set)
        {
            ORANGE_LOG_ERROR("Pipeline: god rays AllocateDescriptorSet 失败");
            return false;
        }
        Orange::Rhi::DescriptorWrite w{};
        w.mBinding             = 0;
        w.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
        w.mImageInfo.mpTexture = sceneDepth.get();
        w.mImageInfo.mpSampler = hdrSampler.get();
        rhi.UpdateDescriptorSet(*set, &w, 1);

        godRaysSet           = std::move(set);
        godRaysSetBoundDepth = sceneDepth.get();
    }
    return true;
}

bool Pipeline::Impl::RecordGodRaysPass(const GodRaysPass& gr,
                                       const glm::mat4&   viewProj)
{
    if (offscreenCmd == nullptr || hdrColor == nullptr || sceneDepth == nullptr
        || godRaysPipeline == nullptr)
    {
        return false;
    }
    if (!EnsureGodRaysSet())
    {
        return false;
    }

    auto& cmd = *offscreenCmd;

    // 1. sceneDepth：DepthStencilAttachment → ShaderReadOnly（让 frag
    //    采样它做 occlusion proxy）。下一帧主 pass 会翻回 DSA。
    if (!sceneDepthLayoutShaderReadOnly)
    {
        cmd.TransitionTexture(*sceneDepth,
                              Orange::Rhi::TextureLayout::DepthStencilAttachment,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
        sceneDepthLayoutShaderReadOnly = true;
    }

    // 2. HDR：调用此函数时 hdrLayoutShaderReadOnly == true（RecordOffscreenPass
    //    末尾 + 粒子 pass 末尾 + bloom pass 末尾都会留在 ShaderReadOnly）。
    //    god rays 要写 HDR 故 transition 回 ColorAttachment。
    cmd.TransitionTexture(*hdrColor,
                          Orange::Rhi::TextureLayout::ShaderReadOnly,
                          Orange::Rhi::TextureLayout::ColorAttachment);

    Orange::Rhi::ColorAttachment att{};
    att.mpView   = hdrColor->GetDefaultView();
    att.mLoadOp  = Orange::Rhi::LoadOp::Load;
    att.mStoreOp = Orange::Rhi::StoreOp::Store;

    Orange::Rhi::RenderingDesc rd{};
    rd.mRenderArea.mWidth  = hdrWidth;
    rd.mRenderArea.mHeight = hdrHeight;
    rd.mColorAttachments.push_back(att);

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

    cmd.BindGraphicsPipeline(*godRaysPipeline);
    cmd.SetDescriptorSet(0, *godRaysSet);

    // 计算 sun 屏幕 uv：把 -sunWorldDir × kFar 这个"远点"投到 NDC，再
    // 翻成 uv。sun 在 frustum 之外时 sunUV 越界 → frag 仍能跑，但 visibility
    // 几乎处处 0 → 视觉上 god rays 自然消失（已知限制）。
    glm::vec3 sunDir = gr.sunWorldDir;
    if (glm::dot(sunDir, sunDir) > 1e-6f)
    {
        sunDir = glm::normalize(sunDir);
    }
    else
    {
        sunDir = glm::vec3(0.0f, -1.0f, 0.0f);
    }
    constexpr float kFarPos     = 1000.0f;
    const glm::vec3  sunPosWorld = -sunDir * kFarPos;
    const glm::vec4  ndc4        = viewProj * glm::vec4(sunPosWorld, 1.0f);
    const float      invW        = (ndc4.w != 0.0f) ? (1.0f / ndc4.w) : 1.0f;
    const float      sunNdcX     = ndc4.x * invW;
    const float      sunNdcY     = ndc4.y * invW;
    const glm::vec2  sunUv(sunNdcX * 0.5f + 0.5f, sunNdcY * 0.5f + 0.5f);

    // push constant 布局必须与 god_rays.frag.glsl 的 push_constant block
    // 字节布局严格对齐；任一处改了，另一处必须同步。
    struct GodRaysPush
    {
        glm::vec2 sunUV;
        float     density;
        float     decay;

        glm::vec3 sunColor;
        float     weight;

        float     exposure;
        float     pad0;
        float     pad1;
        float     pad2;

        std::int32_t numSamples;
        std::int32_t padI0;
        std::int32_t padI1;
        std::int32_t padI2;
    };
    static_assert(sizeof(GodRaysPush) == 64,
                  "GodRaysPush must match god_rays.frag push_constant block (64 B).");

    GodRaysPush push{};
    push.sunUV      = sunUv;
    push.density    = gr.density;
    push.decay      = gr.decay;
    push.sunColor   = gr.sunColor;
    push.weight     = gr.weight;
    push.exposure   = gr.exposure;
    push.numSamples = std::max(gr.numSamples, 1);

    cmd.SetPushConstants(Orange::Rhi::ShaderStage::Fragment, 0,
                         sizeof(GodRaysPush), &push);

    cmd.Draw(3, 1, 0, 0);
    cmd.EndRendering();

    // 3. HDR 翻回 ShaderReadOnly：下一段（capture / tonemap stage B）按
    //    "HDR 在 ShaderReadOnly"假设跑。
    cmd.TransitionTexture(*hdrColor,
                          Orange::Rhi::TextureLayout::ColorAttachment,
                          Orange::Rhi::TextureLayout::ShaderReadOnly);
    return true;
}

}  // namespace Orange::Engine::Render
