// Pipeline 的锐化（sharpen，CAS 式）实现：FindActiveSharpenPass /
// EnsureSharpenResources / RecordSharpenPass。
//
// 两段（与 lens/DoF 同款）：
//   1. sharpen gather：hdrColor[ShaderReadOnly] 邻域 + ubo → sharpColor（RGBA16F）；
//   2. composite：sharpColor → hdrColor（复用 dofCompositePipeline，replace）。
// 独立 sharpColor 避免读 HDR 邻域同时写 HDR 同 target 的反馈。无 depth 依赖。

#include "PipelineImpl.h"

#include "orange/engine/render/PostProcessPasses.h"

#include <glm/vec4.hpp>

#include <cstring>

namespace Orange::Engine::Render
{

    const SharpenPass* Pipeline::Impl::FindActiveSharpenPass() const noexcept
    {
        if (postComponentActive)
        {
            return postSharpen.enabled ? &postSharpen : nullptr;
        }
        if (postProcessChain == nullptr)
        {
            return nullptr;
        }
        const std::size_t count = postProcessChain->PassCount();
        for (std::size_t i = 0; i < count; ++i)
        {
            const IPostProcessPass* p = postProcessChain->PassAt(i);
            if (const SharpenPass* sp = dynamic_cast<const SharpenPass*>(p))
            {
                return sp->enabled ? sp : nullptr;
            }
        }
        return nullptr;
    }

    bool Pipeline::Impl::EnsureSharpenResources()
    {
        if (renderDevice == nullptr || hdrColor == nullptr || hdrSampler == nullptr || sharpenPipeline == nullptr || dofCompositePipeline == nullptr || sharpenLayout == nullptr || bloomLayout == nullptr || sharpenUbo == nullptr)
        {
            return false;
        }
        if (hdrWidth == 0 || hdrHeight == 0)
        {
            return false;
        }
        auto& rhi = renderDevice->GetRhiDevice();

        // pool：2 set。sharpenSet 的 hdr（1 sampler）+ ubo；compositeSet 的 sharpColor（1）。
        if (!sharpenPool)
        {
            Orange::Rhi::DescriptorPoolDesc poolDesc{};
            poolDesc.mMaxSets = 2;
            poolDesc.mPoolSizes.push_back({Orange::Rhi::DescriptorType::CombinedImageSampler, 2});
            poolDesc.mPoolSizes.push_back({Orange::Rhi::DescriptorType::UniformBuffer, 1});
            poolDesc.mpDebugName = "orange_engine.sharpen.pool";
            sharpenPool          = rhi.CreateDescriptorPool(poolDesc);
            if (!sharpenPool)
            {
                return false;
            }
        }

        // sharpColor：RGBA16F，随 HDR 尺寸重建。
        if (!sharpenColor || sharpenColorWidth != hdrWidth || sharpenColorHeight != hdrHeight)
        {
            renderDevice->WaitIdle();
            sharpenColor.reset();
            // 不 reset composite set（RHI pool 无 free-bit，reset+realloc 会 resize
            // 累积耗尽 OOM）；置 bound=null 触发下面 UpdateDescriptorSet 重写。
            sharpenCompositeSetBound = nullptr;

            Orange::Rhi::TextureDesc t{};
            t.mWidth     = hdrWidth;
            t.mHeight    = hdrHeight;
            t.mFormat    = PipelineDetail::kHdrColorFormat;
            t.mDimension = Orange::Rhi::TextureDimension::Tex2D;
            t.mUsage     = Orange::Rhi::TextureUsage::RenderTarget | Orange::Rhi::TextureUsage::Sampled;
            sharpenColor = rhi.CreateTexture(t);
            if (!sharpenColor)
            {
                ORANGE_LOG_ERROR("Pipeline: sharpenColor CreateTexture 失败 ({}x{})", hdrWidth, hdrHeight);
                return false;
            }
            sharpenColorWidth                = hdrWidth;
            sharpenColorHeight               = hdrHeight;
            sharpenColorLayoutShaderReadOnly = false;
        }

        // sharpenSet（0=hdrColor, 1=ubo）：只分配一次，hdr 变化时 UpdateDescriptorSet
        // 重写（不 reset+realloc，避免 resize 累积耗尽 OOM）。
        if (sharpenSet == nullptr)
        {
            sharpenSet = rhi.AllocateDescriptorSet(*sharpenPool, *sharpenLayout);
            if (!sharpenSet)
            {
                return false;
            }
            sharpenSetBoundHdr = nullptr; // 强制下面 update
        }
        if (sharpenSetBoundHdr != hdrColor.get())
        {
            Orange::Rhi::DescriptorWrite w[2]{};
            w[0].mBinding             = 0;
            w[0].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            w[0].mImageInfo.mpTexture = hdrColor.get();
            w[0].mImageInfo.mpSampler = hdrSampler.get();
            w[1].mBinding             = 1;
            w[1].mType                = Orange::Rhi::DescriptorType::UniformBuffer;
            w[1].mBufferInfo.mpBuffer = sharpenUbo.get();
            w[1].mBufferInfo.mOffset  = 0;
            w[1].mBufferInfo.mRange   = sizeof(SharpenUboData);
            rhi.UpdateDescriptorSet(*sharpenSet, w, 2);
            sharpenSetBoundHdr = hdrColor.get();
        }

        // sharpenCompositeSet（0=sharpColor，复用 bloomLayout）：同款 allocate-once + update。
        if (sharpenCompositeSet == nullptr)
        {
            sharpenCompositeSet = rhi.AllocateDescriptorSet(*sharpenPool, *bloomLayout);
            if (!sharpenCompositeSet)
            {
                return false;
            }
            sharpenCompositeSetBound = nullptr;
        }
        if (sharpenCompositeSetBound != sharpenColor.get())
        {
            Orange::Rhi::DescriptorWrite w{};
            w.mBinding             = 0;
            w.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            w.mImageInfo.mpTexture = sharpenColor.get();
            w.mImageInfo.mpSampler = hdrSampler.get();
            rhi.UpdateDescriptorSet(*sharpenCompositeSet, &w, 1);
            sharpenCompositeSetBound = sharpenColor.get();
        }
        return true;
    }

    bool Pipeline::Impl::RecordSharpenPass(const SharpenPass& sharpenDesc)
    {
        if (offscreenCmd == nullptr || hdrColor == nullptr)
        {
            return false;
        }
        if (!EnsureSharpenResources())
        {
            return false;
        }

        // ---- 写 SharpenUbo ----
        {
            SharpenUboData data{};
            data.params  = glm::vec4(sharpenDesc.sharpness, 0.0f, 0.0f, 0.0f);
            void* mapped = sharpenUbo->Map();
            if (mapped == nullptr)
            {
                return false;
            }
            std::memcpy(mapped, &data, sizeof(data));
            sharpenUbo->Unmap();
        }

        auto& cmd = *offscreenCmd;

        // ---- Pass 1：gather（hdrColor[ShaderReadOnly] 邻域 → sharpColor）----
        {
            const auto from = sharpenColorLayoutShaderReadOnly
                                  ? Orange::Rhi::TextureLayout::ShaderReadOnly
                                  : Orange::Rhi::TextureLayout::Undefined;
            cmd.TransitionTexture(*sharpenColor, from,
                                  Orange::Rhi::TextureLayout::ColorAttachment);

            Orange::Rhi::ColorAttachment att{};
            att.mpView   = sharpenColor->GetDefaultView();
            att.mLoadOp  = Orange::Rhi::LoadOp::Clear; // 全屏覆盖，clear 仅防残留
            att.mStoreOp = Orange::Rhi::StoreOp::Store;

            Orange::Rhi::RenderingDesc rd{};
            rd.mRenderArea.mWidth  = sharpenColorWidth;
            rd.mRenderArea.mHeight = sharpenColorHeight;
            rd.mColorAttachments.push_back(att);
            cmd.BeginRendering(rd);

            Orange::Rhi::RHIViewport vp{};
            vp.mWidth    = static_cast<float>(sharpenColorWidth);
            vp.mHeight   = static_cast<float>(sharpenColorHeight);
            vp.mMinDepth = 0.0f;
            vp.mMaxDepth = 1.0f;
            cmd.SetViewport(vp);
            Orange::Rhi::RHIScissor sc{};
            sc.mWidth  = sharpenColorWidth;
            sc.mHeight = sharpenColorHeight;
            cmd.SetScissor(sc);

            cmd.BindGraphicsPipeline(*sharpenPipeline);
            cmd.SetDescriptorSet(0, *sharpenSet);
            cmd.Draw(3, 1, 0, 0);
            cmd.EndRendering();

            cmd.TransitionTexture(*sharpenColor,
                                  Orange::Rhi::TextureLayout::ColorAttachment,
                                  Orange::Rhi::TextureLayout::ShaderReadOnly);
            sharpenColorLayoutShaderReadOnly = true;
        }

        // ---- Pass 2：composite（sharpColor → HDR，replace，复用 dofComposite）----
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
            cmd.SetDescriptorSet(0, *sharpenCompositeSet);
            cmd.Draw(3, 1, 0, 0);
            cmd.EndRendering();

            cmd.TransitionTexture(*hdrColor,
                                  Orange::Rhi::TextureLayout::ColorAttachment,
                                  Orange::Rhi::TextureLayout::ShaderReadOnly);
        }
        return true;
    }

} // namespace Orange::Engine::Render
