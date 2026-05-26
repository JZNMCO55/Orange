// Pipeline 的色彩分级实现：FindActiveColorGradePass / EnsureColorGradeResources
// / RecordColorGradePass。
//
// 两段：grade（hdrColor + ubo → gradeColor，曝光/白平衡/对比/饱和）+ composite
// （gradeColor replace 回 hdrColor，复用 dofCompositePipeline）。白平衡的 per-
// channel 乘子由 host 从 temperature/tint 算出喂 ubo。

#include "PipelineImpl.h"

#include "orange/engine/render/PostProcessPasses.h"

#include <glm/vec3.hpp>
#include <glm/common.hpp>   // glm::max

#include <cstring>

namespace Orange::Engine::Render
{

const ColorGradePass* Pipeline::Impl::FindActiveColorGradePass() const noexcept
{
    if (postProcessChain == nullptr)
    {
        return nullptr;
    }
    const std::size_t count = postProcessChain->PassCount();
    for (std::size_t i = 0; i < count; ++i)
    {
        const IPostProcessPass* p = postProcessChain->PassAt(i);
        if (const ColorGradePass* gp = dynamic_cast<const ColorGradePass*>(p))
        {
            return gp->enabled ? gp : nullptr;
        }
    }
    return nullptr;
}

bool Pipeline::Impl::EnsureColorGradeResources()
{
    if (renderDevice == nullptr || hdrColor == nullptr || hdrSampler == nullptr
        || colorGradePipeline == nullptr || dofCompositePipeline == nullptr
        || colorGradeLayout == nullptr || bloomLayout == nullptr || colorGradeUbo == nullptr)
    {
        return false;
    }
    if (hdrWidth == 0 || hdrHeight == 0)
    {
        return false;
    }
    auto& rhi = renderDevice->GetRhiDevice();

    // pool：2 set。gradeSet 的 hdr（1 sampler）+ ubo；gradeCompositeSet 的 gradeColor（1）。
    if (!colorGradePool)
    {
        Orange::Rhi::DescriptorPoolDesc poolDesc{};
        poolDesc.mMaxSets = 2;
        poolDesc.mPoolSizes.push_back({Orange::Rhi::DescriptorType::CombinedImageSampler, 2});
        poolDesc.mPoolSizes.push_back({Orange::Rhi::DescriptorType::UniformBuffer, 1});
        poolDesc.mpDebugName = "orange_engine.color_grade.pool";
        colorGradePool = rhi.CreateDescriptorPool(poolDesc);
        if (!colorGradePool) { return false; }
    }

    // gradeColor：RGBA16F，随 HDR 尺寸重建。
    if (!gradeColor || gradeColorWidth != hdrWidth || gradeColorHeight != hdrHeight)
    {
        renderDevice->WaitIdle();
        gradeColor.reset();
        gradeCompositeSet.reset();
        gradeCompositeSetBound = nullptr;

        Orange::Rhi::TextureDesc t{};
        t.mWidth     = hdrWidth;
        t.mHeight    = hdrHeight;
        t.mFormat    = PipelineDetail::kHdrColorFormat;
        t.mDimension = Orange::Rhi::TextureDimension::Tex2D;
        t.mUsage     = Orange::Rhi::TextureUsage::RenderTarget
                     | Orange::Rhi::TextureUsage::Sampled;
        gradeColor = rhi.CreateTexture(t);
        if (!gradeColor)
        {
            ORANGE_LOG_ERROR("Pipeline: gradeColor CreateTexture 失败 ({}x{})", hdrWidth, hdrHeight);
            return false;
        }
        gradeColorWidth                = hdrWidth;
        gradeColorHeight               = hdrHeight;
        gradeColorLayoutShaderReadOnly = false;
    }

    // colorGradeSet（0=hdrColor, 1=ubo）：hdr 重建后重绑。
    if (colorGradeSet == nullptr || colorGradeSetBoundHdr != hdrColor.get())
    {
        colorGradeSet.reset();
        auto set = rhi.AllocateDescriptorSet(*colorGradePool, *colorGradeLayout);
        if (!set) { return false; }
        Orange::Rhi::DescriptorWrite w[2]{};
        w[0].mBinding             = 0;
        w[0].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
        w[0].mImageInfo.mpTexture = hdrColor.get();
        w[0].mImageInfo.mpSampler = hdrSampler.get();
        w[1].mBinding             = 1;
        w[1].mType                = Orange::Rhi::DescriptorType::UniformBuffer;
        w[1].mBufferInfo.mpBuffer = colorGradeUbo.get();
        w[1].mBufferInfo.mOffset  = 0;
        w[1].mBufferInfo.mRange   = sizeof(GradeUboData);
        rhi.UpdateDescriptorSet(*set, w, 2);
        colorGradeSet         = std::move(set);
        colorGradeSetBoundHdr = hdrColor.get();
    }

    // gradeCompositeSet（0=gradeColor，复用 bloomLayout）。
    if (gradeCompositeSet == nullptr || gradeCompositeSetBound != gradeColor.get())
    {
        gradeCompositeSet.reset();
        auto set = rhi.AllocateDescriptorSet(*colorGradePool, *bloomLayout);
        if (!set) { return false; }
        Orange::Rhi::DescriptorWrite w{};
        w.mBinding             = 0;
        w.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
        w.mImageInfo.mpTexture = gradeColor.get();
        w.mImageInfo.mpSampler = hdrSampler.get();
        rhi.UpdateDescriptorSet(*set, &w, 1);
        gradeCompositeSet      = std::move(set);
        gradeCompositeSetBound = gradeColor.get();
    }
    return true;
}

bool Pipeline::Impl::RecordColorGradePass(const ColorGradePass& gradeDesc)
{
    if (offscreenCmd == nullptr || hdrColor == nullptr)
    {
        return false;
    }
    if (!EnsureColorGradeResources())
    {
        return false;
    }

    // ---- 写 GradeUbo（白平衡 RGB 乘子 host 端从 temperature/tint 算）----
    {
        const float t = gradeDesc.temperature;
        const float g = gradeDesc.tint;
        // 暖(+t)→ +R -B；品红(+g)→ +R +B -G。系数取温和量级。
        glm::vec3 wb(1.0f + 0.10f * t + 0.05f * g,
                     1.0f - 0.10f * g,
                     1.0f - 0.10f * t + 0.05f * g);
        wb = glm::max(wb, glm::vec3(0.0f));

        GradeUboData data{};
        data.params       = glm::vec4(gradeDesc.exposure, gradeDesc.contrast,
                                      gradeDesc.saturation, 0.0f);
        data.whiteBalance = glm::vec4(wb, 0.0f);
        void* mapped = colorGradeUbo->Map();
        if (mapped == nullptr) { return false; }
        std::memcpy(mapped, &data, sizeof(data));
        colorGradeUbo->Unmap();
    }

    auto& cmd = *offscreenCmd;

    // ---- Pass 1：grade（hdrColor[ShaderReadOnly] → gradeColor）----
    {
        const auto from = gradeColorLayoutShaderReadOnly
            ? Orange::Rhi::TextureLayout::ShaderReadOnly
            : Orange::Rhi::TextureLayout::Undefined;
        cmd.TransitionTexture(*gradeColor, from,
                              Orange::Rhi::TextureLayout::ColorAttachment);

        Orange::Rhi::ColorAttachment att{};
        att.mpView   = gradeColor->GetDefaultView();
        att.mLoadOp  = Orange::Rhi::LoadOp::Clear;
        att.mStoreOp = Orange::Rhi::StoreOp::Store;

        Orange::Rhi::RenderingDesc rd{};
        rd.mRenderArea.mWidth  = gradeColorWidth;
        rd.mRenderArea.mHeight = gradeColorHeight;
        rd.mColorAttachments.push_back(att);
        cmd.BeginRendering(rd);

        Orange::Rhi::RHIViewport vp{};
        vp.mWidth = static_cast<float>(gradeColorWidth);
        vp.mHeight = static_cast<float>(gradeColorHeight);
        vp.mMinDepth = 0.0f; vp.mMaxDepth = 1.0f;
        cmd.SetViewport(vp);
        Orange::Rhi::RHIScissor sc{};
        sc.mWidth = gradeColorWidth; sc.mHeight = gradeColorHeight;
        cmd.SetScissor(sc);

        cmd.BindGraphicsPipeline(*colorGradePipeline);
        cmd.SetDescriptorSet(0, *colorGradeSet);
        cmd.Draw(3, 1, 0, 0);
        cmd.EndRendering();

        cmd.TransitionTexture(*gradeColor,
                              Orange::Rhi::TextureLayout::ColorAttachment,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
        gradeColorLayoutShaderReadOnly = true;
    }

    // ---- Pass 2：composite（gradeColor → HDR，复用 dofCompositePipeline）----
    {
        cmd.TransitionTexture(*hdrColor,
                              Orange::Rhi::TextureLayout::ShaderReadOnly,
                              Orange::Rhi::TextureLayout::ColorAttachment);

        Orange::Rhi::ColorAttachment att{};
        att.mpView   = hdrColor->GetDefaultView();
        att.mLoadOp  = Orange::Rhi::LoadOp::Clear;
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

        cmd.BindGraphicsPipeline(*dofCompositePipeline);
        cmd.SetDescriptorSet(0, *gradeCompositeSet);
        cmd.Draw(3, 1, 0, 0);
        cmd.EndRendering();

        cmd.TransitionTexture(*hdrColor,
                              Orange::Rhi::TextureLayout::ColorAttachment,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
    }
    return true;
}

}  // namespace Orange::Engine::Render
