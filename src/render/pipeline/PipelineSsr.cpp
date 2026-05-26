// Pipeline 的 SSR 系列实现：EnsureSsrResources / RecordSsrPass。
//
// 两个 fullscreen sub-pass：
//   1. ssr：sceneDepth + hdrColor(作反射源) + ubo → ssrColor(RGBA16F，反射
//      色×权重)。读 hdrColor(ShaderReadOnly) 写独立 ssrColor，避免"采 HDR
//      同时写 HDR"的 framebuffer 反馈。
//   2. ssr_composite：ssrColor 加性 blend 进 hdrColor。
//
// 资源：ssrColor 随 HDR 尺寸重建；ssrSet（depth+hdr+ubo）在 sceneDepth /
// hdrColor 重建后重绑；ssrCompositeSet（ssrColor）在 ssrColor 重建后重绑。

#include "PipelineImpl.h"

#include "orange/engine/render/PostProcessPasses.h"

#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>   // glm::inverse

#include <cstring>

namespace Orange::Engine::Render
{

bool Pipeline::Impl::EnsureSsrResources()
{
    if (renderDevice == nullptr || sceneDepth == nullptr || hdrColor == nullptr
        || hdrSampler == nullptr || ssrPipeline == nullptr
        || ssrCompositePipeline == nullptr || ssrLayout == nullptr
        || bloomLayout == nullptr || ssrUbo == nullptr)
    {
        return false;
    }
    if (hdrWidth == 0 || hdrHeight == 0)
    {
        return false;
    }
    auto& rhi = renderDevice->GetRhiDevice();

    if (!ssrPool)
    {
        Orange::Rhi::DescriptorPoolDesc poolDesc{};
        poolDesc.mMaxSets = 2;
        poolDesc.mPoolSizes.push_back({Orange::Rhi::DescriptorType::CombinedImageSampler, 3});
        poolDesc.mPoolSizes.push_back({Orange::Rhi::DescriptorType::UniformBuffer, 1});
        poolDesc.mpDebugName = "orange_engine.ssr.pool";
        ssrPool = rhi.CreateDescriptorPool(poolDesc);
        if (!ssrPool) { return false; }
    }

    // ssrColor：RGBA16F，随 HDR 尺寸重建。
    if (!ssrColor || ssrColorWidth != hdrWidth || ssrColorHeight != hdrHeight)
    {
        renderDevice->WaitIdle();
        ssrColor.reset();
        ssrCompositeSet.reset();
        ssrCompositeSetBound = nullptr;

        Orange::Rhi::TextureDesc t{};
        t.mWidth     = hdrWidth;
        t.mHeight    = hdrHeight;
        t.mFormat    = PipelineDetail::kHdrColorFormat;
        t.mDimension = Orange::Rhi::TextureDimension::Tex2D;
        t.mUsage     = Orange::Rhi::TextureUsage::RenderTarget
                     | Orange::Rhi::TextureUsage::Sampled;
        ssrColor = rhi.CreateTexture(t);
        if (!ssrColor)
        {
            ORANGE_LOG_ERROR("Pipeline: ssrColor CreateTexture 失败 ({}x{} RGBA16F)",
                             hdrWidth, hdrHeight);
            return false;
        }
        ssrColorWidth                = hdrWidth;
        ssrColorHeight               = hdrHeight;
        ssrColorLayoutShaderReadOnly = false;
    }

    // ssrSet（0=sceneDepth, 1=hdrColor, 2=ssrUbo）：depth / hdr 重建后重绑。
    if (ssrSet == nullptr || ssrSetBoundDepth != sceneDepth.get()
        || ssrSetBoundHdr != hdrColor.get())
    {
        ssrSet.reset();
        auto set = rhi.AllocateDescriptorSet(*ssrPool, *ssrLayout);
        if (!set) { return false; }
        Orange::Rhi::DescriptorWrite w[3]{};
        w[0].mBinding             = 0;
        w[0].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
        w[0].mImageInfo.mpTexture = sceneDepth.get();
        w[0].mImageInfo.mpSampler = hdrSampler.get();
        w[1].mBinding             = 1;
        w[1].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
        w[1].mImageInfo.mpTexture = hdrColor.get();
        w[1].mImageInfo.mpSampler = hdrSampler.get();
        w[2].mBinding             = 2;
        w[2].mType                = Orange::Rhi::DescriptorType::UniformBuffer;
        w[2].mBufferInfo.mpBuffer = ssrUbo.get();
        w[2].mBufferInfo.mOffset  = 0;
        w[2].mBufferInfo.mRange   = sizeof(SsrUboData);
        rhi.UpdateDescriptorSet(*set, w, 3);
        ssrSet           = std::move(set);
        ssrSetBoundDepth = sceneDepth.get();
        ssrSetBoundHdr   = hdrColor.get();
    }

    // ssrCompositeSet（0=ssrColor，复用 bloomLayout）。
    if (ssrCompositeSet == nullptr || ssrCompositeSetBound != ssrColor.get())
    {
        ssrCompositeSet.reset();
        auto set = rhi.AllocateDescriptorSet(*ssrPool, *bloomLayout);
        if (!set) { return false; }
        Orange::Rhi::DescriptorWrite w{};
        w.mBinding             = 0;
        w.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
        w.mImageInfo.mpTexture = ssrColor.get();
        w.mImageInfo.mpSampler = hdrSampler.get();
        rhi.UpdateDescriptorSet(*set, &w, 1);
        ssrCompositeSet      = std::move(set);
        ssrCompositeSetBound = ssrColor.get();
    }
    return true;
}

bool Pipeline::Impl::RecordSsrPass(const SsrPass& ssrDesc, const glm::mat4& proj)
{
    if (offscreenCmd == nullptr || hdrColor == nullptr || sceneDepth == nullptr)
    {
        return false;
    }
    if (!EnsureSsrResources())
    {
        return false;
    }

    // ---- 写 SsrUbo ----
    {
        SsrUboData data{};
        data.proj    = proj;
        data.invProj = glm::inverse(proj);
        data.params  = glm::vec4(ssrDesc.maxDistance, ssrDesc.maxSteps,
                                 ssrDesc.thickness, ssrDesc.strength);
        void* mapped = ssrUbo->Map();
        if (mapped == nullptr) { return false; }
        std::memcpy(mapped, &data, sizeof(data));
        ssrUbo->Unmap();
    }

    auto& cmd = *offscreenCmd;

    // sceneDepth → ShaderReadOnly（与 SSAO / god rays 共用 flag）。
    if (!sceneDepthLayoutShaderReadOnly)
    {
        cmd.TransitionTexture(*sceneDepth,
                              Orange::Rhi::TextureLayout::DepthStencilAttachment,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
        sceneDepthLayoutShaderReadOnly = true;
    }

    // ---- Pass 1：ssr（采 hdrColor[ShaderReadOnly] + depth → ssrColor）----
    // 此时 hdrColor 处于 ShaderReadOnly（主 pass / SSAO 末尾留下），可安全采样。
    {
        const auto from = ssrColorLayoutShaderReadOnly
            ? Orange::Rhi::TextureLayout::ShaderReadOnly
            : Orange::Rhi::TextureLayout::Undefined;
        cmd.TransitionTexture(*ssrColor, from,
                              Orange::Rhi::TextureLayout::ColorAttachment);

        Orange::Rhi::ColorAttachment att{};
        att.mpView   = ssrColor->GetDefaultView();
        att.mLoadOp  = Orange::Rhi::LoadOp::Clear;
        att.mStoreOp = Orange::Rhi::StoreOp::Store;  // clear 到 0（未命中处无反射贡献）

        Orange::Rhi::RenderingDesc rd{};
        rd.mRenderArea.mWidth  = ssrColorWidth;
        rd.mRenderArea.mHeight = ssrColorHeight;
        rd.mColorAttachments.push_back(att);
        cmd.BeginRendering(rd);

        Orange::Rhi::RHIViewport vp{};
        vp.mWidth = static_cast<float>(ssrColorWidth);
        vp.mHeight = static_cast<float>(ssrColorHeight);
        vp.mMinDepth = 0.0f; vp.mMaxDepth = 1.0f;
        cmd.SetViewport(vp);
        Orange::Rhi::RHIScissor sc{};
        sc.mWidth = ssrColorWidth; sc.mHeight = ssrColorHeight;
        cmd.SetScissor(sc);

        cmd.BindGraphicsPipeline(*ssrPipeline);
        cmd.SetDescriptorSet(0, *ssrSet);
        cmd.Draw(3, 1, 0, 0);
        cmd.EndRendering();

        cmd.TransitionTexture(*ssrColor,
                              Orange::Rhi::TextureLayout::ColorAttachment,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
        ssrColorLayoutShaderReadOnly = true;
    }

    // ---- Pass 2：ssr_composite（ssrColor 加性 blend 进 HDR）----
    {
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
        vp.mWidth = static_cast<float>(hdrWidth);
        vp.mHeight = static_cast<float>(hdrHeight);
        vp.mMinDepth = 0.0f; vp.mMaxDepth = 1.0f;
        cmd.SetViewport(vp);
        Orange::Rhi::RHIScissor sc{};
        sc.mWidth = hdrWidth; sc.mHeight = hdrHeight;
        cmd.SetScissor(sc);

        cmd.BindGraphicsPipeline(*ssrCompositePipeline);
        cmd.SetDescriptorSet(0, *ssrCompositeSet);
        cmd.Draw(3, 1, 0, 0);
        cmd.EndRendering();

        cmd.TransitionTexture(*hdrColor,
                              Orange::Rhi::TextureLayout::ColorAttachment,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
    }
    return true;
}

}  // namespace Orange::Engine::Render
