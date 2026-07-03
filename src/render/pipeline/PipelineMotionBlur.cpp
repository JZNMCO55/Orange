// Pipeline 的相机运动模糊（motion blur）实现：FindActiveMotionBlurPass /
// EnsureMotionBlurResources / RecordMotionBlurPass。
//
// 两段（与 DoF/SSR 同款）：
//   1. motion_blur gather：hdrColor[ShaderReadOnly] + depth + ubo → motionBlurColor
//      （重投影算屏幕速度，沿速度方向 tap 模糊，RGBA16F）；
//   2. composite：motionBlurColor → hdrColor（复用 dofCompositePipeline，replace）。
// 独立 motionBlurColor 避免 gather 读 HDR 邻域同时写 HDR 同 target 的反馈。
//
// 速度由"当前 / 上一帧（均未 jitter）viewProj"重投影算出（与 TAA 同数学，方向
// 相反——TAA 去抖、motion blur 主动拉糊）。首帧 / resize 后无 prev → hasHistory=0，
// shader 直接返回锐利。

#include "PipelineImpl.h"

#include "orange/engine/render/PostProcessPasses.h"

#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp> // glm::inverse

#include <cstring>

namespace Orange::Engine::Render
{

    const MotionBlurPass* Pipeline::Impl::FindActiveMotionBlurPass() const noexcept
    {
        if (postComponentActive)
        {
            return postMotionBlur.enabled ? &postMotionBlur : nullptr;
        }
        if (postProcessChain == nullptr)
        {
            return nullptr;
        }
        const std::size_t count = postProcessChain->PassCount();
        for (std::size_t i = 0; i < count; ++i)
        {
            const IPostProcessPass* p = postProcessChain->PassAt(i);
            if (const MotionBlurPass* mp = dynamic_cast<const MotionBlurPass*>(p))
            {
                return mp->enabled ? mp : nullptr;
            }
        }
        return nullptr;
    }

    bool Pipeline::Impl::EnsureMotionBlurResources()
    {
        if (renderDevice == nullptr || sceneDepth == nullptr || hdrColor == nullptr || hdrSampler == nullptr || motionBlurPipeline == nullptr || dofCompositePipeline == nullptr || motionBlurLayout == nullptr || bloomLayout == nullptr || motionBlurUbo == nullptr)
        {
            return false;
        }
        if (hdrWidth == 0 || hdrHeight == 0)
        {
            return false;
        }
        auto& rhi = renderDevice->GetRhiDevice();

        // pool：2 set。gatherSet 的 hdr+depth（2 sampler）+ compositeSet 的 mbColor（1）= 3。
        if (!motionBlurPool)
        {
            Orange::Rhi::DescriptorPoolDesc poolDesc{};
            poolDesc.mMaxSets = 2;
            poolDesc.mPoolSizes.push_back({Orange::Rhi::DescriptorType::CombinedImageSampler, 3});
            poolDesc.mPoolSizes.push_back({Orange::Rhi::DescriptorType::UniformBuffer, 1});
            poolDesc.mpDebugName = "orange_engine.motion_blur.pool";
            motionBlurPool       = rhi.CreateDescriptorPool(poolDesc);
            if (!motionBlurPool)
            {
                return false;
            }
        }

        // motionBlurColor：RGBA16F，随 HDR 尺寸重建。
        if (!motionBlurColor || motionBlurColorWidth != hdrWidth || motionBlurColorHeight != hdrHeight)
        {
            renderDevice->WaitIdle();
            motionBlurColor.reset();
            // 不 reset composite set（RHI pool 无 free-bit，reset+realloc 会 resize
            // 累积耗尽 OOM）；置 bound=null 触发下面 UpdateDescriptorSet 重写。
            motionBlurCompositeSetBound = nullptr;

            Orange::Rhi::TextureDesc t{};
            t.mWidth        = hdrWidth;
            t.mHeight       = hdrHeight;
            t.mFormat       = PipelineDetail::kHdrColorFormat;
            t.mDimension    = Orange::Rhi::TextureDimension::Tex2D;
            t.mUsage        = Orange::Rhi::TextureUsage::RenderTarget | Orange::Rhi::TextureUsage::Sampled;
            motionBlurColor = rhi.CreateTexture(t);
            if (!motionBlurColor)
            {
                ORANGE_LOG_ERROR("Pipeline: motionBlurColor CreateTexture 失败 ({}x{})", hdrWidth, hdrHeight);
                return false;
            }
            motionBlurColorWidth                = hdrWidth;
            motionBlurColorHeight               = hdrHeight;
            motionBlurColorLayoutShaderReadOnly = false;
        }

        // motionBlurSet（0=hdrColor, 1=ubo, 2=sceneDepth）：只分配一次，hdr / depth
        // 变化时 UpdateDescriptorSet 重写（不 reset+realloc，避免 resize 累积耗尽 OOM）。
        if (motionBlurSet == nullptr)
        {
            motionBlurSet = rhi.AllocateDescriptorSet(*motionBlurPool, *motionBlurLayout);
            if (!motionBlurSet)
            {
                return false;
            }
            motionBlurSetBoundHdr   = nullptr; // 强制下面 update
            motionBlurSetBoundDepth = nullptr;
        }
        if (motionBlurSetBoundHdr != hdrColor.get() || motionBlurSetBoundDepth != sceneDepth.get())
        {
            Orange::Rhi::DescriptorWrite w[3]{};
            w[0].mBinding             = 0;
            w[0].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            w[0].mImageInfo.mpTexture = hdrColor.get();
            w[0].mImageInfo.mpSampler = hdrSampler.get();
            w[1].mBinding             = 1;
            w[1].mType                = Orange::Rhi::DescriptorType::UniformBuffer;
            w[1].mBufferInfo.mpBuffer = motionBlurUbo.get();
            w[1].mBufferInfo.mOffset  = 0;
            w[1].mBufferInfo.mRange   = sizeof(MotionBlurUboData);
            w[2].mBinding             = 2;
            w[2].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            w[2].mImageInfo.mpTexture = sceneDepth.get();
            w[2].mImageInfo.mpSampler = hdrSampler.get();
            rhi.UpdateDescriptorSet(*motionBlurSet, w, 3);
            motionBlurSetBoundHdr   = hdrColor.get();
            motionBlurSetBoundDepth = sceneDepth.get();
        }

        // motionBlurCompositeSet（0=motionBlurColor，复用 bloomLayout）：同款 allocate-once + update。
        if (motionBlurCompositeSet == nullptr)
        {
            motionBlurCompositeSet = rhi.AllocateDescriptorSet(*motionBlurPool, *bloomLayout);
            if (!motionBlurCompositeSet)
            {
                return false;
            }
            motionBlurCompositeSetBound = nullptr;
        }
        if (motionBlurCompositeSetBound != motionBlurColor.get())
        {
            Orange::Rhi::DescriptorWrite w{};
            w.mBinding             = 0;
            w.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            w.mImageInfo.mpTexture = motionBlurColor.get();
            w.mImageInfo.mpSampler = hdrSampler.get();
            rhi.UpdateDescriptorSet(*motionBlurCompositeSet, &w, 1);
            motionBlurCompositeSetBound = motionBlurColor.get();
        }
        return true;
    }

    bool Pipeline::Impl::RecordMotionBlurPass(const MotionBlurPass& mbDesc,
                                              const glm::mat4&      curViewProj)
    {
        if (offscreenCmd == nullptr || hdrColor == nullptr || sceneDepth == nullptr)
        {
            return false;
        }
        if (!EnsureMotionBlurResources())
        {
            return false;
        }

        // ---- 写 MotionBlurUbo（速度来自未 jitter 的 cur / prev viewProj 重投影）----
        {
            MotionBlurUboData data{};
            data.invCurViewProj = glm::inverse(curViewProj);
            data.prevViewProj   = motionBlurPrevViewProj;
            data.params         = glm::vec4(mbDesc.intensity, mbDesc.maxRadius,
                                            static_cast<float>(mbDesc.sampleCount),
                                    motionBlurHasHistory ? 1.0f : 0.0f);
            void* mapped        = motionBlurUbo->Map();
            if (mapped == nullptr)
            {
                return false;
            }
            std::memcpy(mapped, &data, sizeof(data));
            motionBlurUbo->Unmap();
        }

        auto& cmd = *offscreenCmd;

        // sceneDepth → ShaderReadOnly（与其它 post 共用 flag）。
        if (!sceneDepthLayoutShaderReadOnly)
        {
            cmd.TransitionTexture(*sceneDepth,
                                  Orange::Rhi::TextureLayout::DepthStencilAttachment,
                                  Orange::Rhi::TextureLayout::ShaderReadOnly);
            sceneDepthLayoutShaderReadOnly = true;
        }

        // ---- Pass 1：gather（采 hdrColor[ShaderReadOnly] + depth → motionBlurColor）----
        {
            const auto from = motionBlurColorLayoutShaderReadOnly
                                  ? Orange::Rhi::TextureLayout::ShaderReadOnly
                                  : Orange::Rhi::TextureLayout::Undefined;
            cmd.TransitionTexture(*motionBlurColor, from,
                                  Orange::Rhi::TextureLayout::ColorAttachment);

            Orange::Rhi::ColorAttachment att{};
            att.mpView   = motionBlurColor->GetDefaultView();
            att.mLoadOp  = Orange::Rhi::LoadOp::Clear; // 全屏覆盖，clear 仅防残留
            att.mStoreOp = Orange::Rhi::StoreOp::Store;

            Orange::Rhi::RenderingDesc rd{};
            rd.mRenderArea.mWidth  = motionBlurColorWidth;
            rd.mRenderArea.mHeight = motionBlurColorHeight;
            rd.mColorAttachments.push_back(att);
            cmd.BeginRendering(rd);

            Orange::Rhi::RHIViewport vp{};
            vp.mWidth    = static_cast<float>(motionBlurColorWidth);
            vp.mHeight   = static_cast<float>(motionBlurColorHeight);
            vp.mMinDepth = 0.0f;
            vp.mMaxDepth = 1.0f;
            cmd.SetViewport(vp);
            Orange::Rhi::RHIScissor sc{};
            sc.mWidth  = motionBlurColorWidth;
            sc.mHeight = motionBlurColorHeight;
            cmd.SetScissor(sc);

            cmd.BindGraphicsPipeline(*motionBlurPipeline);
            cmd.SetDescriptorSet(0, *motionBlurSet);
            cmd.Draw(3, 1, 0, 0);
            cmd.EndRendering();

            cmd.TransitionTexture(*motionBlurColor,
                                  Orange::Rhi::TextureLayout::ColorAttachment,
                                  Orange::Rhi::TextureLayout::ShaderReadOnly);
            motionBlurColorLayoutShaderReadOnly = true;
        }

        // ---- Pass 2：composite（motionBlurColor → HDR，replace，复用 dofComposite）----
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
            cmd.SetDescriptorSet(0, *motionBlurCompositeSet);
            cmd.Draw(3, 1, 0, 0);
            cmd.EndRendering();

            cmd.TransitionTexture(*hdrColor,
                                  Orange::Rhi::TextureLayout::ColorAttachment,
                                  Orange::Rhi::TextureLayout::ShaderReadOnly);
        }

        // 本帧未 jitter viewProj 存为下帧 prevViewProj；标记历史有效。
        motionBlurPrevViewProj = curViewProj;
        motionBlurHasHistory   = true;
        return true;
    }

} // namespace Orange::Engine::Render
