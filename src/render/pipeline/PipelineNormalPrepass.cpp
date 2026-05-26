// Pipeline 的法线预通道实现：EnsureNormalBuffer / RecordNormalPrepass。
//
// 把场景几何的 view-space 法线渲到 normalBuffer（RGBA8，n*0.5+0.5 编码），供
// SSAO / SSR 采样真实法线，替代深度差分(dFdx/dFdy)重建。几何遍历复用与
// shadow caster 同款的 Drawables() 循环（绑 VB/IB + push MVP + DrawIndexed），
// 只是把 light-clip 换成 camera-clip、额外 push view*model 翻法线。
//
// depth 复用 sceneDepth 作 scratch（预通道需 z-test 只留最近面法线；写完留在
// DepthStencilAttachment，紧跟的主 pass 以 Undefined→DSA + LoadOp::Clear 丢弃
// 这份深度——这正是引擎无 god rays 时主 pass 的稳态转换，已被验证安全）。故本
// 预通道必须紧贴主 pass 之前录制，且只在 SSAO / SSR 激活时跑。

#include "PipelineImpl.h"

#include <glm/mat4x4.hpp>

namespace Orange::Engine::Render
{

bool Pipeline::Impl::EnsureNormalBuffer()
{
    if (renderDevice == nullptr || hdrWidth == 0 || hdrHeight == 0)
    {
        return false;
    }
    if (normalBuffer && normalBufferWidth == hdrWidth && normalBufferHeight == hdrHeight)
    {
        return true;
    }
    auto& rhi = renderDevice->GetRhiDevice();
    renderDevice->WaitIdle();
    normalBuffer.reset();

    Orange::Rhi::TextureDesc t{};
    t.mWidth     = hdrWidth;
    t.mHeight    = hdrHeight;
    t.mFormat    = Orange::Rhi::TextureFormat::RGBA8Unorm;
    t.mDimension = Orange::Rhi::TextureDimension::Tex2D;
    t.mUsage     = Orange::Rhi::TextureUsage::RenderTarget
                 | Orange::Rhi::TextureUsage::Sampled;
    normalBuffer = rhi.CreateTexture(t);
    if (!normalBuffer)
    {
        ORANGE_LOG_ERROR("Pipeline: normalBuffer CreateTexture 失败 ({}x{} RGBA8)",
                         hdrWidth, hdrHeight);
        return false;
    }
    normalBufferWidth                = hdrWidth;
    normalBufferHeight               = hdrHeight;
    normalBufferLayoutShaderReadOnly = false;
    return true;
}

bool Pipeline::Impl::RecordNormalPrepass(const glm::mat4& viewProj, const glm::mat4& view)
{
    if (offscreenCmd == nullptr || sceneDepth == nullptr || normalPrepassPipeline == nullptr)
    {
        return false;
    }
    if (!EnsureNormalBuffer())
    {
        return false;
    }
    auto& cmd = *offscreenCmd;

    // normalBuffer → ColorAttachment（首帧 Undefined，之后从 ShaderReadOnly）。
    const auto fromColor = normalBufferLayoutShaderReadOnly
        ? Orange::Rhi::TextureLayout::ShaderReadOnly
        : Orange::Rhi::TextureLayout::Undefined;
    cmd.TransitionTexture(*normalBuffer, fromColor,
                          Orange::Rhi::TextureLayout::ColorAttachment);

    // sceneDepth 作 scratch depth：转 DSA（from 选择与主 pass 同款）。
    const auto fromDepth = sceneDepthLayoutShaderReadOnly
        ? Orange::Rhi::TextureLayout::ShaderReadOnly
        : Orange::Rhi::TextureLayout::Undefined;
    cmd.TransitionTexture(*sceneDepth, fromDepth,
                          Orange::Rhi::TextureLayout::DepthStencilAttachment);
    sceneDepthLayoutShaderReadOnly = false;

    Orange::Rhi::ColorAttachment att{};
    att.mpView   = normalBuffer->GetDefaultView();
    att.mLoadOp  = Orange::Rhi::LoadOp::Clear;
    att.mStoreOp = Orange::Rhi::StoreOp::Store;
    // clear = (0.5,0.5,0.5) → 解码 n=(0,0,0)；天空 / 未覆盖处法线为零，但 SSAO /
    // SSR 已对 depth≈1 提前 return，不消费这些像素的法线。
    att.mClear.mColor[0] = 0.5f;
    att.mClear.mColor[1] = 0.5f;
    att.mClear.mColor[2] = 0.5f;
    att.mClear.mColor[3] = 1.0f;

    Orange::Rhi::DepthStencilAttachment depthAtt{};
    depthAtt.mpView        = sceneDepth->GetDefaultView();
    depthAtt.mDepthLoadOp  = Orange::Rhi::LoadOp::Clear;
    depthAtt.mDepthStoreOp = Orange::Rhi::StoreOp::Store;   // 主 pass 会重 clear，store 与否无影响
    depthAtt.mClear.mDepth = 1.0f;

    Orange::Rhi::RenderingDesc rd{};
    rd.mRenderArea.mWidth  = normalBufferWidth;
    rd.mRenderArea.mHeight = normalBufferHeight;
    rd.mColorAttachments.push_back(att);
    rd.mDepthStencil       = depthAtt;
    cmd.BeginRendering(rd);

    Orange::Rhi::RHIViewport vp{};
    vp.mWidth    = static_cast<float>(normalBufferWidth);
    vp.mHeight   = static_cast<float>(normalBufferHeight);
    vp.mMinDepth = 0.0f;
    vp.mMaxDepth = 1.0f;
    cmd.SetViewport(vp);
    Orange::Rhi::RHIScissor sc{};
    sc.mWidth  = normalBufferWidth;
    sc.mHeight = normalBufferHeight;
    cmd.SetScissor(sc);

    cmd.BindGraphicsPipeline(*normalPrepassPipeline);

    for (const auto& drawable : scene.Drawables())
    {
        if (!drawable.mesh.IsValid())
        {
            continue;
        }
        auto cacheIt = meshCache.find(drawable.mesh.Value());
        if (cacheIt == meshCache.end())
        {
            continue;
        }
        const auto& gpu = cacheIt->second;

        // push constant：uMVP(64) + uModelView(64) = 128 B（与 shadow_caster 同尺寸）。
        struct NormalPush { glm::mat4 mvp; glm::mat4 modelView; };
        NormalPush data{};
        data.mvp       = viewProj * drawable.worldMatrix;
        data.modelView = view * drawable.worldMatrix;
        cmd.SetPushConstants(Orange::Rhi::ShaderStage::Vertex, 0,
                             static_cast<std::uint32_t>(sizeof(data)), &data);

        cmd.BindVertexBuffer(0, *gpu.vertexBuffer, 0);
        cmd.BindIndexBuffer(*gpu.indexBuffer, 0, Orange::Rhi::IndexFormat::UInt32);
        cmd.DrawIndexed(gpu.indexCount, 1, 0, 0, 0);
    }

    cmd.EndRendering();

    cmd.TransitionTexture(*normalBuffer,
                          Orange::Rhi::TextureLayout::ColorAttachment,
                          Orange::Rhi::TextureLayout::ShaderReadOnly);
    normalBufferLayoutShaderReadOnly = true;
    return true;
}

}  // namespace Orange::Engine::Render
