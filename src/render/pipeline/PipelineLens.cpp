// Pipeline 的镜头效果（lens：色散 + 暗角）实现：FindActiveLensPass /
// EnsureLensResources / RecordLensPass。
//
// 两段（与 DoF/色彩分级同款）：
//   1. lens gather：hdrColor[ShaderReadOnly] + ubo → lensColor（CA 沿径向偏移
//      采样 + 暗角，RGBA16F）；
//   2. composite：lensColor → hdrColor（复用 dofCompositePipeline，replace）。
// 独立 lensColor 避免 CA 读 HDR 邻域同时写 HDR 同 target 的反馈。无 depth 依赖。

#include "PipelineImpl.h"

#include "orange/engine/render/PostProcessPasses.h"

#include <glm/vec4.hpp>

#include <cstring>

namespace Orange::Engine::Render
{

    const LensPass* Pipeline::Impl::FindActiveLensPass() const noexcept
    {
        if (postComponentActive)
        {
            return postLens.enabled ? &postLens : nullptr;
        }
        if (postProcessChain == nullptr)
        {
            return nullptr;
        }
        const std::size_t count = postProcessChain->PassCount();
        for (std::size_t i = 0; i < count; ++i)
        {
            const IPostProcessPass* p = postProcessChain->PassAt(i);
            if (const LensPass* lp = dynamic_cast<const LensPass*>(p))
            {
                return lp->enabled ? lp : nullptr;
            }
        }
        return nullptr;
    }

    bool Pipeline::Impl::EnsureLensResources()
    {
        if (renderDevice == nullptr || hdrColor == nullptr || hdrSampler == nullptr || lensPipeline == nullptr || dofCompositePipeline == nullptr || lensLayout == nullptr || bloomLayout == nullptr || lensUbo == nullptr)
        {
            return false;
        }
        if (hdrWidth == 0 || hdrHeight == 0)
        {
            return false;
        }
        auto& rhi = renderDevice->GetRhiDevice();

        // pool：2 set。lensSet 的 hdr（1 sampler）+ ubo；lensCompositeSet 的 lensColor（1）。
        if (!lensPool)
        {
            Orange::Rhi::DescriptorPoolDesc poolDesc{};
            poolDesc.mMaxSets = 2;
            poolDesc.mPoolSizes.push_back({Orange::Rhi::DescriptorType::CombinedImageSampler, 2});
            poolDesc.mPoolSizes.push_back({Orange::Rhi::DescriptorType::UniformBuffer, 1});
            poolDesc.mpDebugName = "orange_engine.lens.pool";
            lensPool             = rhi.CreateDescriptorPool(poolDesc);
            if (!lensPool)
            {
                return false;
            }
        }

        // lensColor：RGBA16F，随 HDR 尺寸重建。
        if (!lensColor || lensColorWidth != hdrWidth || lensColorHeight != hdrHeight)
        {
            renderDevice->WaitIdle();
            lensColor.reset();
            // 不 reset composite set（RHI pool 无 free-bit，reset+realloc 会 resize
            // 累积耗尽 OOM）；置 bound=null 触发下面 UpdateDescriptorSet 重写。
            lensCompositeSetBound = nullptr;

            Orange::Rhi::TextureDesc t{};
            t.mWidth     = hdrWidth;
            t.mHeight    = hdrHeight;
            t.mFormat    = PipelineDetail::kHdrColorFormat;
            t.mDimension = Orange::Rhi::TextureDimension::Tex2D;
            t.mUsage     = Orange::Rhi::TextureUsage::RenderTarget | Orange::Rhi::TextureUsage::Sampled;
            lensColor    = rhi.CreateTexture(t);
            if (!lensColor)
            {
                ORANGE_LOG_ERROR("Pipeline: lensColor CreateTexture 失败 ({}x{})", hdrWidth, hdrHeight);
                return false;
            }
            lensColorWidth                = hdrWidth;
            lensColorHeight               = hdrHeight;
            lensColorLayoutShaderReadOnly = false;
        }

        // lensSet（0=hdrColor, 1=ubo）：只分配一次，hdr 变化时 UpdateDescriptorSet
        // 重写（不 reset+realloc，避免 resize 累积耗尽 OOM）。
        if (lensSet == nullptr)
        {
            lensSet = rhi.AllocateDescriptorSet(*lensPool, *lensLayout);
            if (!lensSet)
            {
                return false;
            }
            lensSetBoundHdr = nullptr; // 强制下面 update
        }
        if (lensSetBoundHdr != hdrColor.get())
        {
            Orange::Rhi::DescriptorWrite w[2]{};
            w[0].mBinding             = 0;
            w[0].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            w[0].mImageInfo.mpTexture = hdrColor.get();
            w[0].mImageInfo.mpSampler = hdrSampler.get();
            w[1].mBinding             = 1;
            w[1].mType                = Orange::Rhi::DescriptorType::UniformBuffer;
            w[1].mBufferInfo.mpBuffer = lensUbo.get();
            w[1].mBufferInfo.mOffset  = 0;
            w[1].mBufferInfo.mRange   = sizeof(LensUboData);
            rhi.UpdateDescriptorSet(*lensSet, w, 2);
            lensSetBoundHdr = hdrColor.get();
        }

        // lensCompositeSet（0=lensColor，复用 bloomLayout）：同款 allocate-once + update。
        if (lensCompositeSet == nullptr)
        {
            lensCompositeSet = rhi.AllocateDescriptorSet(*lensPool, *bloomLayout);
            if (!lensCompositeSet)
            {
                return false;
            }
            lensCompositeSetBound = nullptr;
        }
        if (lensCompositeSetBound != lensColor.get())
        {
            Orange::Rhi::DescriptorWrite w{};
            w.mBinding             = 0;
            w.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            w.mImageInfo.mpTexture = lensColor.get();
            w.mImageInfo.mpSampler = hdrSampler.get();
            rhi.UpdateDescriptorSet(*lensCompositeSet, &w, 1);
            lensCompositeSetBound = lensColor.get();
        }
        return true;
    }

    bool Pipeline::Impl::RecordLensPass(const LensPass& lensDesc)
    {
        if (offscreenCmd == nullptr || hdrColor == nullptr)
        {
            return false;
        }
        if (!EnsureLensResources())
        {
            return false;
        }

        // ---- 写 LensUbo ----
        {
            LensUboData data{};
            data.params  = glm::vec4(lensDesc.chromaticAberration, lensDesc.vignetteIntensity,
                                     lensDesc.vignetteSmoothness, 0.0f);
            void* mapped = lensUbo->Map();
            if (mapped == nullptr)
            {
                return false;
            }
            std::memcpy(mapped, &data, sizeof(data));
            lensUbo->Unmap();
        }

        auto& cmd = *offscreenCmd;

        // ---- Pass 1：gather（hdrColor[ShaderReadOnly] → lensColor）----
        {
            const auto from = lensColorLayoutShaderReadOnly
                                  ? Orange::Rhi::TextureLayout::ShaderReadOnly
                                  : Orange::Rhi::TextureLayout::Undefined;
            cmd.TransitionTexture(*lensColor, from,
                                  Orange::Rhi::TextureLayout::ColorAttachment);

            Orange::Rhi::ColorAttachment att{};
            att.mpView   = lensColor->GetDefaultView();
            att.mLoadOp  = Orange::Rhi::LoadOp::Clear; // 全屏覆盖，clear 仅防残留
            att.mStoreOp = Orange::Rhi::StoreOp::Store;

            Orange::Rhi::RenderingDesc rd{};
            rd.mRenderArea.mWidth  = lensColorWidth;
            rd.mRenderArea.mHeight = lensColorHeight;
            rd.mColorAttachments.push_back(att);
            cmd.BeginRendering(rd);

            Orange::Rhi::RHIViewport vp{};
            vp.mWidth    = static_cast<float>(lensColorWidth);
            vp.mHeight   = static_cast<float>(lensColorHeight);
            vp.mMinDepth = 0.0f;
            vp.mMaxDepth = 1.0f;
            cmd.SetViewport(vp);
            Orange::Rhi::RHIScissor sc{};
            sc.mWidth  = lensColorWidth;
            sc.mHeight = lensColorHeight;
            cmd.SetScissor(sc);

            cmd.BindGraphicsPipeline(*lensPipeline);
            cmd.SetDescriptorSet(0, *lensSet);
            cmd.Draw(3, 1, 0, 0);
            cmd.EndRendering();

            cmd.TransitionTexture(*lensColor,
                                  Orange::Rhi::TextureLayout::ColorAttachment,
                                  Orange::Rhi::TextureLayout::ShaderReadOnly);
            lensColorLayoutShaderReadOnly = true;
        }

        // ---- Pass 2：composite（lensColor → HDR，replace，复用 dofComposite）----
        {
            cmd.TransitionTexture(*hdrColor,
                                  Orange::Rhi::TextureLayout::ShaderReadOnly,
                                  Orange::Rhi::TextureLayout::ColorAttachment);

            Orange::Rhi::ColorAttachment att{};
            att.mpView   = hdrColor->GetDefaultView();
            att.mLoadOp  = Orange::Rhi::LoadOp::Clear; // composite 全屏覆盖
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

            cmd.BindGraphicsPipeline(*dofCompositePipeline);
            cmd.SetDescriptorSet(0, *lensCompositeSet);
            cmd.Draw(3, 1, 0, 0);
            cmd.EndRendering();

            cmd.TransitionTexture(*hdrColor,
                                  Orange::Rhi::TextureLayout::ColorAttachment,
                                  Orange::Rhi::TextureLayout::ShaderReadOnly);
        }
        return true;
    }

} // namespace Orange::Engine::Render
