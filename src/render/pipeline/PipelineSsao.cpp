// Pipeline 的 SSAO 系列实现：EnsureSsaoResources / RecordSsaoPass。
//
// 两个 fullscreen sub-pass（与 god rays 同款"Pipeline 自管 cmd + 直接接
// sceneDepth / HDR target"模式）：
//   1. ssao：sceneDepth + SsaoUbo → ssaoColor（R8 原始 AO，程序化 hash 噪声）；
//   2. ssao_apply：ssaoColor 4×4 box 模糊 → 乘法 blend（dst×src = HDR×AO）。
//
// 资源：ssaoColor 随 HDR 尺寸重建；ssaoSet（depth+ubo）在 sceneDepth 重建
// 后重绑；ssaoApplySet（ssaoColor）在 ssaoColor 重建后重绑。pipelines /
// layout / ubo / kernel 在 PipelineSetup 一次性建好。

#include "PipelineImpl.h"

#include "orange/engine/render/PostProcessPasses.h"

#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>   // glm::inverse

#include <algorithm>
#include <cstring>

namespace Orange::Engine::Render
{

bool Pipeline::Impl::EnsureSsaoResources()
{
    if (renderDevice == nullptr || sceneDepth == nullptr || hdrSampler == nullptr
        || ssaoPipeline == nullptr || ssaoApplyPipeline == nullptr
        || ssaoLayout == nullptr || bloomLayout == nullptr || ssaoUbo == nullptr)
    {
        return false;
    }
    if (hdrWidth == 0 || hdrHeight == 0)
    {
        return false;
    }
    // normalBuffer：SSAO 采真实几何法线（替代深度差分）；法线预通道在主 pass 前
    // 已 Ensure + 填好，这里再 Ensure 一次保证 descriptor 始终有有效贴图可绑。
    if (!EnsureNormalBuffer())
    {
        return false;
    }
    auto& rhi = renderDevice->GetRhiDevice();

    // descriptor 池：2 set（ssaoSet=depth+ubo+normal，ssaoApplySet=ssaoColor）。
    // sampler 计数 = ssaoSet 的 depth + normal（2）+ ssaoApplySet 的 ssaoColor（1）= 3。
    if (!ssaoPool)
    {
        Orange::Rhi::DescriptorPoolDesc poolDesc{};
        poolDesc.mMaxSets = 2;
        poolDesc.mPoolSizes.push_back({Orange::Rhi::DescriptorType::CombinedImageSampler, 3});
        poolDesc.mPoolSizes.push_back({Orange::Rhi::DescriptorType::UniformBuffer, 1});
        poolDesc.mpDebugName = "orange_engine.ssao.pool";
        ssaoPool = rhi.CreateDescriptorPool(poolDesc);
        if (!ssaoPool) { return false; }
    }

    // ssaoColor：R8 原始 AO，随 HDR 尺寸重建。
    if (!ssaoColor || ssaoColorWidth != hdrWidth || ssaoColorHeight != hdrHeight)
    {
        renderDevice->WaitIdle();
        ssaoColor.reset();
        ssaoApplySet.reset();        // ssaoColor 换了 → apply set 失效
        ssaoApplySetBoundAo = nullptr;

        Orange::Rhi::TextureDesc t{};
        t.mWidth     = hdrWidth;
        t.mHeight    = hdrHeight;
        t.mFormat    = Orange::Rhi::TextureFormat::R8Unorm;
        t.mDimension = Orange::Rhi::TextureDimension::Tex2D;
        t.mUsage     = Orange::Rhi::TextureUsage::RenderTarget
                     | Orange::Rhi::TextureUsage::Sampled;
        ssaoColor = rhi.CreateTexture(t);
        if (!ssaoColor)
        {
            ORANGE_LOG_ERROR("Pipeline: ssaoColor CreateTexture 失败 ({}x{} R8)",
                             hdrWidth, hdrHeight);
            return false;
        }
        ssaoColorWidth                = hdrWidth;
        ssaoColorHeight               = hdrHeight;
        ssaoColorLayoutShaderReadOnly = false;
    }

    // ssaoSet（binding 0=sceneDepth, 1=ssaoUbo, 2=normalBuffer）：sceneDepth 或
    // normalBuffer 重建后重绑。
    if (ssaoSet == nullptr || ssaoSetBoundDepth != sceneDepth.get()
        || ssaoSetBoundNormal != normalBuffer.get())
    {
        ssaoSet.reset();
        auto set = rhi.AllocateDescriptorSet(*ssaoPool, *ssaoLayout);
        if (!set) { return false; }
        Orange::Rhi::DescriptorWrite w[3]{};
        w[0].mBinding             = 0;
        w[0].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
        w[0].mImageInfo.mpTexture = sceneDepth.get();
        w[0].mImageInfo.mpSampler = hdrSampler.get();
        w[1].mBinding             = 1;
        w[1].mType                = Orange::Rhi::DescriptorType::UniformBuffer;
        w[1].mBufferInfo.mpBuffer = ssaoUbo.get();
        w[1].mBufferInfo.mOffset  = 0;
        w[1].mBufferInfo.mRange   = sizeof(SsaoUboData);
        w[2].mBinding             = 2;
        w[2].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
        w[2].mImageInfo.mpTexture = normalBuffer.get();
        w[2].mImageInfo.mpSampler = hdrSampler.get();
        rhi.UpdateDescriptorSet(*set, w, 3);
        ssaoSet            = std::move(set);
        ssaoSetBoundDepth  = sceneDepth.get();
        ssaoSetBoundNormal = normalBuffer.get();
    }

    // ssaoApplySet（binding 0=ssaoColor）：ssaoColor 重建后重绑（复用 bloomLayout）。
    if (ssaoApplySet == nullptr || ssaoApplySetBoundAo != ssaoColor.get())
    {
        ssaoApplySet.reset();
        auto set = rhi.AllocateDescriptorSet(*ssaoPool, *bloomLayout);
        if (!set) { return false; }
        Orange::Rhi::DescriptorWrite w{};
        w.mBinding             = 0;
        w.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
        w.mImageInfo.mpTexture = ssaoColor.get();
        w.mImageInfo.mpSampler = hdrSampler.get();
        rhi.UpdateDescriptorSet(*set, &w, 1);
        ssaoApplySet        = std::move(set);
        ssaoApplySetBoundAo = ssaoColor.get();
    }
    return true;
}

bool Pipeline::Impl::RecordSsaoPass(const SsaoPass& ssaoDesc, const glm::mat4& proj)
{
    if (offscreenCmd == nullptr || hdrColor == nullptr || sceneDepth == nullptr)
    {
        return false;
    }
    if (!EnsureSsaoResources())
    {
        return false;
    }

    // ---- 写 SsaoUbo（proj/invProj/kernel/params）----
    {
        SsaoUboData data{};
        data.proj    = proj;
        data.invProj = glm::inverse(proj);
        const std::uint32_t kn = kSsaoKernelSize;
        for (std::uint32_t i = 0; i < kn; ++i) { data.kernel[i] = ssaoKernel[i]; }
        const int kernelSize = std::clamp(ssaoDesc.kernelSize, 1,
                                          static_cast<int>(kSsaoKernelSize));
        data.params  = glm::vec4(ssaoDesc.radius, ssaoDesc.bias,
                                 ssaoDesc.strength, ssaoDesc.power);
        // params2：x=kernelSize（半球 SSAO 用），y=sliceCount / z=stepsPerSlice
        //（GTAO 用）。两算法共用同一 UBO，各取所需。
        const int sliceCount = std::max(ssaoDesc.gtaoSliceCount, 1);
        const int stepsPer   = std::max(ssaoDesc.gtaoStepsPerSlice, 1);
        data.params2 = glm::vec4(static_cast<float>(kernelSize),
                                 static_cast<float>(sliceCount),
                                 static_cast<float>(stepsPer), 0.0f);
        void* mapped = ssaoUbo->Map();
        if (mapped == nullptr) { return false; }
        std::memcpy(mapped, &data, sizeof(data));
        ssaoUbo->Unmap();
    }

    auto& cmd = *offscreenCmd;

    // sceneDepth → ShaderReadOnly（与 god rays 共用 flag；SSAO 通常先跑）。
    if (!sceneDepthLayoutShaderReadOnly)
    {
        cmd.TransitionTexture(*sceneDepth,
                              Orange::Rhi::TextureLayout::DepthStencilAttachment,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
        sceneDepthLayoutShaderReadOnly = true;
    }

    // ---- Pass 1：ssao → ssaoColor ----
    {
        const auto from = ssaoColorLayoutShaderReadOnly
            ? Orange::Rhi::TextureLayout::ShaderReadOnly
            : Orange::Rhi::TextureLayout::Undefined;
        cmd.TransitionTexture(*ssaoColor, from,
                              Orange::Rhi::TextureLayout::ColorAttachment);

        Orange::Rhi::ColorAttachment att{};
        att.mpView   = ssaoColor->GetDefaultView();
        att.mLoadOp  = Orange::Rhi::LoadOp::Clear;   // 远平面/未覆盖处 = 1（全亮）由 shader 保证；clear 防残留
        att.mStoreOp = Orange::Rhi::StoreOp::Store;
        att.mClear.mColor[0] = 1.0f;

        Orange::Rhi::RenderingDesc rd{};
        rd.mRenderArea.mWidth  = ssaoColorWidth;
        rd.mRenderArea.mHeight = ssaoColorHeight;
        rd.mColorAttachments.push_back(att);
        cmd.BeginRendering(rd);

        Orange::Rhi::RHIViewport vp{};
        vp.mWidth = static_cast<float>(ssaoColorWidth);
        vp.mHeight = static_cast<float>(ssaoColorHeight);
        vp.mMinDepth = 0.0f; vp.mMaxDepth = 1.0f;
        cmd.SetViewport(vp);
        Orange::Rhi::RHIScissor sc{};
        sc.mWidth = ssaoColorWidth; sc.mHeight = ssaoColorHeight;
        cmd.SetScissor(sc);

        // useGtao 选 horizon-based GTAO，否则半球 kernel SSAO（共用 ssaoSet）。
        cmd.BindGraphicsPipeline(ssaoDesc.useGtao ? *gtaoPipeline : *ssaoPipeline);
        cmd.SetDescriptorSet(0, *ssaoSet);
        cmd.Draw(3, 1, 0, 0);
        cmd.EndRendering();

        cmd.TransitionTexture(*ssaoColor,
                              Orange::Rhi::TextureLayout::ColorAttachment,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
        ssaoColorLayoutShaderReadOnly = true;
    }

    // ---- Pass 2：ssao_apply → HDR×AO（乘法 blend）----
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

        cmd.BindGraphicsPipeline(*ssaoApplyPipeline);
        cmd.SetDescriptorSet(0, *ssaoApplySet);
        cmd.Draw(3, 1, 0, 0);
        cmd.EndRendering();

        cmd.TransitionTexture(*hdrColor,
                              Orange::Rhi::TextureLayout::ColorAttachment,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
    }
    return true;
}

}  // namespace Orange::Engine::Render
