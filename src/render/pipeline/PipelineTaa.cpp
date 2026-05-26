// Pipeline 的 TAA 实现：FindActiveTaaPass / ApplyTaaJitter / EnsureTaaResources
// / RecordTaaResolve。
//
// ApplyTaaJitter：TaaPass 激活时给投影叠加 per-frame Halton sub-pixel 偏移（渲染
// 路径用 jittered viewProj 跑法线预通道 + 主 pass，使每帧在不同亚像素采样）。
// RecordTaaResolve：所有 post 之后调用——
//   1. resolve：current HDR + 重投影的 prevHistory + 邻域 clamp → curHistory；
//   2. copy：curHistory → hdrColor（复用 dofCompositePipeline，replace blend）。
// taaHistory[2] ping-pong：parity p = frameIndex%2，写 [p] 读 [1-p]；本帧 curHistory
// 成为下帧 prevHistory。

#include "PipelineImpl.h"

#include "orange/engine/render/PostProcessPasses.h"

#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>   // glm::inverse

#include <cstring>

namespace Orange::Engine::Render
{
namespace
{
// Halton 低差异序列（base 进制 radical inverse），TAA jitter 标准取样。
float Halton(std::uint32_t index, std::uint32_t base)
{
    float f = 1.0f;
    float r = 0.0f;
    std::uint32_t i = index;
    while (i > 0)
    {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(i % base);
        i /= base;
    }
    return r;
}
}  // namespace

const TaaPass* Pipeline::Impl::FindActiveTaaPass() const noexcept
{
    if (postComponentActive)
    {
        return postTaa.enabled ? &postTaa : nullptr;
    }
    if (postProcessChain == nullptr)
    {
        return nullptr;
    }
    const std::size_t count = postProcessChain->PassCount();
    for (std::size_t i = 0; i < count; ++i)
    {
        const IPostProcessPass* p = postProcessChain->PassAt(i);
        if (const TaaPass* tp = dynamic_cast<const TaaPass*>(p))
        {
            return tp->enabled ? tp : nullptr;
        }
    }
    return nullptr;
}

glm::mat4 Pipeline::Impl::ApplyTaaJitter(const glm::mat4& proj) const
{
    if (FindActiveTaaPass() == nullptr || hdrWidth == 0 || hdrHeight == 0)
    {
        return proj;
    }
    // Halton(2,3) ∈ [0,1)，移到 [-0.5,0.5] 像素，再换算到 NDC（×2/分辨率）。
    const std::uint32_t idx = static_cast<std::uint32_t>(frameIndex % 8) + 1u;
    const float jx = (Halton(idx, 2u) - 0.5f) * 2.0f / static_cast<float>(hdrWidth);
    const float jy = (Halton(idx, 3u) - 0.5f) * 2.0f / static_cast<float>(hdrHeight);
    glm::mat4 j = proj;
    // glm 列主序：[col][row]。叠加到第 2 列的 x/y 行 → clip.xy 平移（亚像素）。
    // 累积对称，符号不影响 AA；重投影用同一 jittered viewProj 自洽。
    j[2][0] += jx;
    j[2][1] += jy;
    return j;
}

bool Pipeline::Impl::EnsureTaaResources()
{
    if (renderDevice == nullptr || sceneDepth == nullptr || hdrColor == nullptr
        || hdrSampler == nullptr || taaResolvePipeline == nullptr
        || dofCompositePipeline == nullptr || taaLayout == nullptr
        || bloomLayout == nullptr || taaUbo == nullptr)
    {
        return false;
    }
    if (hdrWidth == 0 || hdrHeight == 0)
    {
        return false;
    }
    auto& rhi = renderDevice->GetRhiDevice();

    // pool：2 resolve set（各 3 sampler + 1 ubo）+ 2 copy set（各 1 sampler）。
    if (!taaPool)
    {
        Orange::Rhi::DescriptorPoolDesc poolDesc{};
        poolDesc.mMaxSets = 4;
        poolDesc.mPoolSizes.push_back({Orange::Rhi::DescriptorType::CombinedImageSampler, 8});
        poolDesc.mPoolSizes.push_back({Orange::Rhi::DescriptorType::UniformBuffer, 2});
        poolDesc.mpDebugName = "orange_engine.taa.pool";
        taaPool = rhi.CreateDescriptorPool(poolDesc);
        if (!taaPool) { return false; }
    }

    // taaHistory[2]：RGBA16F，随 HDR 尺寸重建。重建即重置历史有效性 + 强制重绑 set。
    if (!taaHistory[0] || taaHistoryWidth != hdrWidth || taaHistoryHeight != hdrHeight)
    {
        renderDevice->WaitIdle();
        for (auto& h : taaHistory) { h.reset(); }
        // 不 reset sets（RHI pool 无 free-bit，reset+realloc 会 resize 累积耗尽
        // OOM）；置 taaSetsBoundHdr=null 触发下面 UpdateDescriptorSet 重写绑定到
        // 新建的 history 纹理。set 本身只分配一次、长期复用。
        taaSetsBoundHdr = nullptr;
        taaHasHistory   = false;
        taaHistoryLayoutShaderReadOnly = {false, false};

        for (auto& h : taaHistory)
        {
            Orange::Rhi::TextureDesc t{};
            t.mWidth     = hdrWidth;
            t.mHeight    = hdrHeight;
            t.mFormat    = PipelineDetail::kHdrColorFormat;
            t.mDimension = Orange::Rhi::TextureDimension::Tex2D;
            t.mUsage     = Orange::Rhi::TextureUsage::RenderTarget
                         | Orange::Rhi::TextureUsage::Sampled;
            h = rhi.CreateTexture(t);
            if (!h)
            {
                ORANGE_LOG_ERROR("Pipeline: taaHistory CreateTexture 失败 ({}x{})",
                                 hdrWidth, hdrHeight);
                return false;
            }
        }
        taaHistoryWidth  = hdrWidth;
        taaHistoryHeight = hdrHeight;
    }

    // sets：只分配一次（4 个：2 resolve + 2 copy）；hdr / depth / history 变化时
    // 只 UpdateDescriptorSet 重写绑定（不 reset+realloc，避免 resize 累积耗尽 OOM）。
    if (taaResolveSet[0] == nullptr)
    {
        for (std::uint32_t p = 0; p < 2; ++p)
        {
            taaResolveSet[p] = rhi.AllocateDescriptorSet(*taaPool, *taaLayout);
            if (!taaResolveSet[p]) { return false; }
            taaCopySet[p] = rhi.AllocateDescriptorSet(*taaPool, *bloomLayout);
            if (!taaCopySet[p]) { return false; }
        }
        taaSetsBoundHdr = nullptr;   // 强制下面 update
    }
    if (taaSetsBoundHdr != hdrColor.get())
    {
        for (std::uint32_t p = 0; p < 2; ++p)
        {
            // resolve set[p]：0=hdrColor, 1=prevHistory(=history[1-p]), 2=depth, 3=ubo。
            Orange::Rhi::DescriptorWrite w[4]{};
            w[0].mBinding             = 0;
            w[0].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            w[0].mImageInfo.mpTexture = hdrColor.get();
            w[0].mImageInfo.mpSampler = hdrSampler.get();
            w[1].mBinding             = 1;
            w[1].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            w[1].mImageInfo.mpTexture = taaHistory[1u - p].get();
            w[1].mImageInfo.mpSampler = hdrSampler.get();
            w[2].mBinding             = 2;
            w[2].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            w[2].mImageInfo.mpTexture = sceneDepth.get();
            w[2].mImageInfo.mpSampler = hdrSampler.get();
            w[3].mBinding             = 3;
            w[3].mType                = Orange::Rhi::DescriptorType::UniformBuffer;
            w[3].mBufferInfo.mpBuffer = taaUbo.get();
            w[3].mBufferInfo.mOffset  = 0;
            w[3].mBufferInfo.mRange   = sizeof(TaaUboData);
            rhi.UpdateDescriptorSet(*taaResolveSet[p], w, 4);

            // copy set[p]：0=curHistory(=history[p])，复用 bloomLayout。
            Orange::Rhi::DescriptorWrite cw{};
            cw.mBinding             = 0;
            cw.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            cw.mImageInfo.mpTexture = taaHistory[p].get();
            cw.mImageInfo.mpSampler = hdrSampler.get();
            rhi.UpdateDescriptorSet(*taaCopySet[p], &cw, 1);
        }
        taaSetsBoundHdr = hdrColor.get();
    }
    return true;
}

bool Pipeline::Impl::RecordTaaResolve(const TaaPass& taaDesc,
                                      const glm::mat4& curJitteredViewProj)
{
    if (offscreenCmd == nullptr || hdrColor == nullptr || sceneDepth == nullptr)
    {
        return false;
    }
    if (!EnsureTaaResources())
    {
        return false;
    }

    const std::uint32_t p = static_cast<std::uint32_t>(frameIndex % 2);

    // ---- 写 TaaUbo ----
    {
        TaaUboData data{};
        data.invCurViewProj = glm::inverse(curJitteredViewProj);
        data.prevViewProj   = taaPrevViewProj;
        data.params         = glm::vec4(taaDesc.feedback, taaHasHistory ? 1.0f : 0.0f,
                                        0.0f, 0.0f);
        void* mapped = taaUbo->Map();
        if (mapped == nullptr) { return false; }
        std::memcpy(mapped, &data, sizeof(data));
        taaUbo->Unmap();
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

    // prevHistory（taaHistory[1-p]）首次使用前是 Undefined——即便首帧 shader
    // 因 hasHistory=0 不采样它，descriptor 仍引用它，submit 时校验要求 layout =
    // ShaderReadOnly。这里 Undefined→ShaderReadOnly 把它转到合法 layout（内容
    // 仍是垃圾，但首帧不消费）。
    const std::uint32_t prevIdx = 1u - p;
    if (!taaHistoryLayoutShaderReadOnly[prevIdx])
    {
        cmd.TransitionTexture(*taaHistory[prevIdx],
                              Orange::Rhi::TextureLayout::Undefined,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
        taaHistoryLayoutShaderReadOnly[prevIdx] = true;
    }

    // ---- Pass 1：resolve（hdrColor + prevHistory + depth → curHistory）----
    {
        Orange::Rhi::RHITexture* cur = taaHistory[p].get();
        const auto from = taaHistoryLayoutShaderReadOnly[p]
            ? Orange::Rhi::TextureLayout::ShaderReadOnly
            : Orange::Rhi::TextureLayout::Undefined;
        cmd.TransitionTexture(*cur, from, Orange::Rhi::TextureLayout::ColorAttachment);

        Orange::Rhi::ColorAttachment att{};
        att.mpView   = cur->GetDefaultView();
        att.mLoadOp  = Orange::Rhi::LoadOp::Clear;   // 全屏覆盖
        att.mStoreOp = Orange::Rhi::StoreOp::Store;

        Orange::Rhi::RenderingDesc rd{};
        rd.mRenderArea.mWidth  = taaHistoryWidth;
        rd.mRenderArea.mHeight = taaHistoryHeight;
        rd.mColorAttachments.push_back(att);
        cmd.BeginRendering(rd);

        Orange::Rhi::RHIViewport vp{};
        vp.mWidth = static_cast<float>(taaHistoryWidth);
        vp.mHeight = static_cast<float>(taaHistoryHeight);
        vp.mMinDepth = 0.0f; vp.mMaxDepth = 1.0f;
        cmd.SetViewport(vp);
        Orange::Rhi::RHIScissor sc{};
        sc.mWidth = taaHistoryWidth; sc.mHeight = taaHistoryHeight;
        cmd.SetScissor(sc);

        cmd.BindGraphicsPipeline(*taaResolvePipeline);
        cmd.SetDescriptorSet(0, *taaResolveSet[p]);
        cmd.Draw(3, 1, 0, 0);
        cmd.EndRendering();

        cmd.TransitionTexture(*cur,
                              Orange::Rhi::TextureLayout::ColorAttachment,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
        taaHistoryLayoutShaderReadOnly[p] = true;
    }

    // ---- Pass 2：copy curHistory → hdrColor（复用 dofCompositePipeline）----
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
        cmd.SetDescriptorSet(0, *taaCopySet[p]);
        cmd.Draw(3, 1, 0, 0);
        cmd.EndRendering();

        cmd.TransitionTexture(*hdrColor,
                              Orange::Rhi::TextureLayout::ColorAttachment,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
    }

    // 本帧 jittered viewProj 存为下帧的 prevViewProj；标记历史有效。
    taaPrevViewProj = curJitteredViewProj;
    taaHasHistory   = true;
    return true;
}

}  // namespace Orange::Engine::Render
