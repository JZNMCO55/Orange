// Pipeline 的景深（DoF）实现：FindActiveDofPass / EnsureDofResources /
// RecordDofPass。
//
// 两段（与 SSAO/SSR 同款）：
//   1. dof gather：hdrColor[ShaderReadOnly] + depth + ubo → dofColor（CoC 圆盘
//      模糊，RGBA16F）；
//   2. dof_composite：dofColor → hdrColor（replace blend，全屏覆盖）。
// 独立 dofColor 避免 gather 读 HDR 邻域同时写 HDR 同 target 的反馈。

#include "PipelineImpl.h"

#include "orange/engine/render/PostProcessPasses.h"

#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp> // glm::inverse

#include <cstring>

namespace Orange::Engine::Render
{

    const DofPass* Pipeline::Impl::FindActiveDofPass() const noexcept
    {
        if (postComponentActive)
        {
            return postDof.enabled ? &postDof : nullptr;
        }
        if (postProcessChain == nullptr)
        {
            return nullptr;
        }
        const std::size_t count = postProcessChain->PassCount();
        for (std::size_t i = 0; i < count; ++i)
        {
            const IPostProcessPass* p = postProcessChain->PassAt(i);
            if (const DofPass* dp = dynamic_cast<const DofPass*>(p))
            {
                return dp->enabled ? dp : nullptr;
            }
        }
        return nullptr;
    }

    bool Pipeline::Impl::EnsureDofResources()
    {
        if (renderDevice == nullptr || sceneDepth == nullptr || hdrColor == nullptr || hdrSampler == nullptr || dofPipeline == nullptr || dofCompositePipeline == nullptr || dofLayout == nullptr || bloomLayout == nullptr || dofUbo == nullptr)
        {
            return false;
        }
        if (hdrWidth == 0 || hdrHeight == 0)
        {
            return false;
        }
        auto& rhi = renderDevice->GetRhiDevice();

        // pool：2 set。dofSet 的 hdr+depth（2 sampler）+ dofCompositeSet 的 dofColor（1）= 3。
        if (!dofPool)
        {
            Orange::Rhi::DescriptorPoolDesc poolDesc{};
            poolDesc.mMaxSets = 2;
            poolDesc.mPoolSizes.push_back({Orange::Rhi::DescriptorType::CombinedImageSampler, 3});
            poolDesc.mPoolSizes.push_back({Orange::Rhi::DescriptorType::UniformBuffer, 1});
            poolDesc.mpDebugName = "orange_engine.dof.pool";
            dofPool              = rhi.CreateDescriptorPool(poolDesc);
            if (!dofPool)
            {
                return false;
            }
        }

        // dofColor：RGBA16F，随 HDR 尺寸重建。
        if (!dofColor || dofColorWidth != hdrWidth || dofColorHeight != hdrHeight)
        {
            renderDevice->WaitIdle();
            dofColor.reset();
            // 不 reset composite set（RHI pool 无 free-bit，reset+realloc 会 resize
            // 累积耗尽 OOM）；置 bound=null 触发下面 UpdateDescriptorSet 重写。
            dofCompositeSetBound = nullptr;

            Orange::Rhi::TextureDesc t{};
            t.mWidth     = hdrWidth;
            t.mHeight    = hdrHeight;
            t.mFormat    = PipelineDetail::kHdrColorFormat;
            t.mDimension = Orange::Rhi::TextureDimension::Tex2D;
            t.mUsage     = Orange::Rhi::TextureUsage::RenderTarget | Orange::Rhi::TextureUsage::Sampled;
            dofColor     = rhi.CreateTexture(t);
            if (!dofColor)
            {
                ORANGE_LOG_ERROR("Pipeline: dofColor CreateTexture 失败 ({}x{})", hdrWidth, hdrHeight);
                return false;
            }
            dofColorWidth                = hdrWidth;
            dofColorHeight               = hdrHeight;
            dofColorLayoutShaderReadOnly = false;
        }

        // dofSet（0=hdrColor, 1=ubo, 2=sceneDepth）：只分配一次，hdr / depth 变化时
        // UpdateDescriptorSet 重写（不 reset+realloc，避免 resize 累积耗尽 OOM）。
        if (dofSet == nullptr)
        {
            dofSet = rhi.AllocateDescriptorSet(*dofPool, *dofLayout);
            if (!dofSet)
            {
                return false;
            }
            dofSetBoundHdr   = nullptr; // 强制下面 update
            dofSetBoundDepth = nullptr;
        }
        if (dofSetBoundHdr != hdrColor.get() || dofSetBoundDepth != sceneDepth.get())
        {
            Orange::Rhi::DescriptorWrite w[3]{};
            w[0].mBinding             = 0;
            w[0].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            w[0].mImageInfo.mpTexture = hdrColor.get();
            w[0].mImageInfo.mpSampler = hdrSampler.get();
            w[1].mBinding             = 1;
            w[1].mType                = Orange::Rhi::DescriptorType::UniformBuffer;
            w[1].mBufferInfo.mpBuffer = dofUbo.get();
            w[1].mBufferInfo.mOffset  = 0;
            w[1].mBufferInfo.mRange   = sizeof(DofUboData);
            w[2].mBinding             = 2;
            w[2].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            w[2].mImageInfo.mpTexture = sceneDepth.get();
            w[2].mImageInfo.mpSampler = hdrSampler.get();
            rhi.UpdateDescriptorSet(*dofSet, w, 3);
            dofSetBoundHdr   = hdrColor.get();
            dofSetBoundDepth = sceneDepth.get();
        }

        // dofCompositeSet（0=dofColor，复用 bloomLayout）：同款 allocate-once + update。
        if (dofCompositeSet == nullptr)
        {
            dofCompositeSet = rhi.AllocateDescriptorSet(*dofPool, *bloomLayout);
            if (!dofCompositeSet)
            {
                return false;
            }
            dofCompositeSetBound = nullptr;
        }
        if (dofCompositeSetBound != dofColor.get())
        {
            Orange::Rhi::DescriptorWrite w{};
            w.mBinding             = 0;
            w.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            w.mImageInfo.mpTexture = dofColor.get();
            w.mImageInfo.mpSampler = hdrSampler.get();
            rhi.UpdateDescriptorSet(*dofCompositeSet, &w, 1);
            dofCompositeSetBound = dofColor.get();
        }
        return true;
    }

    bool Pipeline::Impl::RecordDofPass(const DofPass& dofDesc, const glm::mat4& proj)
    {
        if (offscreenCmd == nullptr || hdrColor == nullptr || sceneDepth == nullptr)
        {
            return false;
        }
        if (!EnsureDofResources())
        {
            return false;
        }

        // ---- 写 DofUbo ----
        {
            DofUboData data{};
            data.invProj = glm::inverse(proj);
            data.params  = glm::vec4(dofDesc.focusDistance, dofDesc.focusRange,
                                     dofDesc.maxCoCRadius, 0.0f);
            void* mapped = dofUbo->Map();
            if (mapped == nullptr)
            {
                return false;
            }
            std::memcpy(mapped, &data, sizeof(data));
            dofUbo->Unmap();
        }

        auto& cmd = *offscreenCmd;

        // sceneDepth → ShaderReadOnly（与 SSAO/SSR/contact 共用 flag）。
        if (!sceneDepthLayoutShaderReadOnly)
        {
            cmd.TransitionTexture(*sceneDepth,
                                  Orange::Rhi::TextureLayout::DepthStencilAttachment,
                                  Orange::Rhi::TextureLayout::ShaderReadOnly);
            sceneDepthLayoutShaderReadOnly = true;
        }

        // ---- Pass 1：dof gather（采 hdrColor[ShaderReadOnly] + depth → dofColor）----
        {
            const auto from = dofColorLayoutShaderReadOnly
                                  ? Orange::Rhi::TextureLayout::ShaderReadOnly
                                  : Orange::Rhi::TextureLayout::Undefined;
            cmd.TransitionTexture(*dofColor, from,
                                  Orange::Rhi::TextureLayout::ColorAttachment);

            Orange::Rhi::ColorAttachment att{};
            att.mpView   = dofColor->GetDefaultView();
            att.mLoadOp  = Orange::Rhi::LoadOp::Clear; // 全屏覆盖，clear 仅防残留
            att.mStoreOp = Orange::Rhi::StoreOp::Store;

            Orange::Rhi::RenderingDesc rd{};
            rd.mRenderArea.mWidth  = dofColorWidth;
            rd.mRenderArea.mHeight = dofColorHeight;
            rd.mColorAttachments.push_back(att);
            cmd.BeginRendering(rd);

            Orange::Rhi::RHIViewport vp{};
            vp.mWidth    = static_cast<float>(dofColorWidth);
            vp.mHeight   = static_cast<float>(dofColorHeight);
            vp.mMinDepth = 0.0f;
            vp.mMaxDepth = 1.0f;
            cmd.SetViewport(vp);
            Orange::Rhi::RHIScissor sc{};
            sc.mWidth  = dofColorWidth;
            sc.mHeight = dofColorHeight;
            cmd.SetScissor(sc);

            cmd.BindGraphicsPipeline(*dofPipeline);
            cmd.SetDescriptorSet(0, *dofSet);
            cmd.Draw(3, 1, 0, 0);
            cmd.EndRendering();

            cmd.TransitionTexture(*dofColor,
                                  Orange::Rhi::TextureLayout::ColorAttachment,
                                  Orange::Rhi::TextureLayout::ShaderReadOnly);
            dofColorLayoutShaderReadOnly = true;
        }

        // ---- Pass 2：dof_composite（dofColor → HDR，replace）----
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
            cmd.SetDescriptorSet(0, *dofCompositeSet);
            cmd.Draw(3, 1, 0, 0);
            cmd.EndRendering();

            cmd.TransitionTexture(*hdrColor,
                                  Orange::Rhi::TextureLayout::ColorAttachment,
                                  Orange::Rhi::TextureLayout::ShaderReadOnly);
        }
        return true;
    }

} // namespace Orange::Engine::Render
