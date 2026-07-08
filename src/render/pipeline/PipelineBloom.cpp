// Pipeline 的 bloom 系列实现：ReleaseBloomResources / EnsureBloomResources /
// RecordBloomChain。6 张 RGBA16F mip + 6 downsample set + 5 upsample set +
// 1 combine set；按 chain 里 BloomPass.threshold / intensity 走 bright-pass
// + additive upsample。

#include "PipelineImpl.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Orange::Engine::Render
{

    using namespace PipelineDetail;

    void Pipeline::Impl::ReleaseBloomResources()
    {
        if (renderDevice)
        {
            renderDevice->WaitIdle();
        }
        bloomCombineSet.reset();
        for (auto& s : bloomUpsampleSets)
            s.reset();
        for (auto& s : bloomDownsampleSets)
            s.reset();
        bloomPool.reset();
        for (auto& mip : bloomMips)
        {
            mip.texture.reset();
            mip.width                = 0;
            mip.height               = 0;
            mip.layoutShaderReadOnly = false;
        }
        bloomMipsReady = false;
    }

    bool Pipeline::Impl::EnsureBloomResources()
    {
        if (renderDevice == nullptr || hdrColor == nullptr || hdrWidth == 0 || hdrHeight == 0)
        {
            return false;
        }

        // 计算第一张 mip 的尺寸（HDR/2，向下取整 + 至少 1 像素）；后续按 /2
        // 依次推到 mip[5]。
        const std::uint32_t expectedMip0W = std::max<std::uint32_t>(hdrWidth / 2, 1);
        const std::uint32_t expectedMip0H = std::max<std::uint32_t>(hdrHeight / 2, 1);

        if (bloomMipsReady && bloomMips[0].width == expectedMip0W && bloomMips[0].height == expectedMip0H)
        {
            return true; // 已建好且尺寸一致
        }

        // 任一条件不满足都重建（HDR resize / chain 切到 BloomPass 的初次激活）。
        ReleaseBloomResources();

        auto& rhi = renderDevice->GetRhiDevice();

        // 1. 创建 6 张 RGBA16F mip texture
        std::uint32_t w = expectedMip0W;
        std::uint32_t h = expectedMip0H;
        for (std::size_t i = 0; i < kBloomMipCount; ++i)
        {
            Orange::Rhi::TextureDesc t{};
            t.mWidth  = w;
            t.mHeight = h;
            t.mFormat = kHdrColorFormat;
            t.mUsage  = Orange::Rhi::TextureUsage::RenderTarget | Orange::Rhi::TextureUsage::Sampled;
            auto tex  = rhi.CreateTexture(t);
            if (!tex)
            {
                ORANGE_LOG_ERROR("Pipeline: bloom mip {} CreateTexture 失败 ({}x{})",
                                 i, w, h);
                ReleaseBloomResources();
                return false;
            }
            bloomMips[i].texture              = std::move(tex);
            bloomMips[i].width                = w;
            bloomMips[i].height               = h;
            bloomMips[i].layoutShaderReadOnly = false;

            w = std::max<std::uint32_t>(w / 2, 1);
            h = std::max<std::uint32_t>(h / 2, 1);
        }

        // 2. Pool —— 6 down + 5 up + 1 combine = 12 sets，13 个 CombinedImageSampler。
        Orange::Rhi::DescriptorPoolDesc poolDesc{};
        poolDesc.mMaxSets    = static_cast<std::uint32_t>(kBloomMipCount * 2); // 12
        poolDesc.mPoolSizes  = {{Orange::Rhi::DescriptorType::CombinedImageSampler,
                                 static_cast<std::uint32_t>(kBloomMipCount * 2 + 1)}}; // 13
        poolDesc.mpDebugName = "orange_engine.bloom.pool";
        bloomPool            = rhi.CreateDescriptorPool(poolDesc);
        if (!bloomPool)
        {
            ORANGE_LOG_ERROR("Pipeline: bloom DescriptorPool 创建失败");
            ReleaseBloomResources();
            return false;
        }

        // 3. 6 个 downsample set —— set[0] 采样 HDR；set[i>=1] 采样 mip[i-1]
        for (std::size_t i = 0; i < kBloomMipCount; ++i)
        {
            auto set = rhi.AllocateDescriptorSet(*bloomPool, *bloomLayout);
            if (!set)
            {
                ORANGE_LOG_ERROR("Pipeline: bloom downsample set {} 分配失败", i);
                ReleaseBloomResources();
                return false;
            }
            Orange::Rhi::DescriptorWrite write{};
            write.mBinding             = 0;
            write.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            write.mImageInfo.mpTexture = (i == 0) ? hdrColor.get()
                                                  : bloomMips[i - 1].texture.get();
            write.mImageInfo.mpSampler = hdrSampler.get();
            rhi.UpdateDescriptorSet(*set, &write, 1);
            bloomDownsampleSets[i] = std::move(set);
        }

        // 4. 5 个 upsample set —— set[i] 采样 mip[i+1]，目标是 mip[i] (i = 0..4)
        for (std::size_t i = 0; i < kBloomMipCount - 1; ++i)
        {
            auto set = rhi.AllocateDescriptorSet(*bloomPool, *bloomLayout);
            if (!set)
            {
                ORANGE_LOG_ERROR("Pipeline: bloom upsample set {} 分配失败", i);
                ReleaseBloomResources();
                return false;
            }
            Orange::Rhi::DescriptorWrite write{};
            write.mBinding             = 0;
            write.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            write.mImageInfo.mpTexture = bloomMips[i + 1].texture.get();
            write.mImageInfo.mpSampler = hdrSampler.get();
            rhi.UpdateDescriptorSet(*set, &write, 1);
            bloomUpsampleSets[i] = std::move(set);
        }

        // 5. combine set —— binding 0 = HDR，binding 1 = bloom_mip[0]
        bloomCombineSet = rhi.AllocateDescriptorSet(*bloomPool, *combineLayout);
        if (!bloomCombineSet)
        {
            ORANGE_LOG_ERROR("Pipeline: bloom combine set 分配失败");
            ReleaseBloomResources();
            return false;
        }
        {
            std::array<Orange::Rhi::DescriptorWrite, 2> writes{};
            writes[0].mBinding             = 0;
            writes[0].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            writes[0].mImageInfo.mpTexture = hdrColor.get();
            writes[0].mImageInfo.mpSampler = hdrSampler.get();

            writes[1].mBinding             = 1;
            writes[1].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            writes[1].mImageInfo.mpTexture = bloomMips[0].texture.get();
            writes[1].mImageInfo.mpSampler = hdrSampler.get();

            rhi.UpdateDescriptorSet(*bloomCombineSet, writes.data(),
                                    static_cast<std::uint32_t>(writes.size()));
        }

        bloomMipsReady = true;
        return true;
    }

    bool Pipeline::Impl::RecordBloomChain(const BloomPass& bloomDesc)
    {
        if (!bloomMipsReady || offscreenCmd == nullptr)
        {
            return false;
        }
        auto& cmd = *offscreenCmd;

        // ---- 6 round downsample（HDR → mip[0]; mip[i-1] → mip[i] for i=1..5）
        for (std::size_t i = 0; i < kBloomMipCount; ++i)
        {
            auto& mip = bloomMips[i];

            const auto fromLayout = mip.layoutShaderReadOnly
                                        ? Orange::Rhi::TextureLayout::ShaderReadOnly
                                        : Orange::Rhi::TextureLayout::Undefined;
            cmd.TransitionTexture(*mip.texture, fromLayout,
                                  Orange::Rhi::TextureLayout::ColorAttachment);

            Orange::Rhi::ColorAttachment att{};
            att.mpView           = mip.texture->GetDefaultView();
            att.mLoadOp          = Orange::Rhi::LoadOp::Clear;
            att.mStoreOp         = Orange::Rhi::StoreOp::Store;
            att.mClear.mColor[0] = 0.0f;
            att.mClear.mColor[1] = 0.0f;
            att.mClear.mColor[2] = 0.0f;
            att.mClear.mColor[3] = 1.0f;

            Orange::Rhi::RenderingDesc rd{};
            rd.mRenderArea.mWidth  = mip.width;
            rd.mRenderArea.mHeight = mip.height;
            rd.mColorAttachments.push_back(att);
            cmd.BeginRendering(rd);

            Orange::Rhi::RHIViewport vp{};
            vp.mWidth    = static_cast<float>(mip.width);
            vp.mHeight   = static_cast<float>(mip.height);
            vp.mMinDepth = 0.0f;
            vp.mMaxDepth = 1.0f;
            cmd.SetViewport(vp);
            Orange::Rhi::RHIScissor sc{};
            sc.mWidth  = mip.width;
            sc.mHeight = mip.height;
            cmd.SetScissor(sc);

            cmd.BindGraphicsPipeline(*bloomDownsamplePipeline);
            cmd.SetDescriptorSet(0, *bloomDownsampleSets[i]);

            // mip[0] 跳走 bright-pass，喂 BloomPass.threshold；后续跳传 0。
            struct PushDown
            {
                float threshold;
                float clampMax; // 亮源钳制上限（仅 bright-pass 跳生效；<=0 不钳）
                float pad1, pad2;
            };
            PushDown pcData{};
            pcData.threshold = (i == 0) ? bloomDesc.threshold : 0.0f;
            pcData.clampMax  = bloomDesc.clampMax;
            cmd.SetPushConstants(Orange::Rhi::ShaderStage::Fragment,
                                 0, static_cast<std::uint32_t>(sizeof(pcData)), &pcData);

            cmd.Draw(3, 1, 0, 0); // big-triangle
            cmd.EndRendering();

            cmd.TransitionTexture(*mip.texture,
                                  Orange::Rhi::TextureLayout::ColorAttachment,
                                  Orange::Rhi::TextureLayout::ShaderReadOnly);
            mip.layoutShaderReadOnly = true;
        }

        // ---- 5 round upsample —— additive blend 累加到 mip[i] 已有内容上
        for (std::size_t step = 0; step < kBloomMipCount - 1; ++step)
        {
            // 从 mip[5] 向 mip[0] 推进：先写 mip[4]，再写 mip[3]，...，最后 mip[0]。
            const std::size_t i   = (kBloomMipCount - 2) - step; // 4, 3, 2, 1, 0
            auto&             mip = bloomMips[i];

            // 把目标 mip 从 ShaderReadOnly 切回 ColorAttachment 准备写入。
            cmd.TransitionTexture(*mip.texture,
                                  Orange::Rhi::TextureLayout::ShaderReadOnly,
                                  Orange::Rhi::TextureLayout::ColorAttachment);
            mip.layoutShaderReadOnly = false;

            Orange::Rhi::ColorAttachment att{};
            att.mpView   = mip.texture->GetDefaultView();
            att.mLoadOp  = Orange::Rhi::LoadOp::Load; // 保留 downsample 写入的内容
            att.mStoreOp = Orange::Rhi::StoreOp::Store;

            Orange::Rhi::RenderingDesc rd{};
            rd.mRenderArea.mWidth  = mip.width;
            rd.mRenderArea.mHeight = mip.height;
            rd.mColorAttachments.push_back(att);
            cmd.BeginRendering(rd);

            Orange::Rhi::RHIViewport vp{};
            vp.mWidth    = static_cast<float>(mip.width);
            vp.mHeight   = static_cast<float>(mip.height);
            vp.mMinDepth = 0.0f;
            vp.mMaxDepth = 1.0f;
            cmd.SetViewport(vp);
            Orange::Rhi::RHIScissor sc{};
            sc.mWidth  = mip.width;
            sc.mHeight = mip.height;
            cmd.SetScissor(sc);

            cmd.BindGraphicsPipeline(*bloomUpsamplePipeline);
            // upsample set i 采样 mip[i+1]
            cmd.SetDescriptorSet(0, *bloomUpsampleSets[i]);
            cmd.Draw(3, 1, 0, 0);
            cmd.EndRendering();

            // 写完之后再 transition 回 ShaderReadOnly，让下次 sample 安全。
            cmd.TransitionTexture(*mip.texture,
                                  Orange::Rhi::TextureLayout::ColorAttachment,
                                  Orange::Rhi::TextureLayout::ShaderReadOnly);
            mip.layoutShaderReadOnly = true;
        }

        return true;
    }

} // namespace Orange::Engine::Render
