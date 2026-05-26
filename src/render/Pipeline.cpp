// Pipeline 实现：双段 frame 流程。
//
// 流程：
//   Stage A —— 离屏 HDR 主 pass（Pipeline 自管 RHI cmd list）
//     transition Undefined/ShaderReadOnly → ColorAttachment
//     BeginRendering(HDR RGBA16F view, Clear)
//     for each drawable: BindPipeline(per-template) + push uMVP +
//                        BindVB / BindIB / DrawIndexed
//     EndRendering
//     transition ColorAttachment → ShaderReadOnly
//     SubmitCommandList → device.WaitIdle（0.x 兜底）
//
//   Stage B —— swap-chain 收尾（走 IRenderer::SubmitItem 路径）
//     renderer.BeginFrame
//     SubmitItem(passthrough fullscreen quad, descriptorSet[0]=HDR-CIS)
//     renderer.EndFrame
//
// 离屏段把场景渲到 RGBA16F off-screen target；收尾段用 fullscreen big-
// triangle + sampler 把离屏结果搬到 swap-chain。当 PostProcessChain 为空
// 或挂了无 Bloom/Tonemap 的 chain 时走本 fallback——视觉上等价于 06.02
// 的 "直接画到 swap-chain"（HDR clamp 到 [0, 1] 后字节级一致）。
//
// MaterialInstance 路由仍按 06.02 的 per-template `RHIPipeline` 缓存做。
// 区别只在于：per-template pipeline 的 color format 由 BGRA8Unorm 切到
// RGBA16F——绑定 HDR off-screen 时格式必须匹配。
//
// `RHIShaderModule` 缓存按 AssetHandle<ShaderAsset>::Value() 跨模板复用。
// `mesh GPU cache` 不变。
//
// 后处理链 / 自定义 RenderPass 真正接通由后续 Bloom / Tonemap /
// LUT / 自定义 InsertPass 跟进；本期只把 Pipeline 双段流
// 程铺出来 + 给后续留好 SetPostProcessChain / SetMaterialSystem 入
// 口与 IPostProcessPass context 字段。

#include "orange/engine/render/Pipeline.h"

#include "pipeline/PipelineImpl.h"

#include "orange/engine/asset/AssetRegistry.h"
#include "orange/engine/asset/MeshAsset.h"
#include "orange/engine/asset/ShaderAsset.h"
#include "orange/engine/asset/TextureAsset.h"
#include "orange/engine/core/Log.h"
#include "orange/engine/core/Profiler.h"
#include "orange/engine/platform/Window.h"
#include "orange/engine/render/BuiltinMaterials.h"
#include "orange/engine/render/BuiltinShadowShaders.h"
#include "orange/engine/render/DebugDrawScene.h"
#include "orange/engine/render/EnvironmentComponent.h"
#include "orange/engine/render/IblBaker.h"
#include "orange/engine/render/LightComponent.h"
#include "orange/engine/render/Material.h"
#include "orange/engine/render/MaterialInstance.h"
#include "orange/engine/render/MaterialTypes.h"
#include "orange/engine/render/PostProcessChain.h"
#include "orange/engine/render/PostProcessPasses.h"
#include "orange/engine/render/IRenderPass.h"
#include "orange/engine/render/RenderPassContext.h"
#include "orange/engine/render/RenderScene.h"
#include "orange/engine/render/VfxSystem.h"
#include "orange/engine/render/ShadowConfig.h"
#include "orange/engine/scene/TransformComponent.h"
#include "orange/engine/scene/World.h"
#include "orange/engine/scene/WorldPartition.h"

#include "orange/core/Log.h"
#include "orange/renderer/RenderDevice.h"
#include "orange/renderer/Renderer.h"
#include "orange/renderer/RenderTypes.h"
#include "orange/resource/UploadContext.h"
#include "orange/rhi/RHI.h"
#include "orange/rhi/RHIDescriptor.h"
#include "orange/rhi/RHIRendering.h"
#include "orange/rhi/RHISampler.h"
#include "orange/rhi/RHITexture.h"

#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

namespace Orange::Engine::Render
{

// Helper / constexpr / InterleavedVertex / OrangeRenderLogAdapter 等
// 内部辅助都搬到 src/render/pipeline/PipelineHelpers.{h,cpp}；本 TU
// 沿用旧名引用（Impl method body 与 Setup / Render 主循环都按短名调
// 用），借 using-directive 把 PipelineDetail 命名空间整入。
using namespace PipelineDetail;


Pipeline::Pipeline() : mpImpl(std::make_unique<Impl>())
{
}

Pipeline::~Pipeline()
{
    Shutdown();
}

Pipeline::Pipeline(Pipeline&&) noexcept            = default;
Pipeline& Pipeline::operator=(Pipeline&&) noexcept = default;

bool Pipeline::IsInitialized() const noexcept
{
    return mpImpl && mpImpl->initialized;
}


Result<void, ResultCode> Pipeline::Initialize(Platform::Window&         window,
                                              Asset::AssetRegistry&     assets)
{
    auto& impl = *mpImpl;
    if (impl.initialized)
    {
        return ResultCode::AlreadyInitialized;
    }
    impl.assets = &assets;
    impl.window = &window;

    // 把 OrangeRender 内部的 Orange::Log* 转入本仓 ORANGE_LOG_*。必须在
    // RenderDevice::Create 之前注册，否则 Instance / Device 创建期间的
    // validation 信息会落到 stderr 而非本仓日志流。
    ::Orange::SetLogSink(&OrangeRenderLogAdapter, nullptr);

    // 1. RenderDevice ----------------------------------------------------
    Orange::Renderer::RenderDeviceDesc deviceDesc{};
    deviceDesc.mBackend          = Orange::Renderer::BackendType::Default;
    deviceDesc.mEnableValidation = true;
    impl.ownedRenderDevice = Orange::Renderer::RenderDevice::Create(deviceDesc);
    if (!impl.ownedRenderDevice)
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: RenderDevice::Create 失败");
        return ResultCode::InternalError;
    }
    impl.renderDevice  = impl.ownedRenderDevice.get();
    impl.offscreenMode = false;

    // 2. Renderer --------------------------------------------------------
    impl.renderer = Orange::Renderer::CreateRenderer();
    if (!impl.renderer)
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: CreateRenderer 失败");
        impl.ownedRenderDevice.reset();
        impl.renderDevice = nullptr;
        return ResultCode::InternalError;
    }

    Orange::Renderer::RendererDesc rendererDesc{};
    rendererDesc.mpDevice             = &impl.renderDevice->GetRhiDevice();
    rendererDesc.mpNativeWindowHandle = window.GetGlfwWindowHandle();
    rendererDesc.mFramesInFlight      = 2;

    if (Orange::Failed(impl.renderer->Initialize(rendererDesc)))
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: Renderer::Initialize 失败");
        impl.renderer.reset();
        impl.ownedRenderDevice.reset();
        impl.renderDevice = nullptr;
        return ResultCode::InternalError;
    }

    // 3. UploadContext --------------------------------------------------
    impl.upload = std::make_unique<Orange::Resource::UploadContext>();
    if (Orange::Failed(impl.upload->Initialize(impl.renderDevice->GetRhiDevice())))
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: UploadContext::Initialize 失败");
        impl.upload.reset();
        impl.renderer->Shutdown();
        impl.renderer.reset();
        impl.ownedRenderDevice.reset();
        impl.renderDevice = nullptr;
        return ResultCode::InternalError;
    }

    auto setupResult = SetupRhiResources();
    if (setupResult.IsErr())
    {
        return setupResult.Error();
    }

    // 8. 初始 HDR target —— 按 window 当前 framebuffer extent 建一张。
    impl.SeedExtentFromWindow();
    if (impl.pendingWidth > 0 && impl.pendingHeight > 0)
    {
        impl.EnsureHdrTarget();
    }

    impl.frameIndex  = 0;
    impl.initialized = true;
    return Result<void, ResultCode>{};
}

Result<void, ResultCode> Pipeline::InitializeOffscreen(Orange::Renderer::RenderDevice& device,
                                                       Asset::AssetRegistry&           assets,
                                                       std::uint32_t                   width,
                                                       std::uint32_t                   height)
{
    auto& impl = *mpImpl;
    if (impl.initialized)
    {
        return ResultCode::AlreadyInitialized;
    }
    if (width == 0 || height == 0)
    {
        ORANGE_LOG_ERROR("Pipeline::InitializeOffscreen: width / height 必须 > 0");
        return ResultCode::InvalidArgument;
    }

    impl.assets         = &assets;
    impl.window         = nullptr;
    impl.renderDevice   = &device;                  // 借用外部 RenderDevice
    impl.ownedRenderDevice.reset();                 // 显式：本路径不持有 device
    impl.offscreenMode  = true;
    impl.pendingWidth   = width;
    impl.pendingHeight  = height;
    impl.hdrDirty       = true;

    // UploadContext：与 window 模式一致，用 `device.GetRhiDevice()` 初始化。
    // 失败路径手动回滚字段（不调 Shutdown —— Shutdown 假定 initialized 后
    // 的资源全图，这里只有 upload 半就绪）。
    impl.upload = std::make_unique<Orange::Resource::UploadContext>();
    if (Orange::Failed(impl.upload->Initialize(impl.renderDevice->GetRhiDevice())))
    {
        ORANGE_LOG_ERROR("Pipeline::InitializeOffscreen: UploadContext::Initialize 失败");
        impl.upload.reset();
        impl.renderDevice  = nullptr;
        impl.offscreenMode = false;
        impl.assets        = nullptr;
        return ResultCode::InternalError;
    }

    // RHI 资源（sampler / passthrough / bloom / tonemap / godrays / 主 pass
    // UBO / shadow caster）共享路径；失败时 SetupRhiResources 内部已调
    // Shutdown 整体回滚，本函数只需透传错误码。
    auto setupResult = SetupRhiResources();
    if (setupResult.IsErr())
    {
        return setupResult.Error();
    }

    // 8. 初始 HDR + viewport 目标。pendingWidth/Height 已经由本入口写入，
    // 不需要 SeedExtentFromWindow。EnsureHdrTarget 失败时回滚整盘资源。
    if (!impl.EnsureHdrTarget())
    {
        ORANGE_LOG_ERROR("Pipeline::InitializeOffscreen: EnsureHdrTarget 失败 ({}x{})",
                         width, height);
        Shutdown();
        return ResultCode::InternalError;
    }
    if (!impl.EnsureViewportTarget())
    {
        ORANGE_LOG_ERROR("Pipeline::InitializeOffscreen: EnsureViewportTarget 失败 ({}x{})",
                         width, height);
        Shutdown();
        return ResultCode::InternalError;
    }

    impl.frameIndex  = 0;
    impl.initialized = true;
    return Result<void, ResultCode>{};
}

void Pipeline::ResizeOffscreen(std::uint32_t width, std::uint32_t height) noexcept
{
    if (!mpImpl)
    {
        return;
    }
    auto& impl = *mpImpl;
    if (!impl.offscreenMode || !impl.initialized)
    {
        return;
    }
    if (width == impl.pendingWidth && height == impl.pendingHeight)
    {
        return;
    }
    // 实际重建在下一次 Render 顶部的 EnsureHdrTarget / EnsureViewportTarget
    // 路径里发生；OnResize 同节奏（pendingWidth/Height + hdrDirty 标记）。
    impl.pendingWidth  = width;
    impl.pendingHeight = height;
    impl.hdrDirty      = true;
}

const Orange::Rhi::RHITexture* Pipeline::GetOffscreenColor() const noexcept
{
    if (!mpImpl)
    {
        return nullptr;
    }
    auto& impl = *mpImpl;
    if (!impl.offscreenMode)
    {
        return nullptr;
    }
    return impl.viewportColor.get();
}

bool Pipeline::DebugReadbackPixel(std::uint32_t x, std::uint32_t y,
                                  float outRGBA[4]) const
{
    if (!mpImpl || outRGBA == nullptr)
    {
        return false;
    }
    auto& impl = *mpImpl;
    if (!impl.offscreenMode || !impl.viewportColor || impl.renderDevice == nullptr
        || !impl.offscreenCmd || x >= impl.viewportWidth || y >= impl.viewportHeight)
    {
        return false;
    }

    auto& rhi = impl.renderDevice->GetRhiDevice();
    const std::uint64_t pixels = static_cast<std::uint64_t>(impl.viewportWidth)
                               * static_cast<std::uint64_t>(impl.viewportHeight);
    const std::uint64_t bytes = pixels * 4u;  // BGRA8

    Orange::Rhi::BufferDesc bd{};
    bd.mSize        = bytes;
    bd.mUsage       = Orange::Rhi::BufferUsage::Transfer;
    bd.mMemoryUsage = Orange::Rhi::MemoryUsage::GpuToCpu;
    auto readback = rhi.CreateBuffer(bd);
    if (!readback)
    {
        return false;
    }

    auto& cmd = *impl.offscreenCmd;
    if (cmd.Begin() != Orange::ResultCode::Success)
    {
        return false;
    }
    // viewportColor 在上一帧末为 ShaderReadOnly。
    cmd.TransitionTexture(*impl.viewportColor,
                          Orange::Rhi::TextureLayout::ShaderReadOnly,
                          Orange::Rhi::TextureLayout::TransferSrc);
    {
        Orange::Rhi::BufferTextureCopyRegion r{};
        r.mBufferOffset = 0;
        r.mMipLevel     = 0;
        r.mArrayLayer   = 0;
        r.mWidth        = impl.viewportWidth;
        r.mHeight       = impl.viewportHeight;
        r.mDepth        = 1;
        cmd.CopyTextureToBuffer(*impl.viewportColor, *readback, r);
    }
    cmd.TransitionTexture(*impl.viewportColor,
                          Orange::Rhi::TextureLayout::TransferSrc,
                          Orange::Rhi::TextureLayout::ShaderReadOnly);
    if (cmd.End() != Orange::ResultCode::Success
        || rhi.SubmitCommandList(cmd) != Orange::ResultCode::Success)
    {
        return false;
    }
    impl.renderDevice->WaitIdle();

    const void* mapped = readback->Map();
    if (mapped == nullptr)
    {
        return false;
    }
    const auto* p = static_cast<const std::uint8_t*>(mapped);
    const std::uint64_t idx = (static_cast<std::uint64_t>(y) * impl.viewportWidth + x) * 4u;
    // kSwapchainColorFormat = BGRA8Unorm → 字节序 B, G, R, A。
    const float b = p[idx + 0] / 255.0f;
    const float g = p[idx + 1] / 255.0f;
    const float rr = p[idx + 2] / 255.0f;
    const float a = p[idx + 3] / 255.0f;
    readback->Unmap();
    outRGBA[0] = rr;
    outRGBA[1] = g;
    outRGBA[2] = b;
    outRGBA[3] = a;
    return true;
}

void Pipeline::Shutdown()
{
    if (!mpImpl)
    {
        return;
    }
    auto& impl = *mpImpl;

    if (impl.renderDevice)
    {
        impl.renderDevice->WaitIdle();
    }

    impl.meshCache.clear();
    impl.templatePipelines.clear();
    impl.shaderModules.clear();

    // 先释放 game-side InsertPass —— 它们的析构可能依赖 RHI 句柄
    // （pipeline / descriptor 等），必须在 renderDevice 还活着时跑。
    for (auto& v : impl.insertedPasses)
    {
        v.clear();
    }

    impl.ReleaseBloomResources();
    impl.godRaysSet.reset();
    impl.godRaysPool.reset();
    impl.godRaysPipeline.reset();
    impl.godRaysFs.reset();
    impl.godRaysSetBoundDepth = nullptr;

    // SSAO 资源（set 先于 pool 释放）。
    impl.ssaoSet.reset();
    impl.ssaoApplySet.reset();
    impl.ssaoPool.reset();
    impl.ssaoColor.reset();
    impl.ssaoColorWidth = 0;
    impl.ssaoColorHeight = 0;
    impl.ssaoColorLayoutShaderReadOnly = false;
    impl.ssaoSetBoundDepth = nullptr;
    impl.ssaoApplySetBoundAo = nullptr;
    impl.ssaoPipeline.reset();
    impl.gtaoPipeline.reset();
    impl.ssaoApplyPipeline.reset();
    impl.ssaoLayout.reset();
    impl.ssaoUbo.reset();
    impl.ssaoFs.reset();
    impl.ssaoApplyFs.reset();
    impl.gtaoFs.reset();

    // SSR 资源（set 先于 pool）。
    impl.ssrSet.reset();
    impl.ssrCompositeSet.reset();
    impl.ssrPool.reset();
    impl.ssrColor.reset();
    impl.ssrColorWidth = 0;
    impl.ssrColorHeight = 0;
    impl.ssrColorLayoutShaderReadOnly = false;
    impl.ssrSetBoundDepth = nullptr;
    impl.ssrSetBoundHdr = nullptr;
    impl.ssrCompositeSetBound = nullptr;
    impl.ssrPipeline.reset();
    impl.ssrCompositePipeline.reset();
    impl.ssrLayout.reset();
    impl.ssrUbo.reset();
    impl.ssrFs.reset();
    impl.ssrCompositeFs.reset();
    impl.ssaoSetBoundNormal = nullptr;
    impl.ssrSetBoundNormal = nullptr;

    // 接触阴影资源（set 先于 pool）。
    impl.contactShadowSet.reset();
    impl.contactShadowPool.reset();
    impl.contactShadowPipeline.reset();
    impl.contactShadowLayout.reset();
    impl.contactShadowUbo.reset();
    impl.contactShadowFs.reset();
    impl.contactShadowSetBoundDepth = nullptr;
    impl.contactShadowSetBoundNormal = nullptr;

    // 景深资源（set 先于 pool）。
    impl.dofSet.reset();
    impl.dofCompositeSet.reset();
    impl.dofPool.reset();
    impl.dofColor.reset();
    impl.dofColorWidth = 0;
    impl.dofColorHeight = 0;
    impl.dofColorLayoutShaderReadOnly = false;
    impl.dofSetBoundHdr = nullptr;
    impl.dofSetBoundDepth = nullptr;
    impl.dofCompositeSetBound = nullptr;
    impl.dofPipeline.reset();
    impl.dofCompositePipeline.reset();
    impl.dofLayout.reset();
    impl.dofUbo.reset();
    impl.dofFs.reset();
    impl.dofCompositeFs.reset();

    // TAA 资源（set 先于 pool）。
    for (auto& s : impl.taaResolveSet) { s.reset(); }
    for (auto& s : impl.taaCopySet) { s.reset(); }
    impl.taaPool.reset();
    for (auto& h : impl.taaHistory) { h.reset(); }
    impl.taaHistoryWidth = 0;
    impl.taaHistoryHeight = 0;
    impl.taaHistoryLayoutShaderReadOnly = {false, false};
    impl.taaSetsBoundHdr = nullptr;
    impl.taaHasHistory = false;
    impl.taaResolvePipeline.reset();
    impl.taaLayout.reset();
    impl.taaUbo.reset();
    impl.taaResolveFs.reset();

    // 色彩分级资源（set 先于 pool）。
    impl.colorGradeSet.reset();
    impl.gradeCompositeSet.reset();
    impl.colorGradePool.reset();
    impl.gradeColor.reset();
    impl.gradeColorWidth = 0;
    impl.gradeColorHeight = 0;
    impl.gradeColorLayoutShaderReadOnly = false;
    impl.colorGradeSetBoundHdr = nullptr;
    impl.gradeCompositeSetBound = nullptr;
    impl.colorGradePipeline.reset();
    impl.colorGradeLayout.reset();
    impl.colorGradeUbo.reset();
    impl.colorGradeFs.reset();

    // 相机运动模糊资源（set 先于 pool）。
    impl.motionBlurSet.reset();
    impl.motionBlurCompositeSet.reset();
    impl.motionBlurPool.reset();
    impl.motionBlurColor.reset();
    impl.motionBlurColorWidth = 0;
    impl.motionBlurColorHeight = 0;
    impl.motionBlurColorLayoutShaderReadOnly = false;
    impl.motionBlurSetBoundHdr = nullptr;
    impl.motionBlurSetBoundDepth = nullptr;
    impl.motionBlurCompositeSetBound = nullptr;
    impl.motionBlurHasHistory = false;
    impl.motionBlurPipeline.reset();
    impl.motionBlurLayout.reset();
    impl.motionBlurUbo.reset();
    impl.motionBlurFs.reset();

    // 镜头效果（色散 + 暗角）资源（set 先于 pool）。
    impl.lensSet.reset();
    impl.lensCompositeSet.reset();
    impl.lensPool.reset();
    impl.lensColor.reset();
    impl.lensColorWidth = 0;
    impl.lensColorHeight = 0;
    impl.lensColorLayoutShaderReadOnly = false;
    impl.lensSetBoundHdr = nullptr;
    impl.lensCompositeSetBound = nullptr;
    impl.lensPipeline.reset();
    impl.lensLayout.reset();
    impl.lensUbo.reset();
    impl.lensFs.reset();

    // 锐化（CAS）资源（set 先于 pool）。
    impl.sharpenSet.reset();
    impl.sharpenCompositeSet.reset();
    impl.sharpenPool.reset();
    impl.sharpenColor.reset();
    impl.sharpenColorWidth = 0;
    impl.sharpenColorHeight = 0;
    impl.sharpenColorLayoutShaderReadOnly = false;
    impl.sharpenSetBoundHdr = nullptr;
    impl.sharpenCompositeSetBound = nullptr;
    impl.sharpenPipeline.reset();
    impl.sharpenLayout.reset();
    impl.sharpenUbo.reset();
    impl.sharpenFs.reset();

    // 法线预通道资源。
    impl.normalPrepassPipeline.reset();
    impl.normalPrepassVs.reset();
    impl.normalPrepassFs.reset();
    impl.normalBuffer.reset();
    impl.normalBufferWidth = 0;
    impl.normalBufferHeight = 0;
    impl.normalBufferLayoutShaderReadOnly = false;
    // DebugDrawScene 必须在 renderDevice WaitIdle 之后、其他 RHI 资源释放前
    // 一起释放——Orange::Renderer::DebugDraw 持有 vertex staging buffer 与
    // 两条 pipeline，析构需要 device 还活着。
    if (impl.debugDrawScene)
    {
        impl.debugDrawScene->ShutdownBackend_();
        impl.debugDrawScene.reset();
    }
    impl.proceduralSkyPipeline.reset();
    impl.proceduralSkyFs.reset();
    impl.skySet.reset();
    impl.skyPool.reset();
    impl.skyPipeline.reset();
    impl.skyLayout.reset();
    impl.skyFs.reset();
    impl.skySetBoundCube = nullptr;
    impl.shadowCasterPipeline.reset();
    impl.shadowMap.reset();
    impl.shadowMapResolution = 0;
    impl.shadowMapLayoutShaderReadOnly = false;
    // spot shadow array：per-layer view 必须先于 array texture 释放。
    for (auto& v : impl.spotShadowLayerViews) { v.reset(); }
    impl.spotShadowArray.reset();
    impl.spotShadowArrayResolution = 0;
    impl.spotShadowArrayLayoutShaderReadOnly = false;
    // point shadow cubes：per-face view 同样先于 cube texture 释放。
    for (auto& v : impl.pointShadowFaceViews) { v.reset(); }
    for (auto& c : impl.pointShadowCubes)     { c.reset(); }
    impl.pointShadowCubeResolution = 0;
    impl.pointShadowCubeLayoutShaderReadOnly = false;
    impl.mainDescSet.reset();
    impl.mainDescPool.reset();
    impl.mainDescLayout.reset();
    impl.lightUbo.reset();
    impl.pointLightsUbo.reset();
    impl.spotLightsUbo.reset();
    impl.spotShadowUbo.reset();
    // set 1 material 资源（GAP-2026-05-25 A2/G1）：descriptor set 必须先于
    // pool 释放；贴图缓存 + default 贴图 + sampler 最后。templatePipelines 已
    // 在本函数顶部 clear（PBR pipeline 引用 materialTexLayout），故 layout
    // 此刻可安全释放。
    impl.materialDescCache.clear();
    impl.defaultMaterialSet.reset();
    impl.materialTexPool.reset();
    impl.materialTexLayout.reset();
    impl.materialTexCache.clear();
    impl.defaultWhiteTex.reset();
    impl.defaultNormalTex.reset();
    impl.materialSampler.reset();
    impl.shadowCasterVsHandle = {};
    impl.shadowCasterFsHandle = {};
    impl.tonemapPipeline.reset();
    impl.passthroughCombinePipeline.reset();
    impl.bloomUpsamplePipeline.reset();
    impl.bloomDownsamplePipeline.reset();
    impl.tonemapFs.reset();
    impl.tonemapVs.reset();
    impl.passthroughCombineFs.reset();
    impl.bloomUpsampleFs.reset();
    impl.bloomDownsampleFs.reset();
    impl.combineLayout.reset();
    impl.bloomLayout.reset();

    impl.passthroughPipeline.reset();
    impl.passthroughFs.reset();
    impl.fullscreenVs.reset();
    impl.passthroughSet.reset();
    impl.passthroughPool.reset();
    impl.passthroughLayout.reset();
    impl.hdrSampler.reset();
    impl.hdrColor.reset();
    impl.sceneDepth.reset();
    impl.viewportColor.reset();
    impl.viewportLayoutShaderReadOnly = false;
    impl.captureBuffer.reset();
    impl.captureBufferCapacity = 0;
    impl.pendingCapturePath.reset();
    impl.offscreenCmd.reset();

    if (impl.upload)
    {
        impl.upload->Shutdown();
        impl.upload.reset();
    }

    impl.builtinDefaultMaterial = Material{};
    impl.builtinDefaultLoaded   = false;
    // baked IBL 三件套（BakeIblFromWorld 出口）必须在 renderer/ownedRenderDevice
    // reset 之前显式释放——否则 ~Impl 在 Shutdown 返回后才析构 unique_ptr，VMA /
    // VkDevice 已死，触发 VmaBlockMetadata 析构期 leak 断言。
    impl.bakedBrdfLut.reset();
    impl.bakedPrefilteredCube.reset();
    impl.bakedIrradianceCube.reset();
    impl.bakedEnvCube.reset();
    impl.lastBakedCubemap = {};
    impl.dummyBrdfLut.reset();
    impl.dummyPrefilteredCube.reset();
    impl.dummyIrradianceCube.reset();

    if (impl.renderer)
    {
        impl.renderer->Shutdown();
    }
    impl.renderer.reset();
    impl.ownedRenderDevice.reset();
    impl.renderDevice  = nullptr;
    impl.offscreenMode = false;

    impl.assets       = nullptr;
    impl.window       = nullptr;
    impl.initialized  = false;
    impl.pendingWidth = 0;
    impl.pendingHeight= 0;
    impl.hdrWidth     = 0;
    impl.hdrHeight    = 0;
    impl.hdrDirty     = true;
    impl.hdrLayoutShaderReadOnly = false;

    // 与 Initialize 头部 SetLogSink 配对。放在所有 RHI 资源 reset 之后：
    // 析构链里可能还有 OrangeRender 内部 log（DeferredDestroy / pool free
    // 之类），不能太早断掉桥。
    ::Orange::ClearLogSink();
}

void Pipeline::OnResize(std::uint32_t width, std::uint32_t height)
{
    auto& impl = *mpImpl;
    if (!impl.initialized || !impl.renderer)
    {
        return;
    }
    impl.renderer->OnResize(width, height);
    if (width != impl.pendingWidth || height != impl.pendingHeight)
    {
        impl.pendingWidth  = width;
        impl.pendingHeight = height;
        impl.hdrDirty      = true;
    }
}

void Pipeline::SetPostProcessChain(PostProcessChain* chain) noexcept
{
    if (mpImpl)
    {
        mpImpl->postProcessChain = chain;
    }
}

void Pipeline::SetMaterialSystem(MaterialSystem* system) noexcept
{
    if (mpImpl)
    {
        mpImpl->materialSystem = system;
    }
}

void Pipeline::SetWorldPartition(const Scene::WorldPartition* partition) noexcept
{
    if (mpImpl)
    {
        mpImpl->worldPartition = partition;
    }
}

void Pipeline::SetEditorCameraOverride(const Camera* camera) noexcept
{
    if (mpImpl)
    {
        mpImpl->editorCameraOverride = camera;
    }
}

namespace
{

constexpr std::size_t StageIndex(PipelineStage stage) noexcept
{
    return static_cast<std::size_t>(stage);
}

}  // namespace

void Pipeline::InsertPass(PipelineStage stage, std::unique_ptr<IRenderPass> pass)
{
    if (!mpImpl || !pass)
    {
        return;
    }
    const std::size_t idx = StageIndex(stage);
    if (idx >= mpImpl->insertedPasses.size())
    {
        ORANGE_LOG_ERROR("Pipeline::InsertPass: 无效 stage 编号 {}",
                         static_cast<unsigned>(idx));
        return;
    }

    // 立即调一次 Setup，让 pass 建 GPU 资源（pipeline / descriptor 等）。
    // 即使 Pipeline 自己尚未 Initialize，Setup 也会被触发——pass 自己
    // 负责对 nullptr device 等情况兜底。这条让"先 InsertPass 后
    // Initialize Pipeline" 与"先 Initialize Pipeline 后 InsertPass"
    // 两条路径都合法，调用方不必关心顺序。
    RenderGraphBuilder builder{};
    builder.SetKind(RenderGraphBuilder::SetupKind::Initial);
    builder.pRenderer = mpImpl->renderer.get();
    builder.pDevice   = mpImpl->renderDevice ? &mpImpl->renderDevice->GetRhiDevice() : nullptr;
    pass->Setup(builder);

    mpImpl->insertedPasses[idx].emplace_back(std::move(pass));
}

void Pipeline::RemovePassesAt(PipelineStage stage)
{
    if (!mpImpl)
    {
        return;
    }
    const std::size_t idx = StageIndex(stage);
    if (idx < mpImpl->insertedPasses.size())
    {
        mpImpl->insertedPasses[idx].clear();
    }
}

void Pipeline::ClearInsertedPasses()
{
    if (!mpImpl)
    {
        return;
    }
    for (auto& v : mpImpl->insertedPasses)
    {
        v.clear();
    }
}

std::size_t Pipeline::InsertedPassCount(PipelineStage stage) const noexcept
{
    if (!mpImpl)
    {
        return 0;
    }
    const std::size_t idx = StageIndex(stage);
    if (idx >= mpImpl->insertedPasses.size())
    {
        return 0;
    }
    return mpImpl->insertedPasses[idx].size();
}

void Pipeline::SetVfxSystem(VfxSystem* system) noexcept
{
    if (!mpImpl)
    {
        return;
    }
    mpImpl->vfxSystem = system;

    // sample 端心智成本最低化：只要 Pipeline 已 Initialize、AssetRegistry
    // 也在手，SetVfxSystem 顺手把 VfxSystem 的 GPU 资源建好。失败时落
    // 一条 error log 并清空挂载——粒子 pass 自然 fallback 为 no-op。
    if (system != nullptr && !system->IsInitialized()
        && mpImpl->renderDevice != nullptr && mpImpl->assets != nullptr)
    {
        auto r = system->Initialize(mpImpl->renderDevice,
                                    /*framesInFlight=*/2u,
                                    *mpImpl->assets);
        if (r.IsErr())
        {
            ORANGE_LOG_ERROR("Pipeline::SetVfxSystem: 自动 Initialize 失败 (code={})",
                             static_cast<unsigned>(r.Error()));
            mpImpl->vfxSystem = nullptr;
        }
    }
}

void Pipeline::SetShadowConfig(const ShadowConfig& config) noexcept
{
    if (!mpImpl)
    {
        return;
    }
    mpImpl->shadowConfig = config;
    // mapResolution 切换会让 EnsureShadowMap 在下一帧重建 shadow target。
}

void Pipeline::SetDummyIblAmbient(float r, float g, float b) noexcept
{
    if (!mpImpl) { return; }
    mpImpl->dummyIblAmbient = glm::vec3{r, g, b};
    // Initialize 之后调：dummyIrradianceCube 已经创建并初始化过，需要按新值
    // 重填 GPU 数据。Initialize 之前调：dummyIrradianceCube 还是 nullptr，
    // FillDummyIblIrradiance 内部判 nullptr 返 false，本字段值会在 Initialize
    // 的 FillDummyIblIrradiance(nullptr) 路径里被读到 —— 都正确。
    if (mpImpl->dummyIrradianceCube != nullptr)
    {
        mpImpl->FillDummyIblIrradiance(/*pBootCmd=*/nullptr);
    }
}

void Pipeline::SetSceneClearColor(float r, float g, float b) noexcept
{
    if (!mpImpl) { return; }
    mpImpl->sceneClearColor = glm::vec3{r, g, b};
    // 不动 GPU 资源；下一帧 RecordOffscreenPass / Sky pass 入口从字段读最新值。
}

void Pipeline::SetAuxPassProvider(IAuxPassProvider* pProvider) noexcept
{
    if (!mpImpl) { return; }
    mpImpl->pAuxPassProvider = pProvider;
}

void Pipeline::SetSkyEnabled(bool enabled) noexcept
{
    if (!mpImpl) { return; }
    mpImpl->skyEnabled = enabled;
}

bool Pipeline::IsSkyEnabled() const noexcept
{
    return mpImpl && mpImpl->skyEnabled;
}

DebugDrawScene* Pipeline::GetDebugDrawScene() noexcept
{
    return mpImpl ? mpImpl->debugDrawScene.get() : nullptr;
}

const DebugDrawScene* Pipeline::GetDebugDrawScene() const noexcept
{
    return mpImpl ? mpImpl->debugDrawScene.get() : nullptr;
}

void Pipeline::SetIblTextures(Orange::Rhi::RHITexture* irradianceCube,
                              Orange::Rhi::RHITexture* prefilteredCube,
                              Orange::Rhi::RHITexture* brdfLut2D) noexcept
{
    if (!mpImpl || !mpImpl->renderDevice || !mpImpl->mainDescSet || !mpImpl->hdrSampler)
    {
        return;  // 未 Initialize 或 main desc set 尚未建好——silent-ignore
    }

    // nullptr → 回退到对应 dummy（启动期注入的 1×1 黑），与 Initialize
    // 时 binding 2/3/4 的 dummy 注入路径完全等价。
    Orange::Rhi::RHITexture* irrTex = irradianceCube  ? irradianceCube  : mpImpl->dummyIrradianceCube.get();
    Orange::Rhi::RHITexture* prefTex = prefilteredCube ? prefilteredCube : mpImpl->dummyPrefilteredCube.get();
    Orange::Rhi::RHITexture* lutTex  = brdfLut2D       ? brdfLut2D       : mpImpl->dummyBrdfLut.get();

    Orange::Rhi::DescriptorWrite writes[3] = {};
    writes[0].mBinding             = 2;
    writes[0].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
    writes[0].mImageInfo.mpTexture = irrTex;
    writes[0].mImageInfo.mpSampler = mpImpl->hdrSampler.get();

    writes[1].mBinding             = 3;
    writes[1].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
    writes[1].mImageInfo.mpTexture = prefTex;
    writes[1].mImageInfo.mpSampler = mpImpl->hdrSampler.get();

    writes[2].mBinding             = 4;
    writes[2].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
    writes[2].mImageInfo.mpTexture = lutTex;
    writes[2].mImageInfo.mpSampler = mpImpl->hdrSampler.get();

    mpImpl->renderDevice->GetRhiDevice().UpdateDescriptorSet(*mpImpl->mainDescSet, writes, 3);
}

// ---------------------------------------------------------------------------
// BakeIblFromWorld：高阶 IBL 接通入口（EnvironmentComponent → IblBaker 三件套）
//
// 责任划分：本接口把 c1~c7 的所有片段串起来——sample / 编辑器只需挂
// EnvironmentComponent 然后调一次，不接触 RHITexture / IblBaker 实例。
//
//   World ──首个 EnvironmentComponent──> cubemap handle
//     ──AssetRegistry::Get<TextureAsset>──> CPU byte buffer (RGBA32Float)
//     ──CreateTexture + staging upload + Transition──> equirect RHITexture
//     ──IblBaker.BakeEquirectToCube──> 6-face cube
//     ├──BakeIrradiance──> irradiance cube
//     ├──BakePrefilteredEnvironment──> 9-mip prefiltered specular cube
//     └──BakeBrdfLut──> split-sum 2D LUT
//   Pipeline::SetIblTextures(...) → PBR shader binding 2/3/4 切换
//
// 中间产物（equirect / 6-face cube）用完即析构；最终三件套以 unique_ptr
// 落 impl.baked* —— 等寿与 Pipeline 一致，避免 IblBaker 析构后 GPU 端纹理
// 失效。
// ---------------------------------------------------------------------------
void Pipeline::BakeIblFromWorld(::Orange::Engine::World&                world,
                                ::Orange::Engine::Asset::AssetRegistry& assets)
{
    if (!mpImpl || !mpImpl->renderDevice)
    {
        return;  // 未 Initialize：silent-ignore（与 SetIblTextures 同节奏）
    }
    auto& rhi = mpImpl->renderDevice->GetRhiDevice();

    // ---- 1. 扫 first-found EnvironmentComponent ----
    auto&       reg     = world.Registry();
    const auto  envView = reg.view<EnvironmentComponent>();
    if (envView.empty())
    {
        // 无 EnvironmentComponent → 切回 dummy IBL；释放之前烘焙的产物
        ORANGE_LOG_INFO("Pipeline::BakeIblFromWorld: 未找到 EnvironmentComponent，"
                        "回退到 dummy IBL");
        mpImpl->bakedIrradianceCube.reset();
        mpImpl->bakedPrefilteredCube.reset();
        mpImpl->bakedBrdfLut.reset();
        mpImpl->bakedEnvCube.reset();
        mpImpl->skySetBoundCube = nullptr;
        mpImpl->lastBakedCubemap = {};
        SetIblTextures(nullptr, nullptr, nullptr);
        return;
    }
    const auto& env = envView.get<EnvironmentComponent>(envView.front());
    if (!env.cubemap.IsValid())
    {
        ORANGE_LOG_WARN("Pipeline::BakeIblFromWorld: EnvironmentComponent.cubemap "
                        "handle 无效，回退到 dummy IBL");
        mpImpl->bakedIrradianceCube.reset();
        mpImpl->bakedPrefilteredCube.reset();
        mpImpl->bakedBrdfLut.reset();
        mpImpl->bakedEnvCube.reset();
        mpImpl->skySetBoundCube = nullptr;
        mpImpl->lastBakedCubemap = {};
        SetIblTextures(nullptr, nullptr, nullptr);
        return;
    }

    // ---- 2. 拿 TextureAsset + 校验格式 ----
    const ::Orange::Engine::Asset::TextureAsset* equirectAsset =
        assets.Get<::Orange::Engine::Asset::TextureAsset>(env.cubemap);
    if (equirectAsset == nullptr)
    {
        ORANGE_LOG_WARN("Pipeline::BakeIblFromWorld: AssetRegistry::Get<TextureAsset> "
                        "返回 nullptr（handle 失效？），回退到 dummy IBL");
        SetIblTextures(nullptr, nullptr, nullptr);
        return;
    }
    if (equirectAsset->Format() != ::Orange::Engine::Asset::TextureFormat::R32G32B32A32_Float)
    {
        ORANGE_LOG_WARN("Pipeline::BakeIblFromWorld: cubemap 资产格式不是 R32G32B32A32_Float "
                        "（当前烘焙路径只接 HDR equirect float32 RGBA），回退到 dummy IBL");
        SetIblTextures(nullptr, nullptr, nullptr);
        return;
    }
    if (equirectAsset->Empty() || equirectAsset->Width() == 0 || equirectAsset->Height() == 0)
    {
        ORANGE_LOG_WARN("Pipeline::BakeIblFromWorld: cubemap 资产像素数据为空，"
                        "回退到 dummy IBL");
        SetIblTextures(nullptr, nullptr, nullptr);
        return;
    }

    const std::uint32_t equirectW = equirectAsset->Width();
    const std::uint32_t equirectH = equirectAsset->Height();
    const auto&         pixels    = equirectAsset->Pixels();
    const std::size_t   pixelBytes = pixels.size();
    // 防御性检查：byte buffer 大小应为 w*h*16（RGBA32Float = 16 bytes/px）
    if (pixelBytes != static_cast<std::size_t>(equirectW)
                    * static_cast<std::size_t>(equirectH)
                    * ::Orange::Engine::Asset::BytesPerPixel(
                        ::Orange::Engine::Asset::TextureFormat::R32G32B32A32_Float))
    {
        ORANGE_LOG_WARN("Pipeline::BakeIblFromWorld: equirect byte size {} 与 {}x{}x16 不符，"
                        "回退到 dummy IBL", pixelBytes, equirectW, equirectH);
        SetIblTextures(nullptr, nullptr, nullptr);
        return;
    }

    // ---- 3. 创建 equirect RHITexture (Tex2D RGBA32Float) ----
    Orange::Rhi::TextureDesc equirectDesc{};
    equirectDesc.mWidth       = equirectW;
    equirectDesc.mHeight      = equirectH;
    equirectDesc.mFormat      = Orange::Rhi::TextureFormat::RGBA32Float;
    equirectDesc.mDimension   = Orange::Rhi::TextureDimension::Tex2D;
    equirectDesc.mArrayLayers = 1u;
    equirectDesc.mUsage       = Orange::Rhi::TextureUsage::Sampled
                              | Orange::Rhi::TextureUsage::TransferDst;
    auto equirectRhi = rhi.CreateTexture(equirectDesc);
    if (!equirectRhi)
    {
        ORANGE_LOG_ERROR("Pipeline::BakeIblFromWorld: CreateTexture(equirect {}x{}) 失败，"
                         "回退到 dummy IBL", equirectW, equirectH);
        SetIblTextures(nullptr, nullptr, nullptr);
        return;
    }

    // staging buffer → equirect upload。RGBA32F 在 desktop GPU 上 1K HDRI
    // 典型 8 MB（2048×1024×16），4K 32 MB；单次启动期分配可以接受，无须
    // 复用 UploadContext 的 ring buffer。
    Orange::Rhi::BufferDesc stagingDesc{};
    stagingDesc.mSize        = pixelBytes;
    stagingDesc.mUsage       = Orange::Rhi::BufferUsage::Transfer;
    stagingDesc.mMemoryUsage = Orange::Rhi::MemoryUsage::CpuToGpu;
    auto staging = rhi.CreateBuffer(stagingDesc);
    if (!staging)
    {
        ORANGE_LOG_ERROR("Pipeline::BakeIblFromWorld: 创建 staging buffer ({} bytes) 失败",
                         pixelBytes);
        SetIblTextures(nullptr, nullptr, nullptr);
        return;
    }
    {
        void* mapped = staging->Map();
        if (mapped == nullptr)
        {
            ORANGE_LOG_ERROR("Pipeline::BakeIblFromWorld: staging buffer Map 失败");
            SetIblTextures(nullptr, nullptr, nullptr);
            return;
        }
        std::memcpy(mapped, pixels.data(), pixelBytes);
        staging->Unmap();
    }

    // 一次性 transfer cmd list —— 与 dummy IBL Initialize 路径同款；
    // offscreenCmd 此刻不在 frame loop 内，可以独立 Begin/End/Submit。
    auto& cmd = *mpImpl->offscreenCmd;
    if (cmd.Begin() != Orange::ResultCode::Success)
    {
        ORANGE_LOG_ERROR("Pipeline::BakeIblFromWorld: cmd.Begin 失败");
        SetIblTextures(nullptr, nullptr, nullptr);
        return;
    }
    cmd.TransitionTexture(*equirectRhi,
                          Orange::Rhi::TextureLayout::Undefined,
                          Orange::Rhi::TextureLayout::TransferDst);
    {
        Orange::Rhi::BufferTextureCopyRegion r{};
        r.mBufferOffset = 0;
        r.mMipLevel     = 0;
        r.mArrayLayer   = 0;
        r.mWidth        = equirectW;
        r.mHeight       = equirectH;
        r.mDepth        = 1;
        cmd.CopyBufferToTexture(*staging, *equirectRhi, r);
    }
    cmd.TransitionTexture(*equirectRhi,
                          Orange::Rhi::TextureLayout::TransferDst,
                          Orange::Rhi::TextureLayout::ShaderReadOnly);
    if (cmd.End() != Orange::ResultCode::Success)
    {
        ORANGE_LOG_ERROR("Pipeline::BakeIblFromWorld: cmd.End 失败");
        SetIblTextures(nullptr, nullptr, nullptr);
        return;
    }
    if (rhi.SubmitCommandList(cmd) != Orange::ResultCode::Success)
    {
        ORANGE_LOG_ERROR("Pipeline::BakeIblFromWorld: SubmitCommandList 失败");
        SetIblTextures(nullptr, nullptr, nullptr);
        return;
    }
    // 等 GPU 落盘后让 staging buffer 安全析构。0.x 阶段同步等待是惯例。
    mpImpl->renderDevice->WaitIdle();

    // ---- 4. IblBaker 三件套 ----
    // BakeEquirectToCube 输入 RHITexture 必须处于 ShaderResource 状态，上面
    // 一段 transition 已经满足。cubeFaceSize 512：与 Lumix / Filament 默认
    // 一致，HDR scene reflection 视觉收敛足够；后续若想拉到 1024 加 Pipeline
    // 参数即可。
    IblBaker baker(rhi, *mpImpl->assets);

    auto envCube = baker.BakeEquirectToCube(*equirectRhi, 512u);
    if (!envCube)
    {
        ORANGE_LOG_ERROR("Pipeline::BakeIblFromWorld: BakeEquirectToCube 失败，"
                         "回退到 dummy IBL");
        SetIblTextures(nullptr, nullptr, nullptr);
        return;
    }
    auto irradianceCube = baker.BakeIrradiance(*envCube);
    if (!irradianceCube)
    {
        ORANGE_LOG_ERROR("Pipeline::BakeIblFromWorld: BakeIrradiance 失败");
        SetIblTextures(nullptr, nullptr, nullptr);
        return;
    }
    // sampleCount 显式 4096（IblBaker 默认 1024）。1024 samples 在 HDR equirect
    // 含高动态范围亮斑（如太阳盘）+ 中 roughness 段（cone 半角 ~30°）撞上
    // 个别 bright pixel 时会留下"白方块"采样伪影；4096 把每像素 GGX 卷积
    // 噪声推到肉眼不可见量级，bake 时间是一次性启动期开销，可接受。
    auto prefilteredCube = baker.BakePrefilteredEnvironment(*envCube, 256u, 9u, 4096u);
    if (!prefilteredCube)
    {
        ORANGE_LOG_ERROR("Pipeline::BakeIblFromWorld: BakePrefilteredEnvironment 失败");
        SetIblTextures(nullptr, nullptr, nullptr);
        return;
    }
    auto brdfLut = baker.BakeBrdfLut();
    if (!brdfLut)
    {
        ORANGE_LOG_ERROR("Pipeline::BakeIblFromWorld: BakeBrdfLut 失败");
        SetIblTextures(nullptr, nullptr, nullptr);
        return;
    }

    // ---- 5. 移动到 Impl + SetIblTextures 接通 PBR shader ----
    // 顺便保存原始 envCube 给 sky-dome pass 采样（未经 GGX 卷积的"真"环境
    // 贴图）。skySetBoundCube 重置让下帧 EnsureSkyDescSet 重新写 binding。
    mpImpl->bakedEnvCube         = std::move(envCube);
    mpImpl->skySetBoundCube      = nullptr;
    mpImpl->bakedIrradianceCube  = std::move(irradianceCube);
    mpImpl->bakedPrefilteredCube = std::move(prefilteredCube);
    mpImpl->bakedBrdfLut         = std::move(brdfLut);
    mpImpl->lastBakedCubemap     = env.cubemap;
    SetIblTextures(mpImpl->bakedIrradianceCube.get(),
                   mpImpl->bakedPrefilteredCube.get(),
                   mpImpl->bakedBrdfLut.get());

    ORANGE_LOG_INFO("Pipeline::BakeIblFromWorld: IBL 三件套烘焙完成 "
                    "(equirect {}x{} → cube 512 / irradiance 32 / prefilter 256 (9 mips) / brdfLut)",
                    equirectW, equirectH);

    // envCube / equirectRhi / baker 在 scope 结束自动析构（中间产物，三件套
    // 不再依赖它们）。
}

void Pipeline::SetFrameTime(float seconds) noexcept
{
    if (!mpImpl)
    {
        return;
    }
    mpImpl->frameTime = seconds;
    // 仅缓存；实际写入 LightUbo 发生在 Render() 内的 UpdateLightUbo。
}

void Pipeline::RequestCapture(const std::filesystem::path& outPath)
{
    if (!mpImpl)
    {
        return;
    }
    // 重复请求覆盖：本帧只兑现最后一次。
    mpImpl->pendingCapturePath = outPath;
}

// Helpers expecting Pipeline::Impl access live as friend free functions
// declared inside the class — keep this anonymous namespace empty.
namespace
{

// 离屏段开始之前先把所有 mesh 上传到 GPU——UploadContext 的内部
// transient cmd 必须在 offscreenCmd.Begin 之前完成。
[[maybe_unused]] void EnsureMeshGpuCacheStub() {}

}  // namespace

bool Pipeline::Impl::RecordOffscreenPass(const glm::mat4& viewProj, bool loadColor)
{
    ORANGE_PROFILE_SCOPE("MainPass");
    auto& impl = *this;
    auto& cmd = *impl.offscreenCmd;
    // Caller (Render) 已经 cmd.Begin() —— 这里只录制主 pass + transition，
    // 后续 bloom 链 / End / Submit 由 Render 顶层负责，让所有 GPU 工作进
    // 入同一 cmd list 同一 Submit。

    // loadColor == true：sky pass 已经把 hdrColor 留在 ColorAttachment，
    // 直接 LoadOp::Load 接住，不重复 transition / clear。
    if (!loadColor)
    {
        const auto fromLayout = impl.hdrLayoutShaderReadOnly
            ? Orange::Rhi::TextureLayout::ShaderReadOnly
            : Orange::Rhi::TextureLayout::Undefined;
        cmd.TransitionTexture(*impl.hdrColor, fromLayout,
                              Orange::Rhi::TextureLayout::ColorAttachment);
    }
    // sceneDepth 每帧 transition → DepthStencilAttachment（每帧 Clear，
    // 几何顺序无关性靠 depth test 保证）。GodRaysPass 启用时上一帧末尾
    // 把它翻到 ShaderReadOnly（采样作 occlusion proxy），未启用时还是
    // 上一帧的 DSA / 首次 Undefined。
    const auto fromDepthLayout = impl.sceneDepthLayoutShaderReadOnly
        ? Orange::Rhi::TextureLayout::ShaderReadOnly
        : Orange::Rhi::TextureLayout::Undefined;
    cmd.TransitionTexture(*impl.sceneDepth,
                          fromDepthLayout,
                          Orange::Rhi::TextureLayout::DepthStencilAttachment);
    impl.sceneDepthLayoutShaderReadOnly = false;

    Orange::Rhi::ColorAttachment att{};
    att.mpView          = impl.hdrColor->GetDefaultView();
    att.mLoadOp         = loadColor ? Orange::Rhi::LoadOp::Load
                                    : Orange::Rhi::LoadOp::Clear;
    att.mStoreOp        = Orange::Rhi::StoreOp::Store;
    // v1.3.0 中性化：clear 色不再 cmake gate，从 impl.sceneClearColor 字段读
    // —— engine 默认 (0.05, 0.07, 0.10) 深蓝灰（shipping 中性 / 与 sample
    // 01/03/04 早期视觉一致），编辑器 / 游戏端按需通过公共 API
    // Pipeline::SetSceneClearColor 提到自家美学量级（OrangeEditor 默认
    // (0.12, 0.12, 0.13) Cocos 风灰，让 viewport 与 main panel 深炭灰拉开
    // 亮度差便于辨识渲染区）。hdrColor 是线性 HDR target，写入值经 tonemap
    // 出到 swapchain；线性 0.12 对应感知 ~0.36 / sRGB ~0.39（取 1/2.2 power）。
    att.mClear.mColor[0] = impl.sceneClearColor.x;
    att.mClear.mColor[1] = impl.sceneClearColor.y;
    att.mClear.mColor[2] = impl.sceneClearColor.z;
    att.mClear.mColor[3] = 1.0f;

    Orange::Rhi::DepthStencilAttachment depthAtt{};
    depthAtt.mpView        = impl.sceneDepth->GetDefaultView();
    depthAtt.mDepthLoadOp  = Orange::Rhi::LoadOp::Clear;
    // depth 用 Store 而不是 DontCare —— GodRaysPass 在主 pass 之后采样
    // sceneDepth 作 occlusion proxy（depth ≈ 1 → sun-visible），需要主
    // pass 写出来的 depth 值在 EndRendering 之后仍可读。GodRaysPass 没
    // 启用时这条 Store 等同于"白白保留一份 depth 数据"，对主 pass 没
    // 实际副作用。
    depthAtt.mDepthStoreOp = Orange::Rhi::StoreOp::Store;
    depthAtt.mClear.mDepth = 1.0f;                              // far plane

    Orange::Rhi::RenderingDesc rd{};
    rd.mRenderArea.mWidth  = impl.hdrWidth;
    rd.mRenderArea.mHeight = impl.hdrHeight;
    rd.mColorAttachments.push_back(att);
    rd.mDepthStencil       = depthAtt;

    cmd.BeginRendering(rd);

    Orange::Rhi::RHIViewport vp{};
    vp.mWidth    = static_cast<float>(impl.hdrWidth);
    vp.mHeight   = static_cast<float>(impl.hdrHeight);
    vp.mMinDepth = 0.0f;
    vp.mMaxDepth = 1.0f;
    cmd.SetViewport(vp);
    Orange::Rhi::RHIScissor sc{};
    sc.mWidth  = impl.hdrWidth;
    sc.mHeight = impl.hdrHeight;
    cmd.SetScissor(sc);

    Orange::Rhi::RHIPipeline* pLastPipeline = nullptr;

    for (const auto& drawable : impl.scene.Drawables())
    {
        if (!drawable.mesh.IsValid())
        {
            continue;
        }
        const std::uint64_t key = drawable.mesh.Value();
        auto cacheIt = impl.meshCache.find(key);
        if (cacheIt == impl.meshCache.end())
        {
            continue;
        }

        const Material* mat = nullptr;
        if (drawable.materialInstance != nullptr)
        {
            mat = drawable.materialInstance->GetMaterial();
        }
        if (mat == nullptr)
        {
            mat = impl.EnsureBuiltinDefaultMaterial();
        }
        if (mat == nullptr)
        {
            continue;
        }
        Orange::Rhi::RHIPipeline* rhiPipeline = impl.GetOrCompilePipeline(*mat);
        if (rhiPipeline == nullptr)
        {
            continue;
        }

        if (rhiPipeline != pLastPipeline)
        {
            cmd.BindGraphicsPipeline(*rhiPipeline);
            pLastPipeline = rhiPipeline;

            // 主 pass 每次 BindGraphicsPipeline 后必须重新 SetDescriptorSet
            // —— pipeline 切换可能让上一次绑定失效（layout 不兼容时）。
            // mainDescSet 一旦绑过 binding 0/1，跨 drawable 内容稳定。
            if (mainDescSet)
            {
                cmd.SetDescriptorSet(0, *mainDescSet);
            }
        }

        // set 1 = per-instance material 贴图（仅 PBR 模板，GAP-2026-05-25 A2/G1）。
        // per-draw 绑定——不同 MaterialInstance 用不同 set。预通道
        // EnsureMaterialDescriptors 已 build + 上传贴图，这里命中缓存（不触
        // GPU 写，录制期安全）；非 PBR 模板（textured/toon/...）无 set 1，跳过。
        if (MaterialUsesTextureSet(*mat))
        {
            Orange::Rhi::RHIDescriptorSet* matSet =
                EnsureMaterialDescriptorSet(drawable.materialInstance);
            if (matSet != nullptr)
            {
                cmd.SetDescriptorSet(1, *matSet);
            }
        }

        // push constant 按 Material.uniforms 推算的尺寸打包。
        //   *  64 B → uMVP 单独（textured-only schema，已被 fallback 切走，
        //              保留兼容外部 sample 自定义 schema）
        //   * 128 B → uMVP + uModel（toon / rim_light / dissolve / emissive /
        //              内置 textured 实际 schema）
        //   * 160 B → uMVP + uModel + uBaseColor + uMRA（pbr）
        // 其他尺寸为半残 schema，按 64 B 处理。
        const glm::mat4 mvp = viewProj * drawable.worldMatrix;
        const std::uint32_t pcSize = ComputePushConstantSize(*mat);
        if (pcSize >= 160)
        {
            // PBR 路径：MaterialInstance 的 uBaseColor / uMRA override 优先；
            // 缺省时 fallback 到 PBR 中性默认（与 BuiltinMaterials::LoadPbr
            // 注释一致：灰塑料 + 非金属 + 中等粗糙 + AO 满）。
            struct PushPbr {
                glm::mat4 mvp;
                glm::mat4 model;
                glm::vec4 baseColor;
                glm::vec4 mra;
            };
            PushPbr data{};
            data.mvp       = mvp;
            data.model     = drawable.worldMatrix;
            data.baseColor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
            data.mra       = glm::vec4(0.0f, 0.5f, 1.0f, 0.0f);
            if (drawable.materialInstance != nullptr)
            {
                if (auto over = drawable.materialInstance->GetUniformVec4("uBaseColor"))
                {
                    data.baseColor = *over;
                }
                if (auto over = drawable.materialInstance->GetUniformVec4("uMRA"))
                {
                    data.mra = *over;
                }
            }
            cmd.SetPushConstants(Orange::Rhi::ShaderStage::Vertex,
                                 /*offset=*/0,
                                 /*size=*/sizeof(PushPbr),
                                 &data);
        }
        else if (pcSize >= 128)
        {
            struct PushMvpModel { glm::mat4 mvp; glm::mat4 model; };
            PushMvpModel data{};
            data.mvp   = mvp;
            data.model = drawable.worldMatrix;
            cmd.SetPushConstants(Orange::Rhi::ShaderStage::Vertex,
                                 /*offset=*/0,
                                 /*size=*/128,
                                 &data);
        }
        else
        {
            cmd.SetPushConstants(Orange::Rhi::ShaderStage::Vertex,
                                 /*offset=*/0,
                                 static_cast<std::uint32_t>(sizeof(glm::mat4)),
                                 &mvp);
        }

        const auto& gpu = cacheIt->second;
        cmd.BindVertexBuffer(0, *gpu.vertexBuffer, /*offset=*/0);
        cmd.BindIndexBuffer(*gpu.indexBuffer, /*offset=*/0,
                            Orange::Rhi::IndexFormat::UInt32);
        cmd.DrawIndexed(gpu.indexCount, /*instanceCount=*/1,
                        /*firstIndex=*/0, /*vertexOffset=*/0,
                        /*firstInstance=*/0);
    }

    cmd.EndRendering();

    cmd.TransitionTexture(*impl.hdrColor,
                          Orange::Rhi::TextureLayout::ColorAttachment,
                          Orange::Rhi::TextureLayout::ShaderReadOnly);
    impl.hdrLayoutShaderReadOnly = true;
    return true;
}

bool Pipeline::Impl::RecordPassthroughToViewport()
{
    auto& impl = *this;
    if (!impl.viewportColor)
    {
        return false;
    }
    auto& cmd = *impl.offscreenCmd;

    // viewportColor 初始或上一帧末翻到 ShaderReadOnly；现在写回 ColorAttachment。
    // 首帧 viewportLayoutShaderReadOnly == false（Undefined），与 hdrColor 同模式。
    const auto fromLayout = impl.viewportLayoutShaderReadOnly
        ? Orange::Rhi::TextureLayout::ShaderReadOnly
        : Orange::Rhi::TextureLayout::Undefined;
    cmd.TransitionTexture(*impl.viewportColor, fromLayout,
                          Orange::Rhi::TextureLayout::ColorAttachment);

    Orange::Rhi::ColorAttachment att{};
    att.mpView           = impl.viewportColor->GetDefaultView();
    att.mLoadOp          = Orange::Rhi::LoadOp::Clear;
    att.mStoreOp         = Orange::Rhi::StoreOp::Store;
    att.mClear.mColor[0] = 0.0f;
    att.mClear.mColor[1] = 0.0f;
    att.mClear.mColor[2] = 0.0f;
    att.mClear.mColor[3] = 1.0f;

    Orange::Rhi::RenderingDesc rd{};
    rd.mRenderArea.mWidth  = impl.viewportWidth;
    rd.mRenderArea.mHeight = impl.viewportHeight;
    rd.mColorAttachments.push_back(att);
    cmd.BeginRendering(rd);

    Orange::Rhi::RHIViewport vp{};
    vp.mWidth    = static_cast<float>(impl.viewportWidth);
    vp.mHeight   = static_cast<float>(impl.viewportHeight);
    vp.mMinDepth = 0.0f;
    vp.mMaxDepth = 1.0f;
    cmd.SetViewport(vp);
    Orange::Rhi::RHIScissor sc{};
    sc.mWidth  = impl.viewportWidth;
    sc.mHeight = impl.viewportHeight;
    cmd.SetScissor(sc);

    // bloom 接 offscreen（编辑器视口 WYSIWYG）：活动 BloomPass + bloom mip 就绪时
    // 走 tonemap 合成（HDR + bloom → ACES，复用窗口 tonemapPipeline + bloomCombineSet；
    // viewportColor 与 swap-chain 同 BGRA8Unorm，格式合法）。否则纯 passthrough（ACES）。
    // exposure=1（与 passthrough.frag 的隐式曝光一致）。
    const BloomPass* bloomPass = impl.FindActiveBloomPass();
    if (bloomPass != nullptr && impl.bloomMipsReady && impl.bloomCombineSet
        && impl.tonemapPipeline)
    {
        struct PushTonemap { float exposure; float bloomIntensity; float pad0; float pad1; };
        PushTonemap pcData{};
        pcData.exposure       = 1.0f;
        pcData.bloomIntensity = bloomPass->intensity;
        // 必须先 BindGraphicsPipeline 再 SetPushConstants —— push constant 落到
        // 当前已绑 pipeline 的 layout 上；先 push 会落到上一个（主 pass mesh，
        // Vertex/64）的 layout 造成 stage/size 不匹配 validation error。
        cmd.BindGraphicsPipeline(*impl.tonemapPipeline);
        cmd.SetPushConstants(Orange::Rhi::ShaderStage::Fragment, 0,
                             static_cast<std::uint32_t>(sizeof(pcData)), &pcData);
        cmd.SetDescriptorSet(0, *impl.bloomCombineSet);
        cmd.Draw(3, 1, 0, 0);
    }
    else
    {
        cmd.BindGraphicsPipeline(*impl.passthroughPipeline);
        cmd.SetDescriptorSet(0, *impl.passthroughSet);
        cmd.Draw(3, 1, 0, 0);  // big-triangle，fullscreen.vert 走 gl_VertexIndex
    }
    cmd.EndRendering();

    cmd.TransitionTexture(*impl.viewportColor,
                          Orange::Rhi::TextureLayout::ColorAttachment,
                          Orange::Rhi::TextureLayout::ShaderReadOnly);
    impl.viewportLayoutShaderReadOnly = true;
    return true;
}

void Pipeline::Impl::RenderOffscreen(Orange::Engine::World& world)
{
    auto& impl = *this;

    // 0. 同步 HDR + viewport RT（ResizeOffscreen 标过 dirty 时这里重建）。
    const bool hdrReady      = impl.EnsureHdrTarget();
    const bool viewportReady = impl.EnsureViewportTarget();
    if (!hdrReady || !viewportReady)
    {
        ++impl.frameIndex;
        return;
    }

    // 0.5 PostProcessComponent 同步：每帧从 world 找全局 post 组件 → 填 post*
    // 成员 + 灌 shadowConfig(PCSS/分辨率)。须在 EnsureShadowMap 之前（mapResolution
    // 可能变）。无组件时 postComponentActive=false，FindActive* 退回 chain。
    impl.SyncPostProcessFromWorld(world);

    // 1. mesh GPU 上传（UploadContext 的 transient cmd 必须先于 offscreenCmd.Begin）。
    if (impl.scene.HasCamera())
    {
        impl.EnsureMeshGpuCache();
        // set 1 material 贴图上传 + descriptor build（同样必须在录制前，
        // 走 UploadContext；录制期 draw loop 只命中缓存）。
        impl.EnsureMaterialDescriptors();
    }

    // 1.5 Shadow / Light 准备：找 DirectionalLight + 写 light UBO + 确保
    // shadow map 已建好。无 light 场景仍写默认 light UBO（rim_light 等
    // fragment 才有合理 base 着色），shadow map 走"远深度清零 + 不画
    // caster"路径（PCF 取 1.0 = 全亮）。
    const DirectionalLight* activeLight = nullptr;
    glm::vec3               activeLightDir{0.3f, -1.0f, 0.4f};  // neutral 默认
    glm::vec3               iblTintIntensity{1.0f, 1.0f, 1.0f}; // 未挂 EnvironmentComponent → 1,1,1（中性）
    glm::vec3               envTint{1.0f, 1.0f, 1.0f};
    float                   envIntensity = 1.0f;
    glm::vec3               cameraWorldPos{0.0f};
    if (impl.scene.HasCamera())
    {
        auto& reg  = world.Registry();
        auto  view = reg.view<DirectionalLight>();
        if (!view.empty())
        {
            const auto entity = view.front();
            activeLight = &view.get<DirectionalLight>(entity);
            // 方向由 entity.Transform.rotation 派生；没挂 Transform 视为
            // identity rotation（光向下 -Y）。Pipeline 不感知"哪是 forward"
            // 约定细节，全部走 LightComponent.h 的统一公式。
            using TC = Orange::Engine::Scene::TransformComponent;
            const auto* tc = reg.try_get<TC>(entity);
            const glm::quat rot = (tc != nullptr) ? tc->rotation : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            activeLightDir = ComputeDirectionalLightWorldDir(rot);
        }
        // EnvironmentComponent first-found：与 DirectionalLight 同款选取；
        // 多个时取迭代器第一个（baseline 单 World 全局环境，多 environment
        // blending 留给后续 reflection probe milestone）。tint * intensity
        // 在 host 端先乘好喂 LightUbo，shader 侧 fragment 一次相乘；同时
        // envTint / envIntensity 单独留下给 sky-dome pass 用（sky shader
        // 内部数学上等价于 `sky * tint * intensity`，但保留两个量可读性更好）。
        auto envView = reg.view<EnvironmentComponent>();
        if (!envView.empty())
        {
            const auto&  env = envView.get<EnvironmentComponent>(envView.front());
            envTint          = env.tint;
            envIntensity     = env.intensity;
            iblTintIntensity = env.tint * env.intensity;
        }
        impl.EnsureShadowMap();
        impl.EnsureSpotShadowArray();
        impl.EnsurePointShadowCube();
        const glm::mat4 lightVP   = activeLight ? impl.ComputeLightViewProj(activeLightDir)
                                                : glm::mat4(1.0f);
        const glm::mat4 invView   = glm::inverse(impl.scene.MainCamera().view);
        cameraWorldPos            = glm::vec3(invView[3]);
        impl.UpdateLightUbo(activeLight, activeLightDir, lightVP, cameraWorldPos, iblTintIntensity);
        impl.UpdatePointLightsUbo(world);
        impl.UpdateSpotLightsUbo(world);
    }

    // 2. 一次 cmd list 包含：shadow → (sky) → 主 pass → (grid) → passthrough → 翻 layout。
    auto& cmd = *impl.offscreenCmd;
    if (Orange::Failed(cmd.Begin()))
    {
        ORANGE_LOG_ERROR("Pipeline::RenderOffscreen: cmd Begin 失败 (frame={})",
                         impl.frameIndex);
        ++impl.frameIndex;
        return;
    }

    bool ok = true;
    if (impl.scene.HasCamera())
    {
        // TAA 激活时给投影叠加 per-frame sub-pixel jitter（法线预通道 + 主 pass
        // 用此 jittered viewProj；resolve 用同一矩阵自洽重投影）。未激活原样返回。
        const glm::mat4 viewProj = impl.ApplyTaaJitter(impl.scene.MainCamera().projection)
                                 * impl.scene.MainCamera().view;
        const glm::mat4 invViewProj = glm::inverse(viewProj);
        // 未 jitter 的 viewProj —— 供 overlay pass（aux/grid + debug draw）使用。
        // TAA 把 jitter 加在场景几何上（多帧 resolve 抹平），但 overlay 在 TAA
        // 之后画、不进 resolve；若用 jittered viewProj 会逐帧 sub-pixel 抖动
        // （grid/gizmo shimmer）。未激活 TAA 时 base == jittered，等价。
        const glm::mat4 baseViewProj    = impl.scene.MainCamera().projection
                                        * impl.scene.MainCamera().view;
        const glm::mat4 invBaseViewProj = glm::inverse(baseViewProj);
        const glm::mat4 lightVP  = activeLight ? impl.ComputeLightViewProj(activeLightDir)
                                               : glm::mat4(1.0f);

        // shadow pre-pass：directional 单张 2D map + spot 透视 shadow array。
        if (impl.shadowMap)
        {
            ok = impl.RecordShadowPass(activeLight, lightVP);
        }
        if (impl.spotShadowArray)
        {
            impl.RecordSpotShadowPass();
        }
        if (impl.pointShadowCubes[0])
        {
            impl.RecordPointShadowPass();
        }

        // sky pass：主 pass 之前画背景。两种分支：
        //   (a) bakedEnvCube 有 → cubemap sky（采 EnvironmentComponent.cubemap）
        //   (b) bakedEnvCube 无 + skyEnabled → procedural sky（3 色 gradient
        //       + 太阳 disc，无资源依赖；Cocos / Godot / Unity HDRP 默认天空
        //       同思路）
        // skyEnabled = false → 都跳过，主 pass clear color fallback。
        bool skyDrew = false;
        if (ok && impl.skyEnabled)
        {
            // sunDir 指向太阳的方向 = -DirectionalLight.dir（光是从太阳
            // 出射的方向，太阳本身在反方向）。无 active light 时取一个
            // 正午偏南默认（让用户即使删了 DirectionalLight 也有视觉锚点）。
            glm::vec3 sunDir = (activeLight != nullptr)
                ? -glm::normalize(activeLightDir)
                : glm::normalize(glm::vec3(0.4f, 1.0f, 0.3f));
            glm::vec3 sunColor = (activeLight != nullptr)
                ? activeLight->color
                : glm::vec3(1.0f, 0.95f, 0.85f);
            float sunIntensity = (activeLight != nullptr) ? activeLight->intensity : 1.0f;

            if (impl.bakedEnvCube)
            {
                skyDrew = impl.RecordSkyPass(invViewProj, cameraWorldPos,
                                             envTint, envIntensity);
            }
            else
            {
                skyDrew = impl.RecordProceduralSkyPass(invViewProj, cameraWorldPos,
                                                       sunDir, sunColor, sunIntensity);
            }
        }

        // 法线预通道：主 pass 之前渲 view-space 法线到 normalBuffer，供 SSAO /
        // SSR 采真实法线（替代深度差分）。只在二者之一激活时跑——复用 sceneDepth
        // 作 scratch depth（写完留 DSA，紧跟的主 pass 以 Undefined→DSA + Clear 自然
        // 丢弃），故必须紧贴主 pass 之前。
        if (ok && (impl.FindActiveSsaoPass() != nullptr || impl.FindActiveSsrPass() != nullptr
                   || impl.FindActiveContactShadowPass() != nullptr))
        {
            ok = impl.RecordNormalPrepass(viewProj, impl.scene.MainCamera().view);
        }

        // 主 HDR pass
        if (ok)
        {
            ok = impl.RecordOffscreenPass(viewProj, /*loadColor=*/skyDrew);
        }

        // 粒子 pass —— 与窗口模式路径对称，插在主 pass 之后、passthrough 之前。
        // VfxSystem 自管 HDR 的 ShaderReadOnly ↔ ColorAttachment 翻转。
        if (ok && impl.vfxSystem != nullptr
            && impl.vfxSystem->IsInitialized()
            && impl.hdrColor)
        {
            impl.vfxSystem->DrawParticles(
                impl.offscreenCmd.get(),
                impl.hdrColor->GetDefaultView(),
                /*pDepthView=*/nullptr,
                glm::value_ptr(viewProj),
                impl.frameIndex,
                impl.hdrWidth,
                impl.hdrHeight);
        }

        // SSAO / SSR：主 pass + 粒子之后、grid / aux 之前 —— 直接合成进
        // hdrColor 的 post pass（无需 stage-B tonemap），让编辑器 offscreen
        // 视口也能所见即所得地显示环境光遮蔽 + 反射。放在 aux/grid 前，使其
        // 只作用场景几何、不染编辑器 overlay。bloom/tonemap 因依赖 stage-B
        // combine 仍仅 window 模式（offscreen passthrough 暂不接，留作后续）。
        if (ok)
        {
            const glm::mat4 proj = impl.scene.MainCamera().projection;
            if (const SsaoPass* aoPass = impl.FindActiveSsaoPass())
            {
                ok = impl.RecordSsaoPass(*aoPass, proj);
            }
            if (ok)
            {
                if (const SsrPass* ssrPass = impl.FindActiveSsrPass())
                {
                    ok = impl.RecordSsrPass(*ssrPass, proj);
                }
            }
            // 接触阴影：SSAO/SSR 之后、god rays（加性）之前——乘法暗化要先于
            // 加光。仅 directional light 时跑（朝光向 = view 空间的 -lightDir）。
            if (ok && activeLight != nullptr)
            {
                if (const ContactShadowPass* csPass = impl.FindActiveContactShadowPass())
                {
                    const glm::vec3 viewL = glm::normalize(
                        glm::mat3(impl.scene.MainCamera().view) * (-activeLightDir));
                    ok = impl.RecordContactShadowPass(*csPass, proj, viewL);
                }
            }
            // 景深：在加性 god rays 之前——先确定对焦/虚化的场景色，god rays
            // 光束再叠加（保持光束锐利）。
            if (ok)
            {
                if (const DofPass* dofPass = impl.FindActiveDofPass())
                {
                    ok = impl.RecordDofPass(*dofPass, proj);
                }
            }
            // Bloom：建资源 + 录 mip 链（offscreen 也接 bloom → 编辑器视口 WYSIWYG）。
            // 位置与 window 路径一致（DoF 之后、god rays 之前）；RecordPassthroughToViewport
            // 据 bloomMipsReady + 活动 BloomPass 决定走 tonemap 合成还是纯 passthrough。
            if (ok)
            {
                if (const BloomPass* bloomPass = impl.FindActiveBloomPass())
                {
                    if (impl.EnsureBloomResources())
                    {
                        ok = impl.RecordBloomChain(*bloomPass);
                    }
                }
            }
            if (ok)
            {
                if (const GodRaysPass* grPass = impl.FindActiveGodRaysPass())
                {
                    ok = impl.RecordGodRaysPass(*grPass, viewProj);
                }
            }
            // TAA resolve：所有场景 post 之后、aux/grid overlay 之前——把当前帧
            // 与重投影历史混合去噪 + 抗锯齿。viewProj 是本帧 jittered 矩阵。
            if (ok)
            {
                if (const TaaPass* taaPass = impl.FindActiveTaaPass())
                {
                    ok = impl.RecordTaaResolve(*taaPass, viewProj);
                }
            }
            // 锐化（CAS）：紧接 TAA 之后恢复其软化的高频细节；放在 motion blur /
            // DoF 之前——那些 pass 之后会有意虚化，锐化它们的结果无意义。
            if (ok)
            {
                if (const SharpenPass* sharpenPass = impl.FindActiveSharpenPass())
                {
                    ok = impl.RecordSharpenPass(*sharpenPass);
                }
            }
            // 相机运动模糊：TAA 之后（TAA 已 resolve 出锐利帧，再沿相机速度拉糊）、
            // 色彩分级之前。用未 jitter 的 baseViewProj 算速度（不含 TAA 亚像素抖动）。
            if (ok)
            {
                if (const MotionBlurPass* mbPass = impl.FindActiveMotionBlurPass())
                {
                    ok = impl.RecordMotionBlurPass(*mbPass, baseViewProj);
                }
            }
            // 色彩分级：最终 look 调整，放在所有 post（含 TAA）之后、tonemap 之前。
            if (ok)
            {
                if (const ColorGradePass* gradePass = impl.FindActiveColorGradePass())
                {
                    ok = impl.RecordColorGradePass(*gradePass);
                }
            }
            // 镜头效果（色散 + 暗角）：最后的"镜头"阶段，所有 post（含色彩分级）
            // 之后、tonemap 之前。
            if (ok)
            {
                if (const LensPass* lensPass = impl.FindActiveLensPass())
                {
                    ok = impl.RecordLensPass(*lensPass);
                }
            }
        }

        // v1.3.0 · AuxPassProvider hook：主 pass + 粒子之后、debug draw /
        // passthrough 之前调用外部注册的辅助 pass（典型：editor 端 grid /
        // outline / wireframe / debug overlay）。Pipeline 在调用 hook 前
        // 统一把 sceneDepth transition 到 ShaderReadOnly，省去 provider
        // 自己 transition 逻辑；契约约定 provider 仅写 hdrColor + 读
        // sceneDepth，不改 depth layout（详见 IAuxPassProvider.h 调用约定）。
        if (ok && impl.pAuxPassProvider != nullptr && impl.offscreenCmd != nullptr
            && impl.hdrColor != nullptr && impl.sceneDepth != nullptr
            && impl.hdrSampler != nullptr)
        {
            if (!impl.sceneDepthLayoutShaderReadOnly)
            {
                cmd.TransitionTexture(*impl.sceneDepth,
                                      Orange::Rhi::TextureLayout::DepthStencilAttachment,
                                      Orange::Rhi::TextureLayout::ShaderReadOnly);
                impl.sceneDepthLayoutShaderReadOnly = true;
            }

            AuxPassContext ctx{};
            ctx.pCmd        = impl.offscreenCmd.get();
            ctx.pHdrColor   = impl.hdrColor.get();
            ctx.pSceneDepth = impl.sceneDepth.get();
            ctx.pHdrSampler = impl.hdrSampler.get();
            ctx.hdrWidth    = impl.hdrWidth;
            ctx.hdrHeight   = impl.hdrHeight;
            ctx.invViewProj = invBaseViewProj;  // overlay 用未 jitter（防 TAA shimmer）
            ctx.viewProj    = baseViewProj;
            ctx.sceneDepthIsShaderReadOnly = true;  // 契约固定 true（pre-transition）
            ctx.hdrColorFormat   = impl.hdrColor->GetDesc().mFormat;
            ctx.sceneDepthFormat = impl.sceneDepth->GetDesc().mFormat;
            ctx.pFullscreenVs    = impl.fullscreenVs.get();
            impl.pAuxPassProvider->RenderAuxPass(ctx);
            // 离开契约：hdrColor 仍 ShaderReadOnly + sceneDepth 仍 ShaderReadOnly。
        }

        // debug draw pass：grid / aux 之后、passthrough 之前。wrap 内自管
        // enabled / 空几何 silent skip；失败 silent，passthrough 继续。
        // 用未 jitter 的 baseViewProj（overlay 不进 TAA resolve，防 shimmer）。
        if (ok)
        {
            impl.RecordDebugDrawPass(baseViewProj);
        }

        // passthrough HDR → viewportColor
        if (ok)
        {
            ok = impl.RecordPassthroughToViewport();
        }
    }
    else
    {
        // 无相机：把 viewportColor 清成黑，避免 ImGui 采样到 Undefined。
        // hdrColor 不写也不读——这一帧主 pass 跳过。
        const auto fromLayout = impl.viewportLayoutShaderReadOnly
            ? Orange::Rhi::TextureLayout::ShaderReadOnly
            : Orange::Rhi::TextureLayout::Undefined;
        cmd.TransitionTexture(*impl.viewportColor, fromLayout,
                              Orange::Rhi::TextureLayout::ColorAttachment);
        Orange::Rhi::ColorAttachment att{};
        att.mpView           = impl.viewportColor->GetDefaultView();
        att.mLoadOp          = Orange::Rhi::LoadOp::Clear;
        att.mStoreOp         = Orange::Rhi::StoreOp::Store;
        att.mClear.mColor[3] = 1.0f;
        Orange::Rhi::RenderingDesc rd{};
        rd.mRenderArea.mWidth  = impl.viewportWidth;
        rd.mRenderArea.mHeight = impl.viewportHeight;
        rd.mColorAttachments.push_back(att);
        cmd.BeginRendering(rd);
        cmd.EndRendering();
        cmd.TransitionTexture(*impl.viewportColor,
                              Orange::Rhi::TextureLayout::ColorAttachment,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
        impl.viewportLayoutShaderReadOnly = true;
    }

    if (Orange::Failed(cmd.End()))
    {
        ORANGE_LOG_ERROR("Pipeline::RenderOffscreen: cmd End 失败 (frame={})",
                         impl.frameIndex);
        ++impl.frameIndex;
        return;
    }
    if (Orange::Failed(impl.renderDevice->GetRhiDevice().SubmitCommandList(cmd)))
    {
        ORANGE_LOG_ERROR("Pipeline::RenderOffscreen: SubmitCommandList 失败 (frame={})",
                         impl.frameIndex);
        ++impl.frameIndex;
        return;
    }
    if (Orange::Failed(impl.renderDevice->WaitIdle()))
    {
        ORANGE_LOG_ERROR("Pipeline::RenderOffscreen: WaitIdle 失败 (frame={})",
                         impl.frameIndex);
    }

    (void)ok;  // ok==false 也走到这里（cmd 已经 End 才能 Submit）；下一帧重来
    ++impl.frameIndex;
}

void Pipeline::Impl::EnsureMeshGpuCache()
{
    auto& impl = *this;
    auto& rhi = impl.renderDevice->GetRhiDevice();
    for (const auto& drawable : impl.scene.Drawables())
    {
        if (!drawable.mesh.IsValid())
        {
            continue;
        }
        const std::uint64_t key = drawable.mesh.Value();
        if (impl.meshCache.find(key) != impl.meshCache.end())
        {
            continue;
        }
        const Asset::MeshAsset* meshAsset =
            impl.assets ? impl.assets->Get(drawable.mesh) : nullptr;
        if (meshAsset == nullptr || meshAsset->Empty())
        {
            continue;
        }

        const auto vertices = InterleaveMesh(*meshAsset);
        const auto& indices = meshAsset->Indices();
        const std::uint64_t vertexBytes = vertices.size() * sizeof(InterleavedVertex);
        const std::uint64_t indexBytes  = indices.size()  * sizeof(std::uint32_t);
        if (vertexBytes == 0 || indexBytes == 0)
        {
            continue;
        }

        Pipeline::Impl::MeshGpu gpu;

        Orange::Rhi::BufferDesc vbDesc{};
        vbDesc.mSize        = vertexBytes;
        vbDesc.mUsage       = Orange::Rhi::BufferUsage::Vertex
                            | Orange::Rhi::BufferUsage::Transfer;
        vbDesc.mMemoryUsage = Orange::Rhi::MemoryUsage::GpuOnly;
        gpu.vertexBuffer = rhi.CreateBuffer(vbDesc);

        Orange::Rhi::BufferDesc ibDesc{};
        ibDesc.mSize        = indexBytes;
        ibDesc.mUsage       = Orange::Rhi::BufferUsage::Index
                            | Orange::Rhi::BufferUsage::Transfer;
        ibDesc.mMemoryUsage = Orange::Rhi::MemoryUsage::GpuOnly;
        gpu.indexBuffer  = rhi.CreateBuffer(ibDesc);

        if (!gpu.vertexBuffer || !gpu.indexBuffer)
        {
            ORANGE_LOG_ERROR("Pipeline::Render: CreateBuffer 失败 (mesh handle={})",
                             static_cast<unsigned long long>(key));
            continue;
        }

        if (Orange::Failed(impl.upload->UploadBuffer(*gpu.vertexBuffer, 0,
                                                      vertices.data(), vertexBytes)))
        {
            ORANGE_LOG_ERROR("Pipeline::Render: UploadBuffer (vertex) 失败");
            continue;
        }
        if (Orange::Failed(impl.upload->UploadBuffer(*gpu.indexBuffer, 0,
                                                      indices.data(), indexBytes)))
        {
            ORANGE_LOG_ERROR("Pipeline::Render: UploadBuffer (index) 失败");
            continue;
        }

        gpu.indexCount = static_cast<std::uint32_t>(indices.size());
        impl.meshCache.emplace(key, std::move(gpu));
    }
}

const BloomPass* Pipeline::Impl::FindActiveBloomPass() const noexcept
{
    if (postProcessChain == nullptr)
    {
        return nullptr;
    }
    const std::size_t count = postProcessChain->PassCount();
    for (std::size_t i = 0; i < count; ++i)
    {
        const IPostProcessPass* p = postProcessChain->PassAt(i);
        if (const BloomPass* bp = dynamic_cast<const BloomPass*>(p))
        {
            return bp;
        }
    }
    return nullptr;
}

const TonemapPass* Pipeline::Impl::FindActiveTonemapPass() const noexcept
{
    if (postProcessChain == nullptr)
    {
        return nullptr;
    }
    const std::size_t count = postProcessChain->PassCount();
    for (std::size_t i = 0; i < count; ++i)
    {
        const IPostProcessPass* p = postProcessChain->PassAt(i);
        if (const TonemapPass* tp = dynamic_cast<const TonemapPass*>(p))
        {
            return tp;
        }
    }
    return nullptr;
}

const GodRaysPass* Pipeline::Impl::FindActiveGodRaysPass() const noexcept
{
    if (postProcessChain == nullptr)
    {
        return nullptr;
    }
    const std::size_t count = postProcessChain->PassCount();
    for (std::size_t i = 0; i < count; ++i)
    {
        const IPostProcessPass* p = postProcessChain->PassAt(i);
        if (const GodRaysPass* gp = dynamic_cast<const GodRaysPass*>(p))
        {
            // enabled = false 等同于"chain 里没挂"——避免调用方拆 chain。
            return gp->enabled ? gp : nullptr;
        }
    }
    return nullptr;
}

// PostProcessComponent 在场（postComponentActive）时 FindActive* 走组件填好的
// post* 成员（数据驱动）；否则退回 chain dynamic_cast（sample/test 等 chain 用法
// 兼容）。SyncPostProcessFromWorld 每帧渲染前更新 postComponentActive + post* 成员。
void Pipeline::Impl::SyncPostProcessFromWorld(Orange::Engine::World& world)
{
    postComponentActive = false;
    auto view = world.Registry().view<PostProcessComponent>();
    if (view.empty())
    {
        return;   // 无组件 → FindActive* 退回 chain
    }
    const PostProcessComponent& pp = view.get<PostProcessComponent>(view.front());
    postComponentActive = true;

    postSsao.enabled  = pp.ssaoEnabled;
    postSsao.useGtao  = pp.ssaoUseGtao;
    postSsao.radius   = pp.ssaoRadius;
    postSsao.strength = pp.ssaoStrength;
    postSsao.power    = pp.ssaoPower;

    postSsr.enabled     = pp.ssrEnabled;
    postSsr.maxDistance = pp.ssrMaxDistance;
    postSsr.thickness   = pp.ssrThickness;
    postSsr.strength    = pp.ssrStrength;

    postContact.enabled   = pp.contactEnabled;
    postContact.length    = pp.contactLength;
    postContact.thickness = pp.contactThickness;
    postContact.strength  = pp.contactStrength;

    postDof.enabled       = pp.dofEnabled;
    postDof.focusDistance = pp.dofFocusDistance;
    postDof.focusRange    = pp.dofFocusRange;
    postDof.maxCoCRadius  = pp.dofMaxCoCRadius;

    postTaa.enabled  = pp.taaEnabled;
    postTaa.feedback = pp.taaFeedback;

    postGrade.enabled     = pp.gradeEnabled;
    postGrade.exposure    = pp.gradeExposure;
    postGrade.contrast    = pp.gradeContrast;
    postGrade.saturation  = pp.gradeSaturation;
    postGrade.temperature = pp.gradeTemperature;
    postGrade.tint        = pp.gradeTint;

    postMotionBlur.enabled     = pp.motionBlurEnabled;
    postMotionBlur.intensity   = pp.motionBlurIntensity;
    postMotionBlur.maxRadius   = pp.motionBlurMaxRadius;
    postMotionBlur.sampleCount = pp.motionBlurSampleCount;

    postLens.enabled             = pp.lensEnabled;
    postLens.chromaticAberration = pp.lensChromaticAberration;
    postLens.vignetteIntensity   = pp.lensVignetteIntensity;
    postLens.vignetteSmoothness  = pp.lensVignetteSmoothness;

    postSharpen.enabled   = pp.sharpenEnabled;
    postSharpen.sharpness = pp.sharpenStrength;

    // PCSS / 阴影分辨率：组件在场时驱动 shadowConfig（压过手动 SetShadowConfig）。
    shadowConfig.pcssLightSize = pp.pcssLightSize;
    if (pp.shadowMapResolution != 0)
    {
        shadowConfig.mapResolution = pp.shadowMapResolution;
    }
}

const SsaoPass* Pipeline::Impl::FindActiveSsaoPass() const noexcept
{
    if (postComponentActive)
    {
        return postSsao.enabled ? &postSsao : nullptr;
    }
    if (postProcessChain == nullptr)
    {
        return nullptr;
    }
    const std::size_t count = postProcessChain->PassCount();
    for (std::size_t i = 0; i < count; ++i)
    {
        const IPostProcessPass* p = postProcessChain->PassAt(i);
        if (const SsaoPass* sp = dynamic_cast<const SsaoPass*>(p))
        {
            return sp->enabled ? sp : nullptr;
        }
    }
    return nullptr;
}

const SsrPass* Pipeline::Impl::FindActiveSsrPass() const noexcept
{
    if (postComponentActive)
    {
        return postSsr.enabled ? &postSsr : nullptr;
    }
    if (postProcessChain == nullptr)
    {
        return nullptr;
    }
    const std::size_t count = postProcessChain->PassCount();
    for (std::size_t i = 0; i < count; ++i)
    {
        const IPostProcessPass* p = postProcessChain->PassAt(i);
        if (const SsrPass* sp = dynamic_cast<const SsrPass*>(p))
        {
            return sp->enabled ? sp : nullptr;
        }
    }
    return nullptr;
}



// v0.9 c4：Pipeline 端 sample bin 树注册。挂到 "LayerUpdate" 下；细分
// 子 pass（Shadow / Sky / MainPass / Particles / Bloom / Tonemap / DebugDraw）
// 让 ProfilerPanel 能看到帧耗时分布。
ORANGE_PROFILE_DECLARE_BIN("Render",    "LayerUpdate")
ORANGE_PROFILE_DECLARE_BIN("Shadow",    "Render")
ORANGE_PROFILE_DECLARE_BIN("Sky",       "Render")
ORANGE_PROFILE_DECLARE_BIN("MainPass",  "Render")
ORANGE_PROFILE_DECLARE_BIN("Particles", "Render")
ORANGE_PROFILE_DECLARE_BIN("Bloom",     "Render")
ORANGE_PROFILE_DECLARE_BIN("Tonemap",   "Render")
ORANGE_PROFILE_DECLARE_BIN("DebugDraw", "Render")
ORANGE_PROFILE_DECLARE_BIN("Grid",      "Render")

void Pipeline::Render(Orange::Engine::World& world)
{
    ORANGE_PROFILE_SCOPE("Render");

    auto& impl = *mpImpl;

    impl.scene.Clear();
    impl.scene.Collect(world, impl.worldPartition);

    // 编辑器 viewport 相机覆写：SetEditorCameraOverride 注入后，把 main
    // camera 替换为编辑器轨道相机的 view/projection；ECS 内 Render::Camera
    // 组件保持游戏侧原始数据不动。详见 GAP-2026-05-15。
    if (impl.editorCameraOverride != nullptr)
    {
        impl.scene.OverrideMainCamera(*impl.editorCameraOverride);
    }

    if (!impl.initialized)
    {
        return;
    }

    // EnvironmentComponent.cubemap 变更自动 re-bake：编辑器 Inspector 拖拽 /
    // picker 替换 cubemap 字段后无需调用方手动 BakeIblFromWorld。每帧 query
    // first-found EnvironmentComponent.cubemap，与上次烘焙时记录的 handle 比
    // 较；不同（含 invalid → valid / valid → 不同 cubemap / valid → invalid）
    // 即触发一次 bake。bake 是同步阻塞（~ 几百 ms 量级），频次取决于用户
    // 编辑节奏，acceptable for 0.x。assets 在 Initialize 时已经绑定到 impl，
    // 所以本路径不需要 caller 显式喂 AssetRegistry。
    if (impl.assets != nullptr)
    {
        auto&        reg     = world.Registry();
        const auto   envView = reg.view<EnvironmentComponent>();
        ::Orange::Engine::Asset::AssetHandle<::Orange::Engine::Asset::TextureAsset>
            currentCubemap{};
        if (!envView.empty())
        {
            currentCubemap =
                envView.get<EnvironmentComponent>(envView.front()).cubemap;
        }
        if (currentCubemap != impl.lastBakedCubemap)
        {
            BakeIblFromWorld(world, *impl.assets);
            // 注意：BakeIblFromWorld 内部会更新 lastBakedCubemap；本路径
            // 不再额外赋值，避免与 graceful fallback（invalid handle → null
            // lastBakedCubemap）冲突。
        }
    }

    // 离屏模式分叉：不走 swap-chain renderer，最终输出落到 viewportColor。
    // S1 范围：bloom / tonemap / godrays / RequestCapture / InsertPass 在
    // 本路径下静默 skip，详见公共头 InitializeOffscreen 的 S1 说明。
    if (impl.offscreenMode)
    {
        impl.RenderOffscreen(world);
        return;
    }

    // 0. HDR target 同步。OnResize 已经把新尺寸写到 pendingWidth/Height；
    // 这里负责按需重建。窗口最小化（extent == 0×0）时 EnsureHdrTarget 返
    // 回 false，Pipeline 仍跑 Renderer.BeginFrame/EndFrame 但跳过 stage A。
    const bool hdrReady = impl.EnsureHdrTarget();

    // 检测 chain 里的 BloomPass / TonemapPass。
    //
    // - BloomPass 在 → stage A 末尾追加 bloom mip-chain；
    // - TonemapPass 在 + Bloom 也在 → stage B 走 tonemap 路径（替代
    //   passthrough_combine 完成 swap-chain 写出，HDR + bloom 经 ACES
    //   映射后写到 BGRA8Unorm）；
    // - 仅 Bloom（无 Tonemap）→ stage B 仍走 06.04 的 passthrough_combine
    //   做加权合成，但不做 HDR → LDR 算子；
    // - 都没有（chain 空 / 仅 HdrPass / 仅 LutPass invalid handle）→
    //   stage B 回到 06.03 的纯 HDR passthrough。
    //
    // Tonemap 没有 Bloom 的搭配（tonemap layout 仍要 binding 1）当前不
    // 在 0.x 支持范围——TonemapPass 期望 bloomCombineSet 已经准备好。
    // 设计上需要时由游戏侧自己把 BloomPass 一并加进 chain；建议默认走
    // BuiltinPostProcessChain::CreateDefault()。
    // PostProcessComponent 同步（须在下面 FindActive* + EnsureShadowMap 之前）：
    // 有组件则 postComponentActive=true、FindActive* 走组件值 + shadowConfig 受其
    // 驱动；无组件退回 chain（窗口 sample 默认走 chain）。
    impl.SyncPostProcessFromWorld(world);

    const BloomPass*   activeBloom   = impl.FindActiveBloomPass();
    const TonemapPass* activeTonemap = impl.FindActiveTonemapPass();
    const GodRaysPass* activeGodRays = impl.FindActiveGodRaysPass();
    const SsaoPass*    activeSsao    = impl.FindActiveSsaoPass();
    const SsrPass*     activeSsr     = impl.FindActiveSsrPass();
    if (activeBloom != nullptr && hdrReady)
    {
        if (!impl.EnsureBloomResources())
        {
            activeBloom = nullptr;  // bloom 资源建不出来 → 退回纯 HDR 路径
        }
    }
    else if (activeBloom == nullptr && impl.bloomMipsReady)
    {
        // chain 切回不含 BloomPass —— 释放 bloom 资源避免占内存。
        impl.ReleaseBloomResources();
    }
    // Tonemap 需要 bloomCombineSet（双 binding），活动 tonemap 但 bloom
    // 路径未就绪时回退到不上 tonemap，stage B 走 06.03 / 06.04 fallback。
    if (activeTonemap != nullptr && (activeBloom == nullptr || !impl.bloomMipsReady))
    {
        activeTonemap = nullptr;
    }

    // 1. mesh GPU 上传必须在自管 cmd 之外完成（UploadContext 内部 transient
    // cmd 与我们的 offscreenCmd 不能嵌套）。
    if (hdrReady && impl.scene.HasCamera())
    {
        impl.EnsureMeshGpuCache();
        impl.EnsureMaterialDescriptors();
    }

    // 1.5 Shadow / Light 准备：找 DirectionalLight + 计算 lightViewProj +
    // 写 light UBO + 确保 shadow map 已建好。无 light 场景 light 仍设为
    // 中性默认（toon / rim_light fragment 才有合理 base 着色），shadow map
    // 走"远深度清零 + 不画 caster"路径，PCF 取 1.0 = 全亮。
    const DirectionalLight* activeLight = nullptr;
    glm::vec3               activeLightDir{0.3f, -1.0f, 0.4f};  // neutral 默认
    glm::vec3               iblTintIntensity{1.0f, 1.0f, 1.0f}; // 未挂 EnvironmentComponent → 1,1,1（中性）
    glm::vec3               envTint{1.0f, 1.0f, 1.0f};
    float                   envIntensity = 1.0f;
    glm::vec3               cameraWorldPos{0.0f};
    if (hdrReady && impl.scene.HasCamera())
    {
        auto& reg  = world.Registry();
        auto  view = reg.view<DirectionalLight>();
        if (!view.empty())
        {
            const auto entity = view.front();
            activeLight = &view.get<DirectionalLight>(entity);
            // 方向由 entity.Transform.rotation 派生（identity = -Y 朝下）；
            // 没挂 Transform 视为 identity，与本函数另一分支 neutral 默认
            // 不冲突——neutral 默认仅在 activeLight==nullptr 时生效。
            using TC = Orange::Engine::Scene::TransformComponent;
            const auto* tc = reg.try_get<TC>(entity);
            const glm::quat rot = (tc != nullptr) ? tc->rotation : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            activeLightDir = ComputeDirectionalLightWorldDir(rot);
        }
        // EnvironmentComponent first-found：详细注释参见 RenderOffscreen 内
        // 同款代码块。envTint / envIntensity 单独留下给 sky-dome pass 用；
        // iblTintIntensity 喂 LightUbo（PBR shader IBL 段消费）。
        auto envView = reg.view<EnvironmentComponent>();
        if (!envView.empty())
        {
            const auto&  env = envView.get<EnvironmentComponent>(envView.front());
            envTint          = env.tint;
            envIntensity     = env.intensity;
            iblTintIntensity = env.tint * env.intensity;
        }
        impl.EnsureShadowMap();
        impl.EnsureSpotShadowArray();
        impl.EnsurePointShadowCube();
        const glm::mat4 lightVP = activeLight ? impl.ComputeLightViewProj(activeLightDir)
                                              : glm::mat4(1.0f);
        // 相机 worldPos：scene.MainCamera().view 是 world→view 矩阵，
        // 取 inverse 后的第 4 列即为相机在 world 中的位置。供 rim_light
        // / 后续 specular 类 fragment 取真 viewDir + sky-dome pass 反推。
        const glm::mat4 invView   = glm::inverse(impl.scene.MainCamera().view);
        cameraWorldPos            = glm::vec3(invView[3]);
        impl.UpdateLightUbo(activeLight, activeLightDir, lightVP, cameraWorldPos, iblTintIntensity);
        impl.UpdatePointLightsUbo(world);
        impl.UpdateSpotLightsUbo(world);
    }

    // 2. Stage A —— 离屏 HDR 主 pass + 可选 bloom mip-chain。无相机 /
    // 无 HDR target 时跳过；所有离屏工作进入同一 cmd list / 同一 Submit /
    // 一次 WaitIdle。
    bool offscreenOk = true;
    if (hdrReady && impl.scene.HasCamera())
    {
        auto& cmd = *impl.offscreenCmd;
        if (Orange::Failed(cmd.Begin()))
        {
            ORANGE_LOG_ERROR("Pipeline::Render: offscreen cmd Begin 失败 (frame={})",
                             impl.frameIndex);
            offscreenOk = false;
        }
        else
        {
            // TAA 激活时叠加 per-frame sub-pixel jitter（见 offscreen 路径同款注释）。
            const glm::mat4 viewProj =
                impl.ApplyTaaJitter(impl.scene.MainCamera().projection)
                * impl.scene.MainCamera().view;
            // 未 jitter 的 viewProj —— motion blur 算屏幕速度用（不含 TAA 亚像素抖动）。
            // 未激活 TAA 时 base == viewProj。
            const glm::mat4 baseViewProj = impl.scene.MainCamera().projection
                                         * impl.scene.MainCamera().view;
            const glm::mat4 lightVP =
                activeLight ? impl.ComputeLightViewProj(activeLightDir) : glm::mat4(1.0f);

            // Shadow 预 pass：在主 pass 之前把场景从 light 视角渲到
            // shadow map（depth-only）。无 light 时跳过实际绘制，只清深度。
            if (impl.shadowMap)
            {
                offscreenOk = impl.RecordShadowPass(activeLight, lightVP);
            }
            if (impl.spotShadowArray)
            {
                impl.RecordSpotShadowPass();
            }
            if (impl.pointShadowCubes[0])
            {
                impl.RecordPointShadowPass();
            }

            // game-side AfterShadow inserted passes —— shadow map 已写完，
            // 主 pass 还没开始。pass 自管 hdrColor / sceneDepth 的 layout
            // transitions（典型用例：往 shadow map 上叠加额外 caster）。
            if (offscreenOk)
            {
                auto& v = impl.insertedPasses[StageIndex(PipelineStage::AfterShadow)];
                if (!v.empty())
                {
                    RenderPassContext ctx{};
                    ctx.pRenderer       = impl.renderer.get();
                    ctx.pDevice         = &impl.renderDevice->GetRhiDevice();
                    ctx.pCmdList        = impl.offscreenCmd.get();
                    ctx.pHdrColorView   = impl.hdrColor   ? impl.hdrColor->GetDefaultView()   : nullptr;
                    ctx.pSceneDepthView = impl.sceneDepth ? impl.sceneDepth->GetDefaultView() : nullptr;
                    ctx.pSharedPool     = nullptr;
                    ctx.pViewProjData   = glm::value_ptr(viewProj);
                    ctx.width           = impl.hdrWidth;
                    ctx.height          = impl.hdrHeight;
                    ctx.frameIndex      = impl.frameIndex;
                    for (auto& p : v) { p->Execute(ctx); }
                }
            }

            // sky-dome pass：主 pass 之前画背景。两条分支与 RenderOffscreen
            // 同款 —— (a) bakedEnvCube 有 → cubemap sky；(b) 无 + skyEnabled
            // → procedural 3 色 gradient + 太阳 disc。skyEnabled = false →
            // 都跳过，主 pass clear color fallback。
            bool skyDrew = false;
            if (offscreenOk && impl.skyEnabled)
            {
                const glm::mat4 invViewProjSky = glm::inverse(viewProj);
                glm::vec3 sunDir = (activeLight != nullptr)
                    ? -glm::normalize(activeLightDir)
                    : glm::normalize(glm::vec3(0.4f, 1.0f, 0.3f));
                glm::vec3 sunColor = (activeLight != nullptr)
                    ? activeLight->color
                    : glm::vec3(1.0f, 0.95f, 0.85f);
                float sunIntensity = (activeLight != nullptr)
                    ? activeLight->intensity : 1.0f;

                if (impl.bakedEnvCube)
                {
                    skyDrew = impl.RecordSkyPass(invViewProjSky, cameraWorldPos,
                                                 envTint, envIntensity);
                }
                else
                {
                    skyDrew = impl.RecordProceduralSkyPass(
                        invViewProjSky, cameraWorldPos,
                        sunDir, sunColor, sunIntensity);
                }
            }

            // 法线预通道：主 pass 之前渲 view-space 法线到 normalBuffer，供 SSAO /
            // SSR 采真实法线。只在二者之一激活时跑（复用 sceneDepth 作 scratch
            // depth，必须紧贴主 pass 之前；详见 offscreen 路径同款注释）。
            if (offscreenOk && impl.scene.HasCamera()
                && (activeSsao != nullptr || activeSsr != nullptr
                    || impl.FindActiveContactShadowPass() != nullptr))
            {
                offscreenOk = impl.RecordNormalPrepass(viewProj,
                                                       impl.scene.MainCamera().view);
            }

            if (offscreenOk)
            {
                offscreenOk = impl.RecordOffscreenPass(viewProj, /*loadColor=*/skyDrew);
            }

            // 粒子 pass 插在主 pass 与 bloom 之间——粒子写到同一 HDR
            // target，颜色 a > 1 自动喂 bloom。VfxSystem 自管 HDR 的
            // ShaderReadOnly ↔ ColorAttachment 翻转，对调用方无副作用。
            if (offscreenOk && impl.vfxSystem != nullptr
                && impl.vfxSystem->IsInitialized()
                && impl.hdrColor)
            {
                impl.vfxSystem->DrawParticles(
                    impl.offscreenCmd.get(),
                    impl.hdrColor->GetDefaultView(),
                    /*pDepthView=*/nullptr,
                    glm::value_ptr(viewProj),
                    impl.frameIndex,
                    impl.hdrWidth,
                    impl.hdrHeight);
            }

            // game-side AfterMainPass inserted passes —— main + particle 已
            // 写完 HDR、bloom / godrays 还没跑。HDR target 当前在
            // ShaderReadOnly（粒子 pass 末尾翻回的）；pass 想写就自己
            // transition 回 ColorAttachment + BeginRendering(Load) + 写 +
            // EndRendering + 翻回 ShaderReadOnly（与 GodRaysPass 同模式）。
            // sceneDepth 当前在 DepthStencilAttachment（主 pass 末尾未翻），
            // pass 若需采样 depth 自己负责 transition。
            if (offscreenOk)
            {
                auto& v = impl.insertedPasses[StageIndex(PipelineStage::AfterMainPass)];
                if (!v.empty())
                {
                    RenderPassContext ctx{};
                    ctx.pRenderer       = impl.renderer.get();
                    ctx.pDevice         = &impl.renderDevice->GetRhiDevice();
                    ctx.pCmdList        = impl.offscreenCmd.get();
                    ctx.pHdrColorView   = impl.hdrColor   ? impl.hdrColor->GetDefaultView()   : nullptr;
                    ctx.pSceneDepthView = impl.sceneDepth ? impl.sceneDepth->GetDefaultView() : nullptr;
                    ctx.pSharedPool     = nullptr;
                    ctx.pViewProjData   = glm::value_ptr(viewProj);
                    ctx.width           = impl.hdrWidth;
                    ctx.height          = impl.hdrHeight;
                    ctx.frameIndex      = impl.frameIndex;
                    for (auto& p : v) { p->Execute(ctx); }
                }
            }

            // v1.3.0 · AuxPassProvider hook（offscreen 路径平行 hook）：与
            // window 模式同款约定（详见 IAuxPassProvider.h）。主 pass +
            // 粒子 + AfterMainPass inserted 之后、debug draw + bloom 之前。
            // Pipeline 统一 pre-transition sceneDepth → ShaderReadOnly。
            if (offscreenOk && impl.pAuxPassProvider != nullptr
                && impl.offscreenCmd != nullptr
                && impl.hdrColor != nullptr && impl.sceneDepth != nullptr
                && impl.hdrSampler != nullptr)
            {
                if (!impl.sceneDepthLayoutShaderReadOnly)
                {
                    cmd.TransitionTexture(*impl.sceneDepth,
                                          Orange::Rhi::TextureLayout::DepthStencilAttachment,
                                          Orange::Rhi::TextureLayout::ShaderReadOnly);
                    impl.sceneDepthLayoutShaderReadOnly = true;
                }

                AuxPassContext ctx{};
                ctx.pCmd        = impl.offscreenCmd.get();
                ctx.pHdrColor   = impl.hdrColor.get();
                ctx.pSceneDepth = impl.sceneDepth.get();
                ctx.pHdrSampler = impl.hdrSampler.get();
                ctx.hdrWidth    = impl.hdrWidth;
                ctx.hdrHeight   = impl.hdrHeight;
                ctx.invViewProj = glm::inverse(viewProj);
                ctx.viewProj    = viewProj;
                ctx.sceneDepthIsShaderReadOnly = true;
                ctx.hdrColorFormat   = impl.hdrColor->GetDesc().mFormat;
                ctx.sceneDepthFormat = impl.sceneDepth->GetDesc().mFormat;
                ctx.pFullscreenVs    = impl.fullscreenVs.get();
                impl.pAuxPassProvider->RenderAuxPass(ctx);
            }

            // debug draw pass：grid / aux 之后、bloom 之前。wrap 内自管空
            // 几何 / disabled silent skip。
            if (offscreenOk)
            {
                impl.RecordDebugDrawPass(viewProj);
            }

            // SSAO：bloom 之前——AO 压暗后的 HDR 再进 bloom，凹处不会被
            // bloom 错误地提亮。proj 取当前帧 main camera（重建 view-space）。
            if (offscreenOk && activeSsao != nullptr && impl.scene.HasCamera())
            {
                offscreenOk = impl.RecordSsaoPass(*activeSsao,
                                                  impl.scene.MainCamera().projection);
            }

            // SSR：SSAO 之后、bloom 之前——反射读到的是已 AO 的 HDR，反射本身
            // 也会进 bloom（湿表面高光反射的 bloom 是想要的）。
            if (offscreenOk && activeSsr != nullptr && impl.scene.HasCamera())
            {
                offscreenOk = impl.RecordSsrPass(*activeSsr,
                                                 impl.scene.MainCamera().projection);
            }

            // 接触阴影：SSAO/SSR 之后、bloom 之前——乘法暗化先于发光。仅
            // directional light 时跑（朝光向 = view 空间的 -lightDir）。
            if (offscreenOk && activeLight != nullptr && impl.scene.HasCamera())
            {
                if (const ContactShadowPass* csPass = impl.FindActiveContactShadowPass())
                {
                    const glm::vec3 viewL = glm::normalize(
                        glm::mat3(impl.scene.MainCamera().view) * (-activeLightDir));
                    offscreenOk = impl.RecordContactShadowPass(
                        *csPass, impl.scene.MainCamera().projection, viewL);
                }
            }

            // 景深：bloom 之前——先虚化场景，离焦的高光再进 bloom（柔和发散）。
            if (offscreenOk && impl.scene.HasCamera())
            {
                if (const DofPass* dofPass = impl.FindActiveDofPass())
                {
                    offscreenOk = impl.RecordDofPass(*dofPass,
                                                     impl.scene.MainCamera().projection);
                }
            }

            if (offscreenOk && activeBloom != nullptr && impl.bloomMipsReady)
            {
                offscreenOk = impl.RecordBloomChain(*activeBloom);
            }

            // god rays 插在 bloom 之后、tonemap 之前——god rays 直接累积
            // 到 HDR target，tonemap 把"HDR + bloom + god rays"整体 ACES
            // 一并压回 LDR。如果放在 bloom 之前 god rays 自身也会被 bloom
            // 二次模糊，过度发散；这里选 bloom-after 视觉更干净。
            if (offscreenOk && activeGodRays != nullptr && impl.scene.HasCamera())
            {
                offscreenOk = impl.RecordGodRaysPass(*activeGodRays, viewProj);
            }

            // TAA resolve：所有场景 post 之后、capture / Stage-B tonemap 之前。
            // viewProj 是本帧 jittered 矩阵。（bloom mip 已在前面用 pre-TAA HDR
            // 建好，demo 无强 emissive 时 bloom 极小，该 staleness 可忽略。）
            if (offscreenOk && impl.scene.HasCamera())
            {
                if (const TaaPass* taaPass = impl.FindActiveTaaPass())
                {
                    offscreenOk = impl.RecordTaaResolve(*taaPass, viewProj);
                }
            }

            // 锐化（CAS）：紧接 TAA 之后恢复细节，motion blur / DoF 之前
            // （与 offscreen 路径同款）。
            if (offscreenOk && impl.scene.HasCamera())
            {
                if (const SharpenPass* sharpenPass = impl.FindActiveSharpenPass())
                {
                    offscreenOk = impl.RecordSharpenPass(*sharpenPass);
                }
            }

            // 相机运动模糊：TAA 之后、色彩分级之前。用未 jitter 的 baseViewProj
            // 算速度（与 offscreen 路径同款）。
            if (offscreenOk && impl.scene.HasCamera())
            {
                if (const MotionBlurPass* mbPass = impl.FindActiveMotionBlurPass())
                {
                    offscreenOk = impl.RecordMotionBlurPass(*mbPass, baseViewProj);
                }
            }

            // 色彩分级：最终 look 调整，所有 post（含 TAA）之后、capture / tonemap 之前。
            if (offscreenOk)
            {
                if (const ColorGradePass* gradePass = impl.FindActiveColorGradePass())
                {
                    offscreenOk = impl.RecordColorGradePass(*gradePass);
                }
            }

            // 镜头效果（色散 + 暗角）：最后的"镜头"阶段，色彩分级之后、capture /
            // tonemap 之前（与 offscreen 路径同款）。
            if (offscreenOk)
            {
                if (const LensPass* lensPass = impl.FindActiveLensPass())
                {
                    offscreenOk = impl.RecordLensPass(*lensPass);
                }
            }

            // RequestCapture 路径：bloom 后 hdrColor 已 ShaderReadOnly，
            // 在 cmd.End() 之前追加一次 image → buffer copy；buffer 在
            // 本帧 WaitIdle 后被 FinalizeCapture 消费。capture buffer 没
            // 准备好（首次或扩容）就跳过本次 capture，下一帧请求重试。
            if (offscreenOk && impl.pendingCapturePath.has_value())
            {
                if (impl.EnsureCaptureBuffer())
                {
                    offscreenOk = impl.RecordCaptureCopy(cmd);
                }
            }

            if (Orange::Failed(cmd.End()))
            {
                ORANGE_LOG_ERROR("Pipeline::Render: offscreen cmd End 失败 (frame={})",
                                 impl.frameIndex);
                offscreenOk = false;
            }
            if (offscreenOk &&
                Orange::Failed(impl.renderDevice->GetRhiDevice().SubmitCommandList(cmd)))
            {
                ORANGE_LOG_ERROR("Pipeline::Render: offscreen SubmitCommandList 失败 (frame={})",
                                 impl.frameIndex);
                offscreenOk = false;
            }
            if (offscreenOk && Orange::Failed(impl.renderDevice->WaitIdle()))
            {
                ORANGE_LOG_ERROR("Pipeline::Render: WaitIdle 失败 (frame={})",
                                 impl.frameIndex);
                offscreenOk = false;
            }

            // WaitIdle 之后 captureBuffer 已被 GPU 写完，可在 host 侧
            // Map → ACES → PNG。RecordCaptureCopy 没追加成功 / offscreenOk
            // 已 false 时，FinalizeCapture 仍会清空请求避免无限重试。
            if (impl.pendingCapturePath.has_value())
            {
                if (offscreenOk)
                {
                    impl.FinalizeCapture();
                }
                else
                {
                    impl.pendingCapturePath.reset();
                }
            }
        }
    }

    // 3. Stage B —— swap-chain 收尾。
    constexpr double kAssumedDt = 1.0 / 60.0;
    Orange::Renderer::FrameTimeInfo timeInfo{};
    timeInfo.mTotalTimeSeconds = impl.frameIndex * kAssumedDt;
    timeInfo.mDeltaTimeSeconds = static_cast<float>(kAssumedDt);

    if (Orange::Failed(impl.renderer->BeginFrame(timeInfo)))
    {
        ORANGE_LOG_ERROR("Pipeline::Render: BeginFrame 失败 (frame={})", impl.frameIndex);
        return;
    }

    // hdrColor 初始化时若 extent == 0，descriptor set 没绑过任何 view。
    // 这种情况下 SubmitItem 会让 fragment 采样未定义 view，validation 会
    // 抱怨——直接跳过本帧 SubmitItem。
    if (hdrReady && impl.hdrLayoutShaderReadOnly)
    {
        Orange::Renderer::RenderItem item{};
        item.mDraw.mVertexCount   = 3;        // big-triangle
        item.mDraw.mInstanceCount = 1;

        if (activeTonemap != nullptr)
        {
            // Tonemap 路径：tonemap pipeline + 双 binding (HDR + bloom)
            // → ACES Narkowicz → swap-chain。activeTonemap 非空已经隐含
            // activeBloom 非空 + bloomMipsReady（在上面的过滤里保证），
            // 所以 bloomCombineSet 一定可用。
            item.mpPipeline          = impl.tonemapPipeline.get();
            item.mpDescriptorSets[0] = impl.bloomCombineSet.get();
            item.mDescriptorSetCount = 1;

            struct PushTonemap { float exposure; float bloomIntensity; float pad0, pad1; };
            PushTonemap pcData{};
            pcData.exposure       = activeTonemap->exposure;
            pcData.bloomIntensity = activeBloom ? activeBloom->intensity : 0.0f;
            std::memcpy(item.mPushConstantData.data(), &pcData, sizeof(pcData));
            item.mPushConstantSize   = static_cast<std::uint32_t>(sizeof(pcData));
            item.mPushConstantOffset = 0;
            item.mPushConstantStage  = Orange::Rhi::ShaderStage::Fragment;
        }
        else if (activeBloom != nullptr && impl.bloomMipsReady)
        {
            // 06.04 fallback：HDR + bloom 加权合成，无 tonemap
            item.mpPipeline          = impl.passthroughCombinePipeline.get();
            item.mpDescriptorSets[0] = impl.bloomCombineSet.get();
            item.mDescriptorSetCount = 1;

            struct PushCombine { float intensity; float pad0, pad1, pad2; };
            PushCombine pcData{};
            pcData.intensity = activeBloom->intensity;
            std::memcpy(item.mPushConstantData.data(), &pcData, sizeof(pcData));
            item.mPushConstantSize   = static_cast<std::uint32_t>(sizeof(pcData));
            item.mPushConstantOffset = 0;
            item.mPushConstantStage  = Orange::Rhi::ShaderStage::Fragment;
        }
        else
        {
            // 06.03 fallback：纯 HDR passthrough
            item.mpPipeline          = impl.passthroughPipeline.get();
            item.mpDescriptorSets[0] = impl.passthroughSet.get();
            item.mDescriptorSetCount = 1;
            item.mPushConstantSize   = 0;
        }
        impl.renderer->SubmitItem(item);
    }
    (void)offscreenOk;  // 离屏失败也继续走 stage B —— renderer 状态机要 Begin/EndFrame 配对

    // game-side AfterPostProcess inserted passes —— stage B 已收尾，swap-
    // chain 上现已是 LDR final image。pCmdList 为 nullptr —— 此阶段
    // OrangeRender 还没暴露 swap-chain 直 RHI，pass 通过 renderer-
    // >SubmitItem 提交自己的 fullscreen item（典型 ImGui dock space /
    // debug overlay）。
    {
        auto& v = impl.insertedPasses[StageIndex(PipelineStage::AfterPostProcess)];
        if (!v.empty())
        {
            RenderPassContext ctx{};
            ctx.pRenderer       = impl.renderer.get();
            ctx.pDevice         = impl.renderDevice ? &impl.renderDevice->GetRhiDevice() : nullptr;
            ctx.pCmdList        = nullptr;  // 此阶段 cmd 由 renderer 私有 swap-chain pass 持有
            ctx.pHdrColorView   = nullptr;  // HDR 已 retire
            ctx.pSceneDepthView = nullptr;
            ctx.pSharedPool     = nullptr;
            ctx.pViewProjData   = nullptr;
            ctx.width           = impl.hdrWidth;
            ctx.height          = impl.hdrHeight;
            ctx.frameIndex      = impl.frameIndex;
            for (auto& p : v) { p->Execute(ctx); }
        }
    }

    if (Orange::Failed(impl.renderer->EndFrame()))
    {
        ORANGE_LOG_ERROR("Pipeline::Render: EndFrame 失败 (frame={})", impl.frameIndex);
        return;
    }

    ++impl.frameIndex;
}

std::size_t Pipeline::TemplatePipelineCount() const noexcept
{
    if (!mpImpl)
    {
        return 0;
    }
    return mpImpl->templatePipelines.size();
}

void Pipeline::GetHdrTargetSize(std::uint32_t& width, std::uint32_t& height) const noexcept
{
    if (!mpImpl)
    {
        width  = 0;
        height = 0;
        return;
    }
    width  = mpImpl->hdrWidth;
    height = mpImpl->hdrHeight;
}

std::size_t Pipeline::BloomMipCount() const noexcept
{
    if (!mpImpl || !mpImpl->bloomMipsReady)
    {
        return 0;
    }
    return kBloomMipCount;
}

void Pipeline::GetBloomMipSize(std::size_t mipIndex,
                               std::uint32_t& width,
                               std::uint32_t& height) const noexcept
{
    width  = 0;
    height = 0;
    if (!mpImpl || !mpImpl->bloomMipsReady || mipIndex >= kBloomMipCount)
    {
        return;
    }
    width  = mpImpl->bloomMips[mipIndex].width;
    height = mpImpl->bloomMips[mipIndex].height;
}

}  // namespace Orange::Engine::Render
