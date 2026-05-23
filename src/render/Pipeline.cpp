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

// stb_image_write 仅 Pipeline::RequestCapture 路径用 PNG 落盘。stb 单头
// 惯例：在唯一一个 TU 里 #define IMPLEMENTATION 把符号定义生进来。include
// 路径由顶层 CMakeLists 的 BUILD_INTERFACE vendor/stb 提供，不暴露到公共面。
// MSVC 把 sprintf / strcpy 等 CRT 函数标 deprecated，本工程把警告升成错误，
// 故 push/disable C4996（deprecated）+ C4244（type narrowing）等 stb 内部
// 触发的常见噪音，include 完恢复。
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable: 4996)  // 'sprintf' deprecated
#  pragma warning(disable: 4244)  // narrowing conversion
#endif
#include "stb_image_write.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

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

Result<void, ResultCode> Pipeline::SetupRhiResources()
{
    // 共享 RHI 资源创建（sampler / passthrough / bloom / tonemap / godrays /
    // main pass UBO / shadow caster pipeline / offscreen cmd list）。Initialize
    // 与 InitializeOffscreen 两条入口都调用本函数；调用前必须保证
    // `mpImpl->renderDevice` + `mpImpl->upload` + `mpImpl->assets` 已就位。
    // 失败路径内部已调 Shutdown() 整体回滚，caller 只需 propagate 错误码。
    auto& impl = *mpImpl;
    auto& rhi = impl.renderDevice->GetRhiDevice();

    // 内置 PBR 顶点 shader push constant 总 160 B（mat4×2 + vec4×2），
    // 超过 Vulkan 规范保证下限 128 B。桌面 NVIDIA / AMD / Intel discrete
    // 普遍 256 B 不触发；移动 / 老 Intel iGPU 可能报 128 B，PBR pipeline
    // 创建会在 vkCreatePipelineLayout 处自然 fail。这里 init-time 一次性
    // 检测并日志告警，便于将来撞上时定位（非 PBR 路径仍可工作，所以不
    // 阻塞启动）。长期方案见 docs/engine-known-gaps.md
    // GAP-2026-05-19-pbr-push-constant-exceeds-spec-min（切 per-instance
    // material UBO 或 multi-stage PushConstantRange）。
    constexpr uint32_t kPbrPushConstantBytes = 160;
    const uint32_t maxPushConstants = rhi.GetCapabilities().mLimits.mMaxPushConstantsSize;
    if (maxPushConstants > 0 && maxPushConstants < kPbrPushConstantBytes)
    {
        ORANGE_LOG_WARN(
            "Pipeline::SetupRhiResources: device maxPushConstantsSize={} < {}B required by "
            "builtin PBR material; PBR pipeline creation may fail. See engine-known-gaps "
            "GAP-2026-05-19-pbr-push-constant-exceeds-spec-min.",
            maxPushConstants, kPbrPushConstantBytes);
    }

    // 4. Sampler ---------------------------------------------------------
    Orange::Rhi::SamplerDesc samplerDesc{};
    samplerDesc.mMagFilter  = Orange::Rhi::SamplerFilter::Linear;
    samplerDesc.mMinFilter  = Orange::Rhi::SamplerFilter::Linear;
    samplerDesc.mMipmapMode = Orange::Rhi::SamplerMipmapMode::Nearest;
    samplerDesc.mAddressU   = Orange::Rhi::SamplerAddressMode::ClampToEdge;
    samplerDesc.mAddressV   = Orange::Rhi::SamplerAddressMode::ClampToEdge;
    samplerDesc.mAddressW   = Orange::Rhi::SamplerAddressMode::ClampToEdge;
    samplerDesc.mpDebugName = "orange_engine.passthrough.sampler";
    impl.hdrSampler = rhi.CreateSampler(samplerDesc);
    if (!impl.hdrSampler)
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: CreateSampler 失败");
        Shutdown();
        return ResultCode::InternalError;
    }

    // 5. Descriptor set layout / pool / set -----------------------------
    Orange::Rhi::DescriptorSetLayoutDesc layoutDesc{};
    layoutDesc.mBindings.push_back({
        /*mBinding=*/0,
        Orange::Rhi::DescriptorType::CombinedImageSampler,
        /*mCount=*/1,
        Orange::Rhi::ShaderStage::Fragment});
    layoutDesc.mpDebugName = "orange_engine.passthrough.layout";
    impl.passthroughLayout = rhi.CreateDescriptorSetLayout(layoutDesc);
    if (!impl.passthroughLayout)
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: CreateDescriptorSetLayout 失败");
        Shutdown();
        return ResultCode::InternalError;
    }

    Orange::Rhi::DescriptorPoolDesc poolDesc{};
    poolDesc.mMaxSets   = 1;
    poolDesc.mPoolSizes = {{Orange::Rhi::DescriptorType::CombinedImageSampler, 1}};
    poolDesc.mpDebugName = "orange_engine.passthrough.pool";
    impl.passthroughPool = rhi.CreateDescriptorPool(poolDesc);
    if (!impl.passthroughPool)
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: CreateDescriptorPool 失败");
        Shutdown();
        return ResultCode::InternalError;
    }

    impl.passthroughSet = rhi.AllocateDescriptorSet(*impl.passthroughPool, *impl.passthroughLayout);
    if (!impl.passthroughSet)
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: AllocateDescriptorSet 失败");
        Shutdown();
        return ResultCode::InternalError;
    }

    // 6. Passthrough shader modules + pipeline --------------------------
    auto vsCode = LoadSpirv("shaders/orange_engine/fullscreen.vert.spv");
    auto fsCode = LoadSpirv("shaders/orange_engine/passthrough.frag.spv");
    if (vsCode.empty() || fsCode.empty())
    {
        Shutdown();
        return ResultCode::IoError;
    }

    Orange::Rhi::ShaderModuleDesc vsDesc{};
    vsDesc.mpCode      = vsCode.data();
    vsDesc.mCodeSize   = vsCode.size() * sizeof(std::uint32_t);
    vsDesc.mStage      = Orange::Rhi::ShaderStage::Vertex;
    vsDesc.mpDebugName = "orange_engine.fullscreen.vert";
    impl.fullscreenVs = rhi.CreateShaderModule(vsDesc);

    Orange::Rhi::ShaderModuleDesc fsDesc{};
    fsDesc.mpCode      = fsCode.data();
    fsDesc.mCodeSize   = fsCode.size() * sizeof(std::uint32_t);
    fsDesc.mStage      = Orange::Rhi::ShaderStage::Fragment;
    fsDesc.mpDebugName = "orange_engine.passthrough.frag";
    impl.passthroughFs = rhi.CreateShaderModule(fsDesc);

    if (!impl.fullscreenVs || !impl.passthroughFs)
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: 内置 fullscreen/passthrough shader 创建失败");
        Shutdown();
        return ResultCode::InternalError;
    }

    Orange::Rhi::GraphicsPipelineDesc ppDesc{};
    ppDesc.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,
                                    impl.fullscreenVs.get(), "main"});
    ppDesc.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment,
                                    impl.passthroughFs.get(), "main"});
    // 没有 vertex buffer——big-triangle 走 gl_VertexIndex；空 vertex layout。
    ppDesc.mInputAssembly.mTopology       = Orange::Rhi::PrimitiveTopology::TriangleList;
    ppDesc.mRasterizer.mCullMode          = Orange::Rhi::CullMode::None;
    ppDesc.mDepthStencil.mDepthTestEnable  = false;
    ppDesc.mDepthStencil.mDepthWriteEnable = false;
    ppDesc.mColorBlend.mAttachments.push_back({});
    ppDesc.mRenderTargets.mColorFormats.push_back(kSwapchainColorFormat);
    ppDesc.mDescriptorSetLayouts.push_back(impl.passthroughLayout.get());
    ppDesc.mpDebugName = "orange_engine.passthrough.pipeline";
    impl.passthroughPipeline = rhi.CreateGraphicsPipeline(ppDesc);
    if (!impl.passthroughPipeline)
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: passthrough pipeline 创建失败");
        Shutdown();
        return ResultCode::InternalError;
    }

    // 7. Offscreen cmd list ---------------------------------------------
    impl.offscreenCmd = rhi.CreateCommandList(Orange::Rhi::CommandQueueType::Graphics);
    if (!impl.offscreenCmd)
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: CreateCommandList 失败");
        Shutdown();
        return ResultCode::InternalError;
    }

    // 7.5 Bloom layouts + shaders + pipelines (chain 真激活前不创建 mip 资源；
    //     mip 资源由 EnsureBloomResources 在第一次 Render 时按需创建)。
    {
        // bloom 单 binding layout
        Orange::Rhi::DescriptorSetLayoutDesc lay{};
        lay.mBindings.push_back({0,
                                 Orange::Rhi::DescriptorType::CombinedImageSampler,
                                 1,
                                 Orange::Rhi::ShaderStage::Fragment});
        lay.mpDebugName = "orange_engine.bloom.layout";
        impl.bloomLayout = rhi.CreateDescriptorSetLayout(lay);

        // combine 双 binding layout (HDR + bloom)
        Orange::Rhi::DescriptorSetLayoutDesc lay2{};
        lay2.mBindings.push_back({0,
                                  Orange::Rhi::DescriptorType::CombinedImageSampler,
                                  1,
                                  Orange::Rhi::ShaderStage::Fragment});
        lay2.mBindings.push_back({1,
                                  Orange::Rhi::DescriptorType::CombinedImageSampler,
                                  1,
                                  Orange::Rhi::ShaderStage::Fragment});
        lay2.mpDebugName = "orange_engine.bloom.combine.layout";
        impl.combineLayout = rhi.CreateDescriptorSetLayout(lay2);

        if (!impl.bloomLayout || !impl.combineLayout)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: bloom DescriptorSetLayout 创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }
    }
    {
        auto downCode      = LoadSpirv("shaders/orange_engine/bloom_downsample.frag.spv");
        auto upCode        = LoadSpirv("shaders/orange_engine/bloom_upsample.frag.spv");
        auto combineCode   = LoadSpirv("shaders/orange_engine/passthrough_combine.frag.spv");
        auto tonemapVsCode = LoadSpirv("shaders/orange_engine/tonemap.vert.spv");
        auto tonemapFsCode = LoadSpirv("shaders/orange_engine/tonemap.frag.spv");
        auto godRaysCode   = LoadSpirv("shaders/orange_engine/god_rays.frag.spv");
        auto skyCode       = LoadSpirv("shaders/orange_engine/sky.frag.spv");
        auto proceduralSkyCode = LoadSpirv("shaders/orange_engine/procedural_sky.frag.spv");
        auto gridCode      = LoadSpirv("shaders/orange_engine/grid.frag.spv");
        if (downCode.empty() || upCode.empty() || combineCode.empty()
            || tonemapVsCode.empty() || tonemapFsCode.empty()
            || godRaysCode.empty()
            || skyCode.empty() || proceduralSkyCode.empty() || gridCode.empty())
        {
            Shutdown();
            return ResultCode::IoError;
        }

        Orange::Rhi::ShaderModuleDesc sm{};

        sm.mStage      = Orange::Rhi::ShaderStage::Fragment;
        sm.mpCode      = downCode.data();
        sm.mCodeSize   = downCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_engine.bloom_downsample.frag";
        impl.bloomDownsampleFs = rhi.CreateShaderModule(sm);

        sm.mpCode      = upCode.data();
        sm.mCodeSize   = upCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_engine.bloom_upsample.frag";
        impl.bloomUpsampleFs = rhi.CreateShaderModule(sm);

        sm.mpCode      = combineCode.data();
        sm.mCodeSize   = combineCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_engine.passthrough_combine.frag";
        impl.passthroughCombineFs = rhi.CreateShaderModule(sm);

        sm.mStage      = Orange::Rhi::ShaderStage::Vertex;
        sm.mpCode      = tonemapVsCode.data();
        sm.mCodeSize   = tonemapVsCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_engine.tonemap.vert";
        impl.tonemapVs = rhi.CreateShaderModule(sm);

        sm.mStage      = Orange::Rhi::ShaderStage::Fragment;
        sm.mpCode      = tonemapFsCode.data();
        sm.mCodeSize   = tonemapFsCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_engine.tonemap.frag";
        impl.tonemapFs = rhi.CreateShaderModule(sm);

        sm.mStage      = Orange::Rhi::ShaderStage::Fragment;
        sm.mpCode      = godRaysCode.data();
        sm.mCodeSize   = godRaysCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_engine.god_rays.frag";
        impl.godRaysFs = rhi.CreateShaderModule(sm);

        sm.mStage      = Orange::Rhi::ShaderStage::Fragment;
        sm.mpCode      = skyCode.data();
        sm.mCodeSize   = skyCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_engine.sky.frag";
        impl.skyFs     = rhi.CreateShaderModule(sm);

        sm.mStage             = Orange::Rhi::ShaderStage::Fragment;
        sm.mpCode             = proceduralSkyCode.data();
        sm.mCodeSize          = proceduralSkyCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName        = "orange_engine.procedural_sky.frag";
        impl.proceduralSkyFs  = rhi.CreateShaderModule(sm);

        sm.mStage      = Orange::Rhi::ShaderStage::Fragment;
        sm.mpCode      = gridCode.data();
        sm.mCodeSize   = gridCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_engine.grid.frag";
        impl.gridFs    = rhi.CreateShaderModule(sm);

        if (!impl.bloomDownsampleFs || !impl.bloomUpsampleFs || !impl.passthroughCombineFs
            || !impl.tonemapVs || !impl.tonemapFs || !impl.godRaysFs
            || !impl.skyFs || !impl.proceduralSkyFs || !impl.gridFs)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: bloom / tonemap / god_rays / sky / "
                             "procedural_sky / grid shader 模块创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }
    }
    {
        // sky descriptor layout (1 binding samplerCube)
        Orange::Rhi::DescriptorSetLayoutDesc layDesc{};
        layDesc.mBindings.push_back({0,
                                     Orange::Rhi::DescriptorType::CombinedImageSampler,
                                     1,
                                     Orange::Rhi::ShaderStage::Fragment});
        layDesc.mpDebugName = "orange_engine.sky.layout";
        impl.skyLayout = rhi.CreateDescriptorSetLayout(layDesc);

        // grid descriptor layout (1 binding sampler2D sceneDepth)
        Orange::Rhi::DescriptorSetLayoutDesc gridLayDesc{};
        gridLayDesc.mBindings.push_back({0,
                                         Orange::Rhi::DescriptorType::CombinedImageSampler,
                                         1,
                                         Orange::Rhi::ShaderStage::Fragment});
        gridLayDesc.mpDebugName = "orange_engine.grid.layout";
        impl.gridLayout = rhi.CreateDescriptorSetLayout(gridLayDesc);

        if (!impl.skyLayout || !impl.gridLayout)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: sky / grid DescriptorSetLayout "
                             "创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }
    }
    {
        // bloom downsample pipeline (RGBA16F target，无 blend，1 binding sampler，
        // push constant uThreshold + 12B pad)
        Orange::Rhi::GraphicsPipelineDesc d{};
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,
                                   impl.fullscreenVs.get(), "main"});
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment,
                                   impl.bloomDownsampleFs.get(), "main"});
        d.mInputAssembly.mTopology       = Orange::Rhi::PrimitiveTopology::TriangleList;
        d.mRasterizer.mCullMode          = Orange::Rhi::CullMode::None;
        d.mDepthStencil.mDepthTestEnable  = false;
        d.mDepthStencil.mDepthWriteEnable = false;
        d.mColorBlend.mAttachments.push_back({});  // 无 blend
        d.mRenderTargets.mColorFormats.push_back(kHdrColorFormat);
        d.mDescriptorSetLayouts.push_back(impl.bloomLayout.get());

        Orange::Rhi::PushConstantRange pcRange{};
        pcRange.mStage  = Orange::Rhi::ShaderStage::Fragment;
        pcRange.mOffset = 0;
        pcRange.mSize   = 16;  // float threshold + 3 float pad
        d.mPushConstantRanges.push_back(pcRange);

        d.mpDebugName = "orange_engine.bloom.downsample";
        impl.bloomDownsamplePipeline = rhi.CreateGraphicsPipeline(d);
    }
    {
        // bloom upsample pipeline —— additive blend
        Orange::Rhi::GraphicsPipelineDesc d{};
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,
                                   impl.fullscreenVs.get(), "main"});
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment,
                                   impl.bloomUpsampleFs.get(), "main"});
        d.mInputAssembly.mTopology       = Orange::Rhi::PrimitiveTopology::TriangleList;
        d.mRasterizer.mCullMode          = Orange::Rhi::CullMode::None;
        d.mDepthStencil.mDepthTestEnable  = false;
        d.mDepthStencil.mDepthWriteEnable = false;
        Orange::Rhi::ColorBlendAttachmentDesc blend{};
        blend.mBlendEnable         = true;
        blend.mSrcColorBlendFactor = Orange::Rhi::BlendFactor::One;
        blend.mDstColorBlendFactor = Orange::Rhi::BlendFactor::One;
        blend.mColorBlendOp        = Orange::Rhi::BlendOp::Add;
        blend.mSrcAlphaBlendFactor = Orange::Rhi::BlendFactor::One;
        blend.mDstAlphaBlendFactor = Orange::Rhi::BlendFactor::One;
        blend.mAlphaBlendOp        = Orange::Rhi::BlendOp::Add;
        d.mColorBlend.mAttachments.push_back(blend);
        d.mRenderTargets.mColorFormats.push_back(kHdrColorFormat);
        d.mDescriptorSetLayouts.push_back(impl.bloomLayout.get());
        d.mpDebugName = "orange_engine.bloom.upsample";
        impl.bloomUpsamplePipeline = rhi.CreateGraphicsPipeline(d);
    }
    {
        // passthrough_combine pipeline (BGRA8Unorm swap-chain target，
        // 2 binding 描述符，push constant uIntensity + 12B pad)
        Orange::Rhi::GraphicsPipelineDesc d{};
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,
                                   impl.fullscreenVs.get(), "main"});
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment,
                                   impl.passthroughCombineFs.get(), "main"});
        d.mInputAssembly.mTopology       = Orange::Rhi::PrimitiveTopology::TriangleList;
        d.mRasterizer.mCullMode          = Orange::Rhi::CullMode::None;
        d.mDepthStencil.mDepthTestEnable  = false;
        d.mDepthStencil.mDepthWriteEnable = false;
        d.mColorBlend.mAttachments.push_back({});
        d.mRenderTargets.mColorFormats.push_back(kSwapchainColorFormat);
        d.mDescriptorSetLayouts.push_back(impl.combineLayout.get());

        Orange::Rhi::PushConstantRange pcRange{};
        pcRange.mStage  = Orange::Rhi::ShaderStage::Fragment;
        pcRange.mOffset = 0;
        pcRange.mSize   = 16;
        d.mPushConstantRanges.push_back(pcRange);

        d.mpDebugName = "orange_engine.passthrough_combine";
        impl.passthroughCombinePipeline = rhi.CreateGraphicsPipeline(d);
    }
    {
        // tonemap pipeline (BGRA8Unorm swap-chain target，combineLayout
        // 双 binding，push constant uExposure + uBloomIntensity + 8B pad)
        Orange::Rhi::GraphicsPipelineDesc d{};
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,
                                   impl.tonemapVs.get(), "main"});
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment,
                                   impl.tonemapFs.get(), "main"});
        d.mInputAssembly.mTopology       = Orange::Rhi::PrimitiveTopology::TriangleList;
        d.mRasterizer.mCullMode          = Orange::Rhi::CullMode::None;
        d.mDepthStencil.mDepthTestEnable  = false;
        d.mDepthStencil.mDepthWriteEnable = false;
        d.mColorBlend.mAttachments.push_back({});
        d.mRenderTargets.mColorFormats.push_back(kSwapchainColorFormat);
        d.mDescriptorSetLayouts.push_back(impl.combineLayout.get());

        Orange::Rhi::PushConstantRange pcRange{};
        pcRange.mStage  = Orange::Rhi::ShaderStage::Fragment;
        pcRange.mOffset = 0;
        pcRange.mSize   = 16;
        d.mPushConstantRanges.push_back(pcRange);

        d.mpDebugName = "orange_engine.tonemap";
        impl.tonemapPipeline = rhi.CreateGraphicsPipeline(d);
    }
    {
        // god rays pipeline —— RGBA16F target，加性 blend（与 bloomUpsample
        // 同 blend）；descriptor 布局复用 bloomLayout（1 binding sampler，
        // 这里绑 sceneDepth）；push constant 64 字节装 sun_uv + 各浮点参
        // 数 + numSamples，与 src/render/builtin_shaders/god_rays.frag.glsl
        // 的 push_constant block 字节布局严格对齐。
        Orange::Rhi::GraphicsPipelineDesc d{};
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,
                                   impl.fullscreenVs.get(), "main"});
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment,
                                   impl.godRaysFs.get(), "main"});
        d.mInputAssembly.mTopology       = Orange::Rhi::PrimitiveTopology::TriangleList;
        d.mRasterizer.mCullMode          = Orange::Rhi::CullMode::None;
        d.mDepthStencil.mDepthTestEnable  = false;
        d.mDepthStencil.mDepthWriteEnable = false;
        Orange::Rhi::ColorBlendAttachmentDesc blend{};
        blend.mBlendEnable         = true;
        blend.mSrcColorBlendFactor = Orange::Rhi::BlendFactor::One;
        blend.mDstColorBlendFactor = Orange::Rhi::BlendFactor::One;
        blend.mColorBlendOp        = Orange::Rhi::BlendOp::Add;
        blend.mSrcAlphaBlendFactor = Orange::Rhi::BlendFactor::One;
        blend.mDstAlphaBlendFactor = Orange::Rhi::BlendFactor::One;
        blend.mAlphaBlendOp        = Orange::Rhi::BlendOp::Add;
        d.mColorBlend.mAttachments.push_back(blend);
        d.mRenderTargets.mColorFormats.push_back(kHdrColorFormat);
        d.mDescriptorSetLayouts.push_back(impl.bloomLayout.get());

        Orange::Rhi::PushConstantRange pcRange{};
        pcRange.mStage  = Orange::Rhi::ShaderStage::Fragment;
        pcRange.mOffset = 0;
        pcRange.mSize   = 64;  // 与 god_rays.frag.glsl push_constant block 一致
        d.mPushConstantRanges.push_back(pcRange);

        d.mpDebugName = "orange_engine.god_rays";
        impl.godRaysPipeline = rhi.CreateGraphicsPipeline(d);
    }
    {
        // sky pipeline —— RGBA16F HDR target，no blend，无 depth attachment
        // （sky 自家 BeginRendering 不带 depth；主 pass 在 sky 之后自己 Clear
        // depth 到 1.0 + 写入几何 depth）。push constant 96 字节（mat4 invVP
        // + vec3 camera + intensity + vec3 tint + pad）。
        Orange::Rhi::GraphicsPipelineDesc d{};
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,
                                   impl.fullscreenVs.get(), "main"});
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment,
                                   impl.skyFs.get(), "main"});
        d.mInputAssembly.mTopology        = Orange::Rhi::PrimitiveTopology::TriangleList;
        d.mRasterizer.mCullMode           = Orange::Rhi::CullMode::None;
        d.mDepthStencil.mDepthTestEnable  = false;
        d.mDepthStencil.mDepthWriteEnable = false;
        d.mColorBlend.mAttachments.push_back({});  // no blend
        d.mRenderTargets.mColorFormats.push_back(kHdrColorFormat);
        d.mDescriptorSetLayouts.push_back(impl.skyLayout.get());

        Orange::Rhi::PushConstantRange pcRange{};
        pcRange.mStage  = Orange::Rhi::ShaderStage::Fragment;
        pcRange.mOffset = 0;
        pcRange.mSize   = 96;  // mat4(64) + vec3(12) + float(4) + vec3(12) + float(4)
        d.mPushConstantRanges.push_back(pcRange);

        d.mpDebugName = "orange_engine.sky";
        impl.skyPipeline = rhi.CreateGraphicsPipeline(d);
    }
    {
        // procedural sky pipeline —— 与 skyPipeline 同 attachment 配置
        // （RGBA16F no blend，无 depth），仅 shader 不同、无描述符。push
        // constant 128 字节（与 ProceduralSkyPush 严格对齐）。
        //
        // BUG-2026-05-19-vk-cmd-begin-rendering-crash-on-intel-iris-xe：
        // 测试机回归锁定真因是 Intel Iris Xe ICD 对"RGBA16F color-only +
        // dynamic rendering"路径有 driver bug（具体崩在 vkCmdBeginRendering
        // 内 deref 0x3B0）。主 pass / passthrough 有 depth attachment 工作正
        // 常，唯 procedural sky 是 color-only。workaround：给 pipeline +
        // RenderingDesc 都加 dummy depth attachment（depth test / write 都
        // 关），shader 不消费 depth，但 dynamic rendering 路径在 Intel ICD
        // 看到 depth 后走有 depth 的 well-tested 路径。NV 上行为不变。
        Orange::Rhi::GraphicsPipelineDesc d{};
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,
                                   impl.fullscreenVs.get(), "main"});
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment,
                                   impl.proceduralSkyFs.get(), "main"});
        d.mInputAssembly.mTopology        = Orange::Rhi::PrimitiveTopology::TriangleList;
        d.mRasterizer.mCullMode           = Orange::Rhi::CullMode::None;
        d.mDepthStencil.mDepthTestEnable  = false;
        d.mDepthStencil.mDepthWriteEnable = false;
        d.mColorBlend.mAttachments.push_back({});
        d.mRenderTargets.mColorFormats.push_back(kHdrColorFormat);
        // Intel Iris Xe workaround：声明 depth attachment format（与 sceneDepth
        // 同 D32_SFLOAT），pipeline 与 RenderingDesc 对齐，driver 走 with-depth
        // 路径。depth test / write 均关，运行时不读写 sceneDepth 内容。
        d.mRenderTargets.mDepthStencilFormat = Orange::Rhi::TextureFormat::D32Float;

        Orange::Rhi::PushConstantRange pcRange{};
        pcRange.mStage  = Orange::Rhi::ShaderStage::Fragment;
        pcRange.mOffset = 0;
        pcRange.mSize   = 112;  // mat4(64) + 3×(vec3+float)(48) = 112
        d.mPushConstantRanges.push_back(pcRange);

        d.mpDebugName = "orange_engine.procedural_sky";
        impl.proceduralSkyPipeline = rhi.CreateGraphicsPipeline(d);
    }
    {
        // grid pipeline —— RGBA16F HDR target with alpha blend (over)，**无
        // depth attachment**。grid shader 自己采 sceneDepth + 手动比较 + discard
        // 处理几何遮挡，比 gl_FragDepth 路径稳。push 128B（mat4 invVP + mat4 VP）。
        Orange::Rhi::GraphicsPipelineDesc d{};
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,
                                   impl.fullscreenVs.get(), "main"});
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment,
                                   impl.gridFs.get(), "main"});
        d.mInputAssembly.mTopology        = Orange::Rhi::PrimitiveTopology::TriangleList;
        d.mRasterizer.mCullMode           = Orange::Rhi::CullMode::None;
        d.mDepthStencil.mDepthTestEnable  = false;
        d.mDepthStencil.mDepthWriteEnable = false;

        Orange::Rhi::ColorBlendAttachmentDesc blend{};
        blend.mBlendEnable         = true;
        blend.mSrcColorBlendFactor = Orange::Rhi::BlendFactor::SrcAlpha;
        blend.mDstColorBlendFactor = Orange::Rhi::BlendFactor::OneMinusSrcAlpha;
        blend.mColorBlendOp        = Orange::Rhi::BlendOp::Add;
        blend.mSrcAlphaBlendFactor = Orange::Rhi::BlendFactor::One;
        blend.mDstAlphaBlendFactor = Orange::Rhi::BlendFactor::OneMinusSrcAlpha;
        blend.mAlphaBlendOp        = Orange::Rhi::BlendOp::Add;
        d.mColorBlend.mAttachments.push_back(blend);

        d.mRenderTargets.mColorFormats.push_back(kHdrColorFormat);
        d.mDescriptorSetLayouts.push_back(impl.gridLayout.get());

        Orange::Rhi::PushConstantRange pcRange{};
        pcRange.mStage  = Orange::Rhi::ShaderStage::Fragment;
        pcRange.mOffset = 0;
        pcRange.mSize   = 128;  // mat4(64) + mat4(64)
        d.mPushConstantRanges.push_back(pcRange);

        d.mpDebugName = "orange_engine.grid";
        impl.gridPipeline = rhi.CreateGraphicsPipeline(d);
    }
    if (!impl.bloomDownsamplePipeline || !impl.bloomUpsamplePipeline ||
        !impl.passthroughCombinePipeline || !impl.tonemapPipeline ||
        !impl.godRaysPipeline ||
        !impl.skyPipeline || !impl.proceduralSkyPipeline || !impl.gridPipeline)
    {
        ORANGE_LOG_ERROR(
            "Pipeline::Initialize: bloom / combine / tonemap / god_rays pipeline 创建失败");
        Shutdown();
        return ResultCode::InternalError;
    }

    // 7.6 Main pass descriptor set + light UBO + shadow caster pipeline
    {
        // Main desc layout: set 0（详细 binding 注释见 Pipeline::Impl 内）
        //   binding 0 = sampler2D    shadowMap
        //   binding 1 = uniform      LightUbo
        //   binding 2 = samplerCube  uIrradiance       (IBL diffuse)
        //   binding 3 = samplerCube  uPrefilteredEnv   (IBL specular)
        //   binding 4 = sampler2D    uBrdfLut          (IBL split-sum LUT)
        Orange::Rhi::DescriptorSetLayoutDesc lay{};
        lay.mBindings.push_back({0,
                                 Orange::Rhi::DescriptorType::CombinedImageSampler,
                                 1,
                                 Orange::Rhi::ShaderStage::Fragment});
        lay.mBindings.push_back({1,
                                 Orange::Rhi::DescriptorType::UniformBuffer,
                                 1,
                                 Orange::Rhi::ShaderStage::Fragment});
        lay.mBindings.push_back({2,
                                 Orange::Rhi::DescriptorType::CombinedImageSampler,
                                 1,
                                 Orange::Rhi::ShaderStage::Fragment});
        lay.mBindings.push_back({3,
                                 Orange::Rhi::DescriptorType::CombinedImageSampler,
                                 1,
                                 Orange::Rhi::ShaderStage::Fragment});
        lay.mBindings.push_back({4,
                                 Orange::Rhi::DescriptorType::CombinedImageSampler,
                                 1,
                                 Orange::Rhi::ShaderStage::Fragment});
        // GAP-2026-05-11 G2：binding 5 = PointLightsUbo（独立 UBO，只 PBR
        // shader 引用；其他 shader dead-code 但 layout 必须 declare）。
        lay.mBindings.push_back({5,
                                 Orange::Rhi::DescriptorType::UniformBuffer,
                                 1,
                                 Orange::Rhi::ShaderStage::Fragment});
        lay.mpDebugName = "orange_engine.main.layout";
        impl.mainDescLayout = rhi.CreateDescriptorSetLayout(lay);

        // Light UBO：CpuToGpu 内存 + Map/memcpy/Unmap，per-frame 写一次。
        // 大小取 sizeof(LightUboData) = 112 B 即可，对齐由后端补到 256 B
        // 之类的 std140 / minUboAlignment——上层不关心。
        Orange::Rhi::BufferDesc bufDesc{};
        bufDesc.mSize        = sizeof(Pipeline::Impl::LightUboData);
        bufDesc.mUsage       = Orange::Rhi::BufferUsage::Uniform;
        bufDesc.mMemoryUsage = Orange::Rhi::MemoryUsage::CpuToGpu;
        impl.lightUbo = rhi.CreateBuffer(bufDesc);

        // PointLightsUbo（同款 CpuToGpu）。
        Orange::Rhi::BufferDesc plBufDesc{};
        plBufDesc.mSize        = sizeof(Pipeline::Impl::PointLightsUboData);
        plBufDesc.mUsage       = Orange::Rhi::BufferUsage::Uniform;
        plBufDesc.mMemoryUsage = Orange::Rhi::MemoryUsage::CpuToGpu;
        impl.pointLightsUbo = rhi.CreateBuffer(plBufDesc);

        // Main desc pool: 1 set，4 个 CombinedImageSampler（shadow + 3 dummy IBL） + 2 个 UBO
        Orange::Rhi::DescriptorPoolDesc pool{};
        pool.mMaxSets = 1;
        pool.mPoolSizes.push_back({Orange::Rhi::DescriptorType::CombinedImageSampler, 4});
        pool.mPoolSizes.push_back({Orange::Rhi::DescriptorType::UniformBuffer, 2});
        pool.mpDebugName = "orange_engine.main.pool";
        impl.mainDescPool = rhi.CreateDescriptorPool(pool);

        if (!impl.mainDescLayout || !impl.lightUbo || !impl.pointLightsUbo
            || !impl.mainDescPool)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: main desc layout / pool / lightUbo / pointLightsUbo 创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }

        impl.mainDescSet = rhi.AllocateDescriptorSet(*impl.mainDescPool, *impl.mainDescLayout);
        if (!impl.mainDescSet)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: main desc set 分配失败");
            Shutdown();
            return ResultCode::InternalError;
        }

        // 立刻把 binding 1 (lightUbo) + binding 5 (pointLightsUbo) 写进
        // desc set；binding 0 (shadow sampler) 等 EnsureShadowMap 创建出
        // shadowMap 后再写。
        Orange::Rhi::DescriptorWrite write{};
        write.mBinding             = 1;
        write.mType                = Orange::Rhi::DescriptorType::UniformBuffer;
        write.mBufferInfo.mpBuffer = impl.lightUbo.get();
        write.mBufferInfo.mOffset  = 0;
        write.mBufferInfo.mRange   = sizeof(Pipeline::Impl::LightUboData);
        rhi.UpdateDescriptorSet(*impl.mainDescSet, &write, 1);

        Orange::Rhi::DescriptorWrite writePl{};
        writePl.mBinding             = 5;
        writePl.mType                = Orange::Rhi::DescriptorType::UniformBuffer;
        writePl.mBufferInfo.mpBuffer = impl.pointLightsUbo.get();
        writePl.mBufferInfo.mOffset  = 0;
        writePl.mBufferInfo.mRange   = sizeof(Pipeline::Impl::PointLightsUboData);
        rhi.UpdateDescriptorSet(*impl.mainDescSet, &writePl, 1);
    }

    // 7.7 Dummy IBL 资源
    {
        // 三纹理：1×1×6 RGBA16Float cube ×2 + 1×1 RGBA16Float 2D。启动期
        // zero-clear → PBR shader IBL 贡献 = 0 → 退化为 direct-only。
        // BRDF LUT 当前用 RGBA16Float 而非 R16G16F：dummy 只关心结果 = 0；
        // 真实烘焙路径上线时再切回 R16G16F 物理正确格式（依赖 RHI 端补齐
        // R16G16F 创建支持）。
        Orange::Rhi::TextureDesc cubeDesc{};
        cubeDesc.mWidth       = 1;
        cubeDesc.mHeight      = 1;
        cubeDesc.mFormat      = Orange::Rhi::TextureFormat::RGBA16Float;
        cubeDesc.mDimension   = Orange::Rhi::TextureDimension::TexCube;
        cubeDesc.mArrayLayers = 6;
        cubeDesc.mUsage       = Orange::Rhi::TextureUsage::Sampled
                              | Orange::Rhi::TextureUsage::TransferDst;
        impl.dummyIrradianceCube  = rhi.CreateTexture(cubeDesc);
        impl.dummyPrefilteredCube = rhi.CreateTexture(cubeDesc);

        Orange::Rhi::TextureDesc brdfDesc{};
        brdfDesc.mWidth       = 1;
        brdfDesc.mHeight      = 1;
        brdfDesc.mFormat      = Orange::Rhi::TextureFormat::RGBA16Float;
        brdfDesc.mDimension   = Orange::Rhi::TextureDimension::Tex2D;
        brdfDesc.mArrayLayers = 1;
        brdfDesc.mUsage       = Orange::Rhi::TextureUsage::Sampled
                              | Orange::Rhi::TextureUsage::TransferDst;
        impl.dummyBrdfLut = rhi.CreateTexture(brdfDesc);

        if (!impl.dummyIrradianceCube || !impl.dummyPrefilteredCube || !impl.dummyBrdfLut)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: dummy IBL 纹理创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }

        // 一次性 staging buffer：每像素 RGBA16Float = 8 字节。
        //   offset  0..7  : 中性灰 ambient (0.25, 0.25, 0.25, 1.0)，给
        //                   irradiance cube 用——没挂 EnvironmentComponent
        //                   时 PBR 物体仍有可见 ambient（与 Cocos / Unity URP
        //                   默认 ambient 量级一致），观感是"灰白塑料"，不
        //                   是仅 direct light 的"半灰"
        //   offset  8..15 : 全 0，给 prefiltered cube + BRDF LUT 用
        //                   （specular 反射保持 0 避免没环境时出现"灰雾"
        //                   破坏 PBR 数学，BRDF LUT 全 0 等同 IBL 总贡献
        //                   被两项乘子双 0 抹平）
        Orange::Rhi::BufferDesc stagingDesc{};
        stagingDesc.mSize        = 64;
        stagingDesc.mUsage       = Orange::Rhi::BufferUsage::Transfer;
        stagingDesc.mMemoryUsage = Orange::Rhi::MemoryUsage::CpuToGpu;
        auto staging = rhi.CreateBuffer(stagingDesc);
        if (!staging)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: dummy IBL staging buffer 创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }
        {
            void* mapped = staging->Map();
            if (mapped == nullptr)
            {
                ORANGE_LOG_ERROR("Pipeline::Initialize: dummy IBL staging Map 失败");
                Shutdown();
                return ResultCode::InternalError;
            }
            std::memset(mapped, 0, 64);
#if defined(ORANGE_ENGINE_WITH_EDITOR_AUX_PASSES)
            // GAP-2026-05-22 editor-default-ibl-missing-causes-black-pbr-faces：
            // dummy IBL ambient = 0.5 灰（v1.0.1 c5 由 0.25 → 0.5 ）—— 编辑器
            // 路径下未挂 EnvironmentComponent 时给 PBR 暗面 ~50% baseColor 的
            // ambient 暖橙过渡，避免 "cube 暗面全黑/像透明" UX 陷阱。0.25
            // 实测验收时仍偏暗（暗面 ~25% baseColor，对零基础用户判定为
            // "黑"）。
            //
            // shipping 构建（ORANGE_ENGINE_WITH_EDITOR_AUX_PASSES=OFF）仍走
            // ambient = (0,0,0) 路径，engine 默认中性原则不动 —— 见
            // GAP-2026-05-19-editor-aux-passes-in-engine-pipeline 处理记录。
            //
            // 0x3800 = 0.5 half；0x3C00 = 1.0 half。LE 平台直写 uint16；MSVC
            // + RTX 5070 Ti 都是 LE，需要 BE 平台时再换 byteswap。
            const std::uint16_t halfPx[4] = {
                std::uint16_t{0x3800},   // R = 0.5
                std::uint16_t{0x3800},   // G = 0.5
                std::uint16_t{0x3800},   // B = 0.5
                std::uint16_t{0x3C00},   // A = 1.0
            };
            std::memcpy(mapped, halfPx, 8);
#endif
            staging->Unmap();
        }

        // 用 offscreenCmd 跑一次性 transition + copy；本帧前 offscreenCmd
        // 还没进入 frame loop，可以独立 Begin/End/Submit 一次再 reset 回去。
        auto& cmd = *impl.offscreenCmd;
        if (cmd.Begin() != Orange::ResultCode::Success)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: dummy IBL cmd.Begin 失败");
            Shutdown();
            return ResultCode::InternalError;
        }

        auto initCube = [&](Orange::Rhi::RHITexture& tex, std::uint64_t srcOffset) {
            cmd.TransitionTexture(tex,
                                  Orange::Rhi::TextureLayout::Undefined,
                                  Orange::Rhi::TextureLayout::TransferDst);
            for (std::uint32_t layer = 0; layer < 6; ++layer)
            {
                Orange::Rhi::BufferTextureCopyRegion r{};
                r.mBufferOffset = srcOffset;
                r.mMipLevel     = 0;
                r.mArrayLayer   = layer;
                r.mWidth        = 1;
                r.mHeight       = 1;
                r.mDepth        = 1;
                cmd.CopyBufferToTexture(*staging, tex, r);
            }
            cmd.TransitionTexture(tex,
                                  Orange::Rhi::TextureLayout::TransferDst,
                                  Orange::Rhi::TextureLayout::ShaderReadOnly);
        };

        initCube(*impl.dummyIrradianceCube,  /*srcOffset=*/0);  // ambient 灰
        initCube(*impl.dummyPrefilteredCube, /*srcOffset=*/8);  // 全 0

        // 2D BRDF LUT —— 单 layer 单 copy，全 0
        cmd.TransitionTexture(*impl.dummyBrdfLut,
                              Orange::Rhi::TextureLayout::Undefined,
                              Orange::Rhi::TextureLayout::TransferDst);
        {
            Orange::Rhi::BufferTextureCopyRegion r{};
            r.mBufferOffset = 8;
            r.mMipLevel     = 0;
            r.mArrayLayer   = 0;
            r.mWidth        = 1;
            r.mHeight       = 1;
            r.mDepth        = 1;
            cmd.CopyBufferToTexture(*staging, *impl.dummyBrdfLut, r);
        }
        cmd.TransitionTexture(*impl.dummyBrdfLut,
                              Orange::Rhi::TextureLayout::TransferDst,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);

        if (cmd.End() != Orange::ResultCode::Success)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: dummy IBL cmd.End 失败");
            Shutdown();
            return ResultCode::InternalError;
        }
        if (rhi.SubmitCommandList(cmd) != Orange::ResultCode::Success)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: dummy IBL SubmitCommandList 失败");
            Shutdown();
            return ResultCode::InternalError;
        }
        // 等待 copy 落盘后再让 staging buffer 出作用域；0.x 阶段 WaitIdle 够用
        impl.renderDevice->WaitIdle();

        // 把 binding 2/3/4 写入 mainDescSet。hdrSampler 在 7.x 早段已创建，
        // 与 shadow / IBL sampler 共用同一个 linear sampler（point/linear/mip
        // 等差异等到 EnvironmentComponent 引入再独立）。
        if (impl.mainDescSet && impl.hdrSampler)
        {
            Orange::Rhi::DescriptorWrite writes[3] = {};
            writes[0].mBinding             = 2;
            writes[0].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            writes[0].mImageInfo.mpTexture = impl.dummyIrradianceCube.get();
            writes[0].mImageInfo.mpSampler = impl.hdrSampler.get();

            writes[1].mBinding             = 3;
            writes[1].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            writes[1].mImageInfo.mpTexture = impl.dummyPrefilteredCube.get();
            writes[1].mImageInfo.mpSampler = impl.hdrSampler.get();

            writes[2].mBinding             = 4;
            writes[2].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            writes[2].mImageInfo.mpTexture = impl.dummyBrdfLut.get();
            writes[2].mImageInfo.mpSampler = impl.hdrSampler.get();

            rhi.UpdateDescriptorSet(*impl.mainDescSet, writes, 3);
        }
    }
    {
        // shadow caster pipeline：depth-only target (D32Float)，push constant
        // 128 B (uLightViewProj + uModel)。
        BuiltinShadowShaders::ShaderPair shadowPair = BuiltinShadowShaders::LoadShadowCaster(*impl.assets);
        if (!shadowPair.vertex.IsValid() || !shadowPair.fragment.IsValid())
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: shadow_caster shader handle 无效");
            Shutdown();
            return ResultCode::IoError;
        }
        impl.shadowCasterVsHandle = shadowPair.vertex;
        impl.shadowCasterFsHandle = shadowPair.fragment;

        auto* vsModule = impl.GetOrCreateShaderModule(shadowPair.vertex,
                                                     Orange::Rhi::ShaderStage::Vertex,
                                                     "orange_engine.shadow_caster.vert");
        auto* fsModule = impl.GetOrCreateShaderModule(shadowPair.fragment,
                                                     Orange::Rhi::ShaderStage::Fragment,
                                                     "orange_engine.shadow_caster.frag");
        if (vsModule == nullptr || fsModule == nullptr)
        {
            Shutdown();
            return ResultCode::InternalError;
        }

        Orange::Rhi::GraphicsPipelineDesc d{};
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,   vsModule, "main"});
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment, fsModule, "main"});

        FillVertexInputLayout(d);

        d.mInputAssembly.mTopology       = Orange::Rhi::PrimitiveTopology::TriangleList;
        // Shadow caster：背面剔除关掉避免 light back-facing 被剪掉；
        // 与 OrangeRender 主 pass 的 CCW 约定保持一致由顶点 winding 决定。
        d.mRasterizer.mCullMode          = Orange::Rhi::CullMode::None;
        d.mRasterizer.mFrontFace         = Orange::Rhi::FrontFace::CounterClockwise;
        d.mDepthStencil.mDepthTestEnable  = true;
        d.mDepthStencil.mDepthWriteEnable = true;
        d.mDepthStencil.mDepthCompareOp   = Orange::Rhi::CompareOp::LessOrEqual;
        // 深度 only —— 不挂 color attachment。
        d.mRenderTargets.mDepthStencilFormat = Orange::Rhi::TextureFormat::D32Float;

        Orange::Rhi::PushConstantRange pcRange{};
        pcRange.mStage  = Orange::Rhi::ShaderStage::Vertex;
        pcRange.mOffset = 0;
        pcRange.mSize   = 128;  // mat4 uLightViewProj + mat4 uModel
        d.mPushConstantRanges.push_back(pcRange);

        d.mpDebugName = "orange_engine.shadow_caster";
        impl.shadowCasterPipeline = rhi.CreateGraphicsPipeline(d);
        if (!impl.shadowCasterPipeline)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: shadow caster pipeline 创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }
    }

    // DebugDrawScene 接 RHI：HDR target 格式 RGBA16Float / 双缓冲。失败不阻
    // 塞 Pipeline 初始化（DebugDraw 是 viewport overlay 调试工具，不可用时
    // 只是 GetDebugDrawScene 返回的 wrap IsInitialized()=false，Add* 自动
    // 静默丢——保留 Pipeline 主路径仍能起。
    impl.debugDrawScene = std::make_unique<DebugDrawScene>();
    auto dbgRc = impl.debugDrawScene->InitializeBackend_(
        rhi, kHdrColorFormat, /*framesInFlight=*/2u);
    if (dbgRc.IsErr())
    {
        ORANGE_LOG_WARN(
            "Pipeline::SetupRhiResources: DebugDrawScene backend 初始化失败，"
            "viewport debug draw 不可用。");
        // wrap 已构造但 IsInitialized=false；保留对象让 GetDebugDrawScene 仍返回
        // 有效指针，消费者 Add* 自动 no-op。
    }

    return Result<void, ResultCode>{};
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
    // DebugDrawScene 必须在 renderDevice WaitIdle 之后、其他 RHI 资源释放前
    // 一起释放——Orange::Renderer::DebugDraw 持有 vertex staging buffer 与
    // 两条 pipeline，析构需要 device 还活着。
    if (impl.debugDrawScene)
    {
        impl.debugDrawScene->ShutdownBackend_();
        impl.debugDrawScene.reset();
    }
    impl.gridSet.reset();
    impl.gridPool.reset();
    impl.gridPipeline.reset();
    impl.gridLayout.reset();
    impl.gridFs.reset();
    impl.gridSetBoundDepth = nullptr;
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
    impl.mainDescSet.reset();
    impl.mainDescPool.reset();
    impl.mainDescLayout.reset();
    impl.lightUbo.reset();
    impl.pointLightsUbo.reset();
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

void Pipeline::SetEditorGridEnabled(bool enabled) noexcept
{
    if (!mpImpl) { return; }
#if defined(ORANGE_ENGINE_WITH_EDITOR_AUX_PASSES)
    mpImpl->editorGridEnabled = enabled;
#else
    // Shipping 构建剔除编辑器审美 pass：grid 永远关闭，setter 静默 ignore。
    (void)enabled;
    mpImpl->editorGridEnabled = false;
#endif
}

bool Pipeline::IsEditorGridEnabled() const noexcept
{
    return mpImpl && mpImpl->editorGridEnabled;
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
#if defined(ORANGE_ENGINE_WITH_EDITOR_AUX_PASSES)
    // GAP-2026-05-19 editor-aux-passes：viewport 默认背景 Cocos Creator 风
    // 中性灰（≈ #5C5C60）是"编辑器审美决定"，让 viewport 与主 panel 深炭灰
    // 拉开一档亮度便于辨识渲染区。shipping 构建退回 game 端深蓝默认。
    // hdrColor 是线性 HDR target，写入值经 tonemap 出到 swapchain；线性 0.12
    // 对应感知 ~0.36 / sRGB ~0.39（取 1/2.2 power）。
    att.mClear.mColor[0] = 0.12f;
    att.mClear.mColor[1] = 0.12f;
    att.mClear.mColor[2] = 0.13f;
    att.mClear.mColor[3] = 1.0f;
#else
    // Shipping：v0.8.5 之前的深蓝默认（与 sample 01/03/04 早期视觉一致）。
    att.mClear.mColor[0] = 0.05f;
    att.mClear.mColor[1] = 0.07f;
    att.mClear.mColor[2] = 0.10f;
    att.mClear.mColor[3] = 1.0f;
#endif

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

    cmd.BindGraphicsPipeline(*impl.passthroughPipeline);
    cmd.SetDescriptorSet(0, *impl.passthroughSet);
    cmd.Draw(3, 1, 0, 0);  // big-triangle，fullscreen.vert 走 gl_VertexIndex
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

    // 1. mesh GPU 上传（UploadContext 的 transient cmd 必须先于 offscreenCmd.Begin）。
    if (impl.scene.HasCamera())
    {
        impl.EnsureMeshGpuCache();
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
        const glm::mat4 lightVP   = activeLight ? impl.ComputeLightViewProj(activeLightDir)
                                                : glm::mat4(1.0f);
        const glm::mat4 invView   = glm::inverse(impl.scene.MainCamera().view);
        cameraWorldPos            = glm::vec3(invView[3]);
        impl.UpdateLightUbo(activeLight, activeLightDir, lightVP, cameraWorldPos, iblTintIntensity);
        impl.UpdatePointLightsUbo(world);
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
        const glm::mat4 viewProj = impl.scene.MainCamera().projection
                                 * impl.scene.MainCamera().view;
        const glm::mat4 invViewProj = glm::inverse(viewProj);
        const glm::mat4 lightVP  = activeLight ? impl.ComputeLightViewProj(activeLightDir)
                                               : glm::mat4(1.0f);

        // shadow pre-pass
        if (impl.shadowMap)
        {
            ok = impl.RecordShadowPass(activeLight, lightVP);
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

        // grid pass：在主 pass + 粒子之后、passthrough 之前。开关 +
        // 内部 NULL 检查；失败 silent，passthrough 继续。
        if (ok && impl.editorGridEnabled)
        {
            impl.RecordGridPass(invViewProj, viewProj);
        }

        // debug draw pass：grid 之后、passthrough 之前。wrap 内自管 enabled /
        // 空几何 silent skip；失败 silent，passthrough 继续。
        if (ok)
        {
            impl.RecordDebugDrawPass(viewProj);
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

bool Pipeline::Impl::EnsureShadowMap()
{
    const std::uint32_t targetRes = shadowConfig.mapResolution > 0
                                        ? shadowConfig.mapResolution : 1024u;
    if (shadowMap && shadowMapResolution == targetRes)
    {
        return true;
    }
    if (renderDevice == nullptr)
    {
        return false;
    }
    renderDevice->WaitIdle();
    shadowMap.reset();

    Orange::Rhi::TextureDesc t{};
    t.mWidth     = targetRes;
    t.mHeight    = targetRes;
    t.mFormat    = Orange::Rhi::TextureFormat::D32Float;
    t.mUsage     = Orange::Rhi::TextureUsage::DepthStencil
                 | Orange::Rhi::TextureUsage::Sampled;
    auto tex = renderDevice->GetRhiDevice().CreateTexture(t);
    if (!tex)
    {
        ORANGE_LOG_ERROR("Pipeline: shadow map CreateTexture 失败 ({}x{} D32Float)",
                         targetRes, targetRes);
        return false;
    }
    shadowMap                      = std::move(tex);
    shadowMapResolution            = targetRes;
    shadowMapLayoutShaderReadOnly  = false;

    // 把 main desc set 的 binding 0 重新指向新 shadow view。binding 1 已
    // 在 EnsureMainDescriptorSetBinding 时绑过 lightUbo。
    if (mainDescSet && hdrSampler)
    {
        Orange::Rhi::DescriptorWrite write{};
        write.mBinding             = 0;
        write.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
        write.mImageInfo.mpTexture = shadowMap.get();
        write.mImageInfo.mpSampler = hdrSampler.get();  // 与 HDR sampler 共用一个 linear sampler
        renderDevice->GetRhiDevice().UpdateDescriptorSet(*mainDescSet, &write, 1);
    }
    return true;
}

glm::mat4 Pipeline::Impl::ComputeLightViewProj(const glm::vec3& lightWorldDir) const
{
    // 0.x 简化：scene bbox 假定为 ±10 单位的立方体（足够覆盖 sample
    // 的 plane + cube + sphere）。后续接 RenderScene 提供的 bbox。
    const glm::vec3 lightDir = glm::normalize(lightWorldDir);
    const glm::vec3 sceneCenter(0.0f);
    constexpr float kHalfExtent = 10.0f;

    // 把"光源位置"放在 sceneCenter - lightDir * 2 * halfExtent，让 view
    // 看向 sceneCenter；ortho 视锥按 ±halfExtent 包住整个 scene。
    const glm::vec3 lightPos = sceneCenter - lightDir * (2.0f * kHalfExtent);
    glm::vec3       up       = glm::vec3(0.0f, 1.0f, 0.0f);
    if (std::abs(lightDir.y) > 0.99f)
    {
        // 光照接近垂直 → 切到 Z 轴避免奇异。
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    const glm::mat4 view = glm::lookAt(lightPos, sceneCenter, up);

    // 手写 Vulkan-style ortho（与 Camera::Orthographic 同公式）：z ∈ [0, 1]、
    // y-flip。glm::ortho 的 z 输出是 OpenGL [-1, 1]，会被 Vulkan 近平面 z=0
    // 裁掉一半 frustum，shadow caster 写不进 shadow map → plane 上看不到
    // 任何阴影。这里直接构造正确矩阵。
    constexpr float zNear = 0.1f;
    constexpr float zFar  = 4.0f * kHalfExtent;
    glm::mat4 proj(1.0f);
    proj[0][0] =  1.0f / kHalfExtent;
    proj[1][1] = -1.0f / kHalfExtent;       // y-flip 到 Vulkan NDC
    proj[2][2] =  1.0f / (zNear - zFar);    // z ∈ [0, 1]（near 远 → 0）
    proj[3][2] =  zNear / (zNear - zFar);
    return proj * view;
}

void Pipeline::Impl::UpdateLightUbo(const DirectionalLight* light,
                                    const glm::vec3&        lightWorldDir,
                                    const glm::mat4&        lightViewProj,
                                    const glm::vec3&        cameraWorldPos,
                                    const glm::vec3&        iblTintIntensity)
{
    if (!lightUbo)
    {
        return;
    }
    LightUboData data{};
    data.lightViewProj = lightViewProj;
    if (light != nullptr)
    {
        data.lightDirIntensity = glm::vec4(lightWorldDir, light->intensity);
        data.lightColor        = glm::vec4(light->color, 0.0f);
    }
    else
    {
        // neutral light：方向斜下、白光、单位强度。toon / rim_light 在
        // 没真光的场景仍能给出"基线"光照（与 sample 03/04 视觉一致）。
        data.lightDirIntensity = glm::vec4(0.3f, -1.0f, 0.4f, 1.0f);
        data.lightColor        = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    }
    data.shadowParams = glm::vec4(static_cast<float>(shadowConfig.pcfKernelRadius),
                                  shadowConfig.depthBias,
                                  0.0f, 0.0f);
    data.cameraWorldPos = glm::vec4(cameraWorldPos, 0.0f);
    data.frameInfo      = glm::vec4(frameTime, 0.0f, 0.0f, 0.0f);
    data.iblFactor      = glm::vec4(iblTintIntensity, 0.0f);

    void* mapped = lightUbo->Map();
    if (mapped == nullptr)
    {
        ORANGE_LOG_ERROR("Pipeline: lightUbo Map 失败");
        return;
    }
    std::memcpy(mapped, &data, sizeof(data));
    lightUbo->Unmap();
}

void Pipeline::Impl::UpdatePointLightsUbo(Orange::Engine::World& world)
{
    if (!pointLightsUbo) { return; }

    PointLightsUboData data{};
    std::uint32_t      count = 0;

    auto& reg = world.Registry();
    // entt 不要求 TransformComponent 同步存在；缺 Transform 视为 origin。
    auto view = reg.view<PointLight>();
    bool warnedOverflow = false;
    for (auto entity : view)
    {
        if (count >= kMaxPointLights)
        {
            if (!warnedOverflow)
            {
                ORANGE_LOG_WARN("Pipeline: scene has more than {} PointLights; "
                                "extras ignored.", kMaxPointLights);
                warnedOverflow = true;
            }
            continue;
        }
        const auto& pl = view.get<PointLight>(entity);
        glm::vec3 pos{0.0f};
        if (const auto* tc = reg.try_get<Orange::Engine::Scene::TransformComponent>(entity))
        {
            pos = tc->position;
        }
        data.lights[count].posRange       = glm::vec4(pos, pl.range);
        data.lights[count].colorIntensity = glm::vec4(pl.color, pl.intensity);
        ++count;
    }
    data.countPad.x = count;

    void* mapped = pointLightsUbo->Map();
    if (mapped == nullptr)
    {
        ORANGE_LOG_ERROR("Pipeline: pointLightsUbo Map 失败");
        return;
    }
    std::memcpy(mapped, &data, sizeof(data));
    pointLightsUbo->Unmap();
}

bool Pipeline::Impl::RecordShadowPass(const DirectionalLight* light,
                                      const glm::mat4& lightViewProj)
{
    ORANGE_PROFILE_SCOPE("Shadow");
    if (!shadowMap || offscreenCmd == nullptr)
    {
        return false;
    }
    auto& cmd = *offscreenCmd;

    const auto fromLayout = shadowMapLayoutShaderReadOnly
        ? Orange::Rhi::TextureLayout::ShaderReadOnly
        : Orange::Rhi::TextureLayout::Undefined;
    cmd.TransitionTexture(*shadowMap, fromLayout,
                          Orange::Rhi::TextureLayout::DepthStencilAttachment);

    Orange::Rhi::DepthStencilAttachment depth{};
    depth.mpView          = shadowMap->GetDefaultView();
    depth.mDepthLoadOp    = Orange::Rhi::LoadOp::Clear;
    depth.mDepthStoreOp   = Orange::Rhi::StoreOp::Store;
    depth.mClear.mDepth   = 1.0f;

    Orange::Rhi::RenderingDesc rd{};
    rd.mRenderArea.mWidth  = shadowMapResolution;
    rd.mRenderArea.mHeight = shadowMapResolution;
    rd.mDepthStencil       = depth;
    cmd.BeginRendering(rd);

    Orange::Rhi::RHIViewport vp{};
    vp.mWidth    = static_cast<float>(shadowMapResolution);
    vp.mHeight   = static_cast<float>(shadowMapResolution);
    vp.mMinDepth = 0.0f;
    vp.mMaxDepth = 1.0f;
    cmd.SetViewport(vp);
    Orange::Rhi::RHIScissor sc{};
    sc.mWidth  = shadowMapResolution;
    sc.mHeight = shadowMapResolution;
    cmd.SetScissor(sc);

    // 光源不投影 / 缺失时——清完深度 = 1.0 即"远深度"，shadow_pcf 取
    // currentDepth <= 1.0 → 总是 1（全亮），等价于"无阴影"。
    const bool runCaster = (light != nullptr && light->castsShadow && shadowCasterPipeline);
    if (runCaster)
    {
        cmd.BindGraphicsPipeline(*shadowCasterPipeline);

        for (const auto& drawable : scene.Drawables())
        {
            if (!drawable.castsShadow)
            {
                continue;  // 主 pass 仍会绘，只是不进 shadow map
            }
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

            // shadow_caster 的 push constant：uLightViewProj(64) + uModel(64) = 128 B
            struct ShadowCasterPush { glm::mat4 lightVP; glm::mat4 model; };
            ShadowCasterPush data{};
            data.lightVP = lightViewProj;
            data.model   = drawable.worldMatrix;
            cmd.SetPushConstants(Orange::Rhi::ShaderStage::Vertex,
                                 0, static_cast<std::uint32_t>(sizeof(data)),
                                 &data);

            cmd.BindVertexBuffer(0, *gpu.vertexBuffer, 0);
            cmd.BindIndexBuffer(*gpu.indexBuffer, 0, Orange::Rhi::IndexFormat::UInt32);
            cmd.DrawIndexed(gpu.indexCount, 1, 0, 0, 0);
        }
    }

    cmd.EndRendering();

    cmd.TransitionTexture(*shadowMap,
                          Orange::Rhi::TextureLayout::DepthStencilAttachment,
                          Orange::Rhi::TextureLayout::ShaderReadOnly);
    shadowMapLayoutShaderReadOnly = true;
    return true;
}

// ---------------------------------------------------------------------------
// RequestCapture 路径实现：
//   1. RequestCapture(path) 仅缓存路径；
//   2. Render() Stage A 末尾若 pendingCapturePath 有值，
//      EnsureCaptureBuffer 按 hdr 尺寸 grow buffer，RecordCaptureCopy
//      在已 Begin 的 offscreenCmd 上追加 transition + CopyTextureToBuffer
//      + 转回 ShaderReadOnly；
//   3. Render() WaitIdle 之后 FinalizeCapture：Map → ACES → stbi_write_png。
// ---------------------------------------------------------------------------

bool Pipeline::Impl::EnsureCaptureBuffer()
{
    if (renderDevice == nullptr || hdrColor == nullptr || hdrWidth == 0 || hdrHeight == 0)
    {
        return false;
    }
    // RGBA16Float = 4 通道 × 2 字节 = 8 字节 / 像素。
    const std::uint64_t needed = static_cast<std::uint64_t>(hdrWidth) * hdrHeight * 8ULL;
    if (captureBuffer && captureBufferCapacity >= needed)
    {
        return true;
    }
    renderDevice->WaitIdle();  // 旧 buffer 可能被 in-flight 命令引用 — 等空再释放
    captureBuffer.reset();
    Orange::Rhi::BufferDesc desc{};
    desc.mSize        = needed;
    desc.mUsage       = Orange::Rhi::BufferUsage::Transfer;
    desc.mMemoryUsage = Orange::Rhi::MemoryUsage::GpuToCpu;
    captureBuffer = renderDevice->GetRhiDevice().CreateBuffer(desc);
    if (!captureBuffer)
    {
        ORANGE_LOG_ERROR("Pipeline: capture buffer create 失败 (size={} bytes)", needed);
        captureBufferCapacity = 0;
        return false;
    }
    captureBufferCapacity = needed;
    return true;
}

bool Pipeline::Impl::RecordCaptureCopy(Orange::Rhi::RHICommandList& cmd)
{
    if (!hdrColor || !captureBuffer)
    {
        return false;
    }
    // 主 pass + bloom 后 hdrColor 处于 ShaderReadOnly；先转 TransferSrc。
    cmd.TransitionTexture(*hdrColor,
                          Orange::Rhi::TextureLayout::ShaderReadOnly,
                          Orange::Rhi::TextureLayout::TransferSrc);

    Orange::Rhi::BufferTextureCopyRegion region{};
    region.mBufferOffset = 0;
    region.mMipLevel     = 0;
    region.mArrayLayer   = 0;
    region.mWidth        = hdrWidth;
    region.mHeight       = hdrHeight;
    region.mDepth        = 1;
    cmd.CopyTextureToBuffer(*hdrColor, *captureBuffer, region);

    // 转回 ShaderReadOnly，让 Stage B 的 tonemap pass 仍可 sample。
    cmd.TransitionTexture(*hdrColor,
                          Orange::Rhi::TextureLayout::TransferSrc,
                          Orange::Rhi::TextureLayout::ShaderReadOnly);
    return true;
}

namespace
{

// IEEE-754 binary16 → binary32。subnormal / inf / nan 全部覆盖。
float HalfToFloat(std::uint16_t h) noexcept
{
    const std::uint32_t s = (h >> 15) & 0x1u;
    const std::uint32_t e = (h >> 10) & 0x1Fu;
    const std::uint32_t m = h & 0x3FFu;
    std::uint32_t f = 0;
    if (e == 0)
    {
        if (m == 0)
        {
            f = s << 31;  // ±0
        }
        else
        {
            // subnormal —— normalize 一下到 binary32 形式
            std::uint32_t mantissa = m;
            std::uint32_t shift    = 0;
            while ((mantissa & 0x400u) == 0)
            {
                mantissa <<= 1;
                ++shift;
            }
            mantissa &= 0x3FFu;
            f = (s << 31) | ((127u - 14u - shift) << 23) | (mantissa << 13);
        }
    }
    else if (e == 31)
    {
        f = (s << 31) | 0x7F800000u | (m << 13);  // inf / nan
    }
    else
    {
        f = (s << 31) | ((e + 127u - 15u) << 23) | (m << 13);
    }
    float r;
    std::memcpy(&r, &f, sizeof(float));
    return r;
}

// ACES Narkowicz fit —— 与内置 tonemap.frag 同曲线。
float AcesNarkowicz(float x) noexcept
{
    constexpr float a = 2.51f;
    constexpr float b = 0.03f;
    constexpr float c = 2.43f;
    constexpr float d = 0.59f;
    constexpr float e = 0.14f;
    const float result = (x * (a * x + b)) / (x * (c * x + d) + e);
    return std::clamp(result, 0.0f, 1.0f);
}

}  // namespace

void Pipeline::Impl::FinalizeCapture()
{
    if (!pendingCapturePath.has_value() || !captureBuffer || hdrWidth == 0 || hdrHeight == 0)
    {
        pendingCapturePath.reset();
        return;
    }
    const std::filesystem::path outPath = std::move(*pendingCapturePath);
    pendingCapturePath.reset();

    void* mapped = captureBuffer->Map();
    if (mapped == nullptr)
    {
        ORANGE_LOG_ERROR("Pipeline: capture buffer Map 失败");
        return;
    }

    const std::uint32_t pixelCount = hdrWidth * hdrHeight;
    std::vector<std::uint8_t> ldr(static_cast<std::size_t>(pixelCount) * 4);
    const std::uint16_t* hdrPx = static_cast<const std::uint16_t*>(mapped);

    for (std::uint32_t i = 0; i < pixelCount; ++i)
    {
        const std::uint16_t* p = hdrPx + i * 4;
        float r = AcesNarkowicz(HalfToFloat(p[0]));
        float g = AcesNarkowicz(HalfToFloat(p[1]));
        float b = AcesNarkowicz(HalfToFloat(p[2]));
        ldr[i * 4 + 0] = static_cast<std::uint8_t>(r * 255.0f + 0.5f);
        ldr[i * 4 + 1] = static_cast<std::uint8_t>(g * 255.0f + 0.5f);
        ldr[i * 4 + 2] = static_cast<std::uint8_t>(b * 255.0f + 0.5f);
        ldr[i * 4 + 3] = 255;  // alpha 一律不透明，HDR alpha 不参与 tonemap
    }

    captureBuffer->Unmap();

    const std::string outStr = outPath.string();
    const int ok = stbi_write_png(outStr.c_str(),
                                  static_cast<int>(hdrWidth),
                                  static_cast<int>(hdrHeight),
                                  4,
                                  ldr.data(),
                                  static_cast<int>(hdrWidth * 4));
    if (ok == 0)
    {
        ORANGE_LOG_ERROR("Pipeline: stbi_write_png 失败 path={}", outStr);
        return;
    }
    ORANGE_LOG_INFO("Pipeline: capture saved to {} ({}x{})", outStr, hdrWidth, hdrHeight);
}

void Pipeline::Impl::ReleaseBloomResources()
{
    if (renderDevice)
    {
        renderDevice->WaitIdle();
    }
    bloomCombineSet.reset();
    for (auto& s : bloomUpsampleSets) s.reset();
    for (auto& s : bloomDownsampleSets) s.reset();
    bloomPool.reset();
    for (auto& mip : bloomMips)
    {
        mip.texture.reset();
        mip.width = 0;
        mip.height = 0;
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
    const std::uint32_t expectedMip0W = std::max<std::uint32_t>(hdrWidth  / 2, 1);
    const std::uint32_t expectedMip0H = std::max<std::uint32_t>(hdrHeight / 2, 1);

    if (bloomMipsReady && bloomMips[0].width == expectedMip0W && bloomMips[0].height == expectedMip0H)
    {
        return true;  // 已建好且尺寸一致
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
        t.mUsage  = Orange::Rhi::TextureUsage::RenderTarget
                  | Orange::Rhi::TextureUsage::Sampled;
        auto tex = rhi.CreateTexture(t);
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
    poolDesc.mMaxSets   = static_cast<std::uint32_t>(kBloomMipCount * 2);  // 12
    poolDesc.mPoolSizes = {{Orange::Rhi::DescriptorType::CombinedImageSampler,
                            static_cast<std::uint32_t>(kBloomMipCount * 2 + 1)}};  // 13
    poolDesc.mpDebugName = "orange_engine.bloom.pool";
    bloomPool = rhi.CreateDescriptorPool(poolDesc);
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
        att.mpView   = mip.texture->GetDefaultView();
        att.mLoadOp  = Orange::Rhi::LoadOp::Clear;
        att.mStoreOp = Orange::Rhi::StoreOp::Store;
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
        struct PushDown { float threshold; float pad0, pad1, pad2; };
        PushDown pcData{};
        pcData.threshold = (i == 0) ? bloomDesc.threshold : 0.0f;
        cmd.SetPushConstants(Orange::Rhi::ShaderStage::Fragment,
                             0, static_cast<std::uint32_t>(sizeof(pcData)), &pcData);

        cmd.Draw(3, 1, 0, 0);  // big-triangle
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
        const std::size_t i = (kBloomMipCount - 2) - step;  // 4, 3, 2, 1, 0
        auto& mip = bloomMips[i];

        // 把目标 mip 从 ShaderReadOnly 切回 ColorAttachment 准备写入。
        cmd.TransitionTexture(*mip.texture,
                              Orange::Rhi::TextureLayout::ShaderReadOnly,
                              Orange::Rhi::TextureLayout::ColorAttachment);
        mip.layoutShaderReadOnly = false;

        Orange::Rhi::ColorAttachment att{};
        att.mpView   = mip.texture->GetDefaultView();
        att.mLoadOp  = Orange::Rhi::LoadOp::Load;     // 保留 downsample 写入的内容
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

// ---------------------------------------------------------------------------
// GodRaysPass 录制 + 描述符 set 维护
// ---------------------------------------------------------------------------

bool Pipeline::Impl::EnsureGodRaysSet()
{
    if (renderDevice == nullptr || sceneDepth == nullptr || hdrSampler == nullptr
        || godRaysPipeline == nullptr || bloomLayout == nullptr)
    {
        return false;
    }

    auto& rhi = renderDevice->GetRhiDevice();

    // 池只建一次（只有 1 个 set，长寿命）。后续仅刷新 binding。
    if (!godRaysPool)
    {
        Orange::Rhi::DescriptorPoolDesc poolDesc{};
        poolDesc.mMaxSets = 1;
        Orange::Rhi::DescriptorPoolSize sz{};
        sz.mType  = Orange::Rhi::DescriptorType::CombinedImageSampler;
        sz.mCount = 1;
        poolDesc.mPoolSizes.push_back(sz);
        poolDesc.mpDebugName = "orange_engine.god_rays.pool";
        godRaysPool = rhi.CreateDescriptorPool(poolDesc);
        if (!godRaysPool)
        {
            ORANGE_LOG_ERROR("Pipeline: god rays CreateDescriptorPool 失败");
            return false;
        }
    }

    // sceneDepth 重建后（OnResize / 首次） → 重新分配 set 并写 binding。
    if (godRaysSet == nullptr || godRaysSetBoundDepth != sceneDepth.get())
    {
        godRaysSet.reset();
        auto set = rhi.AllocateDescriptorSet(*godRaysPool, *bloomLayout);
        if (!set)
        {
            ORANGE_LOG_ERROR("Pipeline: god rays AllocateDescriptorSet 失败");
            return false;
        }
        Orange::Rhi::DescriptorWrite w{};
        w.mBinding             = 0;
        w.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
        w.mImageInfo.mpTexture = sceneDepth.get();
        w.mImageInfo.mpSampler = hdrSampler.get();
        rhi.UpdateDescriptorSet(*set, &w, 1);

        godRaysSet           = std::move(set);
        godRaysSetBoundDepth = sceneDepth.get();
    }
    return true;
}

bool Pipeline::Impl::RecordGodRaysPass(const GodRaysPass& gr,
                                       const glm::mat4&   viewProj)
{
    if (offscreenCmd == nullptr || hdrColor == nullptr || sceneDepth == nullptr
        || godRaysPipeline == nullptr)
    {
        return false;
    }
    if (!EnsureGodRaysSet())
    {
        return false;
    }

    auto& cmd = *offscreenCmd;

    // 1. sceneDepth：DepthStencilAttachment → ShaderReadOnly（让 frag
    //    采样它做 occlusion proxy）。下一帧主 pass 会翻回 DSA。
    if (!sceneDepthLayoutShaderReadOnly)
    {
        cmd.TransitionTexture(*sceneDepth,
                              Orange::Rhi::TextureLayout::DepthStencilAttachment,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
        sceneDepthLayoutShaderReadOnly = true;
    }

    // 2. HDR：调用此函数时 hdrLayoutShaderReadOnly == true（RecordOffscreenPass
    //    末尾 + 粒子 pass 末尾 + bloom pass 末尾都会留在 ShaderReadOnly）。
    //    god rays 要写 HDR 故 transition 回 ColorAttachment。
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
    vp.mWidth    = static_cast<float>(hdrWidth);
    vp.mHeight   = static_cast<float>(hdrHeight);
    vp.mMinDepth = 0.0f;
    vp.mMaxDepth = 1.0f;
    cmd.SetViewport(vp);
    Orange::Rhi::RHIScissor sc{};
    sc.mWidth  = hdrWidth;
    sc.mHeight = hdrHeight;
    cmd.SetScissor(sc);

    cmd.BindGraphicsPipeline(*godRaysPipeline);
    cmd.SetDescriptorSet(0, *godRaysSet);

    // 计算 sun 屏幕 uv：把 -sunWorldDir × kFar 这个"远点"投到 NDC，再
    // 翻成 uv。sun 在 frustum 之外时 sunUV 越界 → frag 仍能跑，但 visibility
    // 几乎处处 0 → 视觉上 god rays 自然消失（已知限制）。
    glm::vec3 sunDir = gr.sunWorldDir;
    if (glm::dot(sunDir, sunDir) > 1e-6f)
    {
        sunDir = glm::normalize(sunDir);
    }
    else
    {
        sunDir = glm::vec3(0.0f, -1.0f, 0.0f);
    }
    constexpr float kFarPos     = 1000.0f;
    const glm::vec3  sunPosWorld = -sunDir * kFarPos;
    const glm::vec4  ndc4        = viewProj * glm::vec4(sunPosWorld, 1.0f);
    const float      invW        = (ndc4.w != 0.0f) ? (1.0f / ndc4.w) : 1.0f;
    const float      sunNdcX     = ndc4.x * invW;
    const float      sunNdcY     = ndc4.y * invW;
    const glm::vec2  sunUv(sunNdcX * 0.5f + 0.5f, sunNdcY * 0.5f + 0.5f);

    // push constant 布局必须与 god_rays.frag.glsl 的 push_constant block
    // 字节布局严格对齐；任一处改了，另一处必须同步。
    struct GodRaysPush
    {
        glm::vec2 sunUV;
        float     density;
        float     decay;

        glm::vec3 sunColor;
        float     weight;

        float     exposure;
        float     pad0;
        float     pad1;
        float     pad2;

        std::int32_t numSamples;
        std::int32_t padI0;
        std::int32_t padI1;
        std::int32_t padI2;
    };
    static_assert(sizeof(GodRaysPush) == 64,
                  "GodRaysPush must match god_rays.frag push_constant block (64 B).");

    GodRaysPush push{};
    push.sunUV      = sunUv;
    push.density    = gr.density;
    push.decay      = gr.decay;
    push.sunColor   = gr.sunColor;
    push.weight     = gr.weight;
    push.exposure   = gr.exposure;
    push.numSamples = std::max(gr.numSamples, 1);

    cmd.SetPushConstants(Orange::Rhi::ShaderStage::Fragment, 0,
                         sizeof(GodRaysPush), &push);

    cmd.Draw(3, 1, 0, 0);
    cmd.EndRendering();

    // 3. HDR 翻回 ShaderReadOnly：下一段（capture / tonemap stage B）按
    //    "HDR 在 ShaderReadOnly"假设跑。
    cmd.TransitionTexture(*hdrColor,
                          Orange::Rhi::TextureLayout::ColorAttachment,
                          Orange::Rhi::TextureLayout::ShaderReadOnly);
    return true;
}

bool Pipeline::Impl::EnsureSkyDescSet()
{
    if (renderDevice == nullptr || skyPipeline == nullptr || skyLayout == nullptr
        || hdrSampler == nullptr || bakedEnvCube == nullptr)
    {
        return false;
    }

    auto& rhi = renderDevice->GetRhiDevice();

    // 池只建一次（1 个 set，长寿命）。后续仅 rewrite binding。
    if (!skyPool)
    {
        Orange::Rhi::DescriptorPoolDesc poolDesc{};
        poolDesc.mMaxSets = 1;
        Orange::Rhi::DescriptorPoolSize sz{};
        sz.mType  = Orange::Rhi::DescriptorType::CombinedImageSampler;
        sz.mCount = 1;
        poolDesc.mPoolSizes.push_back(sz);
        poolDesc.mpDebugName = "orange_engine.sky.pool";
        skyPool = rhi.CreateDescriptorPool(poolDesc);
        if (!skyPool)
        {
            ORANGE_LOG_ERROR("Pipeline: sky CreateDescriptorPool 失败");
            return false;
        }
    }

    // bakedEnvCube 重建（BakeIblFromWorld 重跑、首次烘焙完成）→ 重新分配
    // set 并写 binding 0。同款"绑定缓存"惯例参 EnsureGodRaysSet。
    if (skySet == nullptr || skySetBoundCube != bakedEnvCube.get())
    {
        skySet.reset();
        auto set = rhi.AllocateDescriptorSet(*skyPool, *skyLayout);
        if (!set)
        {
            ORANGE_LOG_ERROR("Pipeline: sky AllocateDescriptorSet 失败");
            return false;
        }
        Orange::Rhi::DescriptorWrite w{};
        w.mBinding             = 0;
        w.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
        w.mImageInfo.mpTexture = bakedEnvCube.get();
        w.mImageInfo.mpSampler = hdrSampler.get();
        rhi.UpdateDescriptorSet(*set, &w, 1);

        skySet           = std::move(set);
        skySetBoundCube  = bakedEnvCube.get();
    }
    return true;
}

bool Pipeline::Impl::RecordSkyPass(const glm::mat4& invViewProj,
                                   const glm::vec3& cameraPos,
                                   const glm::vec3& tint,
                                   float            intensity)
{
    ORANGE_PROFILE_SCOPE("Sky");
    if (offscreenCmd == nullptr || hdrColor == nullptr || skyPipeline == nullptr)
    {
        return false;
    }
    if (!EnsureSkyDescSet())
    {
        return false;  // 无 bakedEnvCube 或资源未就绪
    }

    auto& cmd = *offscreenCmd;

    // hdrColor 当前 layout：上一帧末尾通常翻在 ShaderReadOnly（passthrough /
    // bloom 末尾的契约）；首帧 hdrLayoutShaderReadOnly == false（Undefined）。
    const auto fromLayout = hdrLayoutShaderReadOnly
        ? Orange::Rhi::TextureLayout::ShaderReadOnly
        : Orange::Rhi::TextureLayout::Undefined;
    cmd.TransitionTexture(*hdrColor, fromLayout,
                          Orange::Rhi::TextureLayout::ColorAttachment);
    hdrLayoutShaderReadOnly = false;

    Orange::Rhi::ColorAttachment att{};
    att.mpView          = hdrColor->GetDefaultView();
    att.mLoadOp         = Orange::Rhi::LoadOp::Clear;
    att.mStoreOp        = Orange::Rhi::StoreOp::Store;
    att.mClear.mColor[0] = 0.05f;
    att.mClear.mColor[1] = 0.07f;
    att.mClear.mColor[2] = 0.10f;
    att.mClear.mColor[3] = 1.0f;

    Orange::Rhi::RenderingDesc rd{};
    rd.mRenderArea.mWidth  = hdrWidth;
    rd.mRenderArea.mHeight = hdrHeight;
    rd.mColorAttachments.push_back(att);
    // 不挂 depth attachment —— sky 不消费 / 不修改 depth；主 pass 自家
    // BeginRendering 会以 LoadOp::Clear depth = 1.0 重建 sceneDepth。

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

    cmd.BindGraphicsPipeline(*skyPipeline);
    cmd.SetDescriptorSet(0, *skySet);

    // push constant 96 字节：mat4 invVP + vec3 cameraPos + float intensity
    // + vec3 tint + float pad；与 sky.frag.glsl push_constant block 严格对齐。
    struct SkyPush
    {
        glm::mat4 invViewProj;
        glm::vec3 cameraPos;
        float     intensity;
        glm::vec3 tint;
        float     pad0;
    };
    static_assert(sizeof(SkyPush) == 96,
                  "SkyPush must match sky.frag push_constant block (96 B).");

    SkyPush push{};
    push.invViewProj = invViewProj;
    push.cameraPos   = cameraPos;
    push.intensity   = intensity;
    push.tint        = tint;
    cmd.SetPushConstants(Orange::Rhi::ShaderStage::Fragment, 0,
                         sizeof(SkyPush), &push);

    cmd.Draw(3, 1, 0, 0);
    cmd.EndRendering();

    // hdrColor 留在 ColorAttachment —— 主 pass 入口接 LoadOp::Load 直接
    // 沿用本帧 sky 输出作背景。
    return true;
}

bool Pipeline::Impl::RecordProceduralSkyPass(const glm::mat4& invViewProj,
                                             const glm::vec3& cameraPos,
                                             const glm::vec3& sunDir,
                                             const glm::vec3& sunColor,
                                             float            sunIntensity)
{
    ORANGE_PROFILE_SCOPE("Sky");
    if (offscreenCmd == nullptr || hdrColor == nullptr || proceduralSkyPipeline == nullptr)
    {
        return false;
    }

    auto& cmd = *offscreenCmd;

    // hdrColor transition：与 RecordSkyPass 同款入口契约（上一帧通常翻在
    // ShaderReadOnly；首帧 Undefined）。
    const auto fromLayout = hdrLayoutShaderReadOnly
        ? Orange::Rhi::TextureLayout::ShaderReadOnly
        : Orange::Rhi::TextureLayout::Undefined;
    cmd.TransitionTexture(*hdrColor, fromLayout,
                          Orange::Rhi::TextureLayout::ColorAttachment);
    hdrLayoutShaderReadOnly = false;

    // BUG-2026-05-19-vk-cmd-begin-rendering-crash-on-intel-iris-xe workaround：
    // procedural sky pipeline 声明了 depth attachment format（见 SetupRhiResources），
    // RenderingDesc 必须对齐挂 sceneDepth；否则 Intel Iris Xe ICD 走 color-only
    // dynamic rendering 路径 deref 0x3B0 段错误。本 transition / attachment
    // 是 pure workaround，shader 不消费 depth、depth test / write 均关。NV 上
    // 行为不变（with-depth 路径也 well-tested）。
    const auto fromDepthLayout = sceneDepthLayoutShaderReadOnly
        ? Orange::Rhi::TextureLayout::ShaderReadOnly
        : Orange::Rhi::TextureLayout::Undefined;
    cmd.TransitionTexture(*sceneDepth, fromDepthLayout,
                          Orange::Rhi::TextureLayout::DepthStencilAttachment);
    sceneDepthLayoutShaderReadOnly = false;

    Orange::Rhi::ColorAttachment att{};
    att.mpView          = hdrColor->GetDefaultView();
    att.mLoadOp         = Orange::Rhi::LoadOp::Clear;
    att.mStoreOp        = Orange::Rhi::StoreOp::Store;
    // Sky pass 入口 clear —— shader 会全覆盖，clear 值仅用于驱动 spec
    // requirement，不影响最终视觉。两路径写值统一为深蓝 (0.05, 0.07, 0.10)
    // 作中性默认（与 shipping ClearColor 一致），避免编辑器审美污染入口。
    att.mClear.mColor[0] = 0.05f;
    att.mClear.mColor[1] = 0.07f;
    att.mClear.mColor[2] = 0.10f;
    att.mClear.mColor[3] = 1.0f;

    // Dummy depth attachment（pipeline 已声明 D32 format，此处必须配对）。
    // LoadOp=Clear 把 depth 清到 far（1.0），与主 pass 行为同款 —— 主 pass 之
    // 后接的 RecordOffscreenPass 仍会 Clear depth，本 sky pass 写的 depth 值
    // 被主 pass 抹掉，纯占位无副作用。StoreOp=Store 让主 pass 入口 transition
    // 起点合法（也可 DontCare，但 Store 与现有 pattern 一致）。
    Orange::Rhi::DepthStencilAttachment depthAtt{};
    depthAtt.mpView         = sceneDepth->GetDefaultView();
    depthAtt.mDepthLoadOp   = Orange::Rhi::LoadOp::Clear;
    depthAtt.mDepthStoreOp  = Orange::Rhi::StoreOp::Store;
    depthAtt.mClear.mDepth  = 1.0f;

    Orange::Rhi::RenderingDesc rd{};
    rd.mRenderArea.mWidth  = hdrWidth;
    rd.mRenderArea.mHeight = hdrHeight;
    rd.mColorAttachments.push_back(att);
    rd.mDepthStencil       = depthAtt;

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

    cmd.BindGraphicsPipeline(*proceduralSkyPipeline);

    // push 112 字节，必须与 procedural_sky.frag.glsl 内 push_constant block
    // 严格对齐（mat4 + 3 组 vec3+float，std430 自然 16B 对齐）。zenith /
    // horizon palette 在 shader 内 hardcode（v0.x baseline），未来扩展时
    // 升级为 push 字段或 UBO。
    struct ProceduralSkyPush
    {
        glm::mat4 invViewProj;     // 64
        glm::vec3 cameraPos;       // 12
        float     pad0;            //  4
        glm::vec3 sunDir;          // 12
        float     sunSize;         //  4
        glm::vec3 sunColor;        // 12
        float     sunIntensity;    //  4
    };
    static_assert(sizeof(ProceduralSkyPush) == 112,
                  "ProceduralSkyPush must match procedural_sky.frag push_constant (112 B).");

    ProceduralSkyPush push{};
    push.invViewProj = invViewProj;
    push.cameraPos   = cameraPos;
    push.sunDir      = sunDir;
    // sunSize 是 disc 阈值的 cos 值，越接近 1 = 越小。0.9995 ≈ 视角 ~1.8°，
    // 比真实太阳（~0.53°）偏大，编辑器观感优先 —— 用户能清楚看到一个圆盘
    // 而非"针尖"。
    push.sunSize      = 0.9995f;
    push.sunColor     = sunColor;
    push.sunIntensity = sunIntensity;

    cmd.SetPushConstants(Orange::Rhi::ShaderStage::Fragment, 0,
                         sizeof(ProceduralSkyPush), &push);

    cmd.Draw(3, 1, 0, 0);
    cmd.EndRendering();

    // hdrColor 留在 ColorAttachment，主 pass 接 LoadOp::Load。
    return true;
}

bool Pipeline::Impl::RecordGridPass(const glm::mat4& invViewProj,
                                    const glm::mat4& viewProj)
{
    ORANGE_PROFILE_SCOPE("Grid");
    if (offscreenCmd == nullptr || hdrColor == nullptr || sceneDepth == nullptr
        || gridPipeline == nullptr || gridLayout == nullptr || hdrSampler == nullptr)
    {
        return false;
    }

    auto& rhi = renderDevice->GetRhiDevice();
    auto& cmd = *offscreenCmd;

    // 1. grid descriptor set lazy create + re-bind sceneDepth：
    //    池 + set 都只 alloc 一次（避免 ImGui viewport 启动期 N 次 resize 让
    //    sceneDepth 反复重建 → reset+realloc 撞 OUT_OF_POOL_MEMORY，Vulkan
    //    默认 pool 不支持 free 单 set）。sceneDepth 重建只走 UpdateDescriptorSet
    //    重写 binding，Pipeline::Render 末尾 WaitIdle 保证前帧 set 不再 in-flight。
    if (!gridPool)
    {
        Orange::Rhi::DescriptorPoolDesc poolDesc{};
        poolDesc.mMaxSets = 1;
        Orange::Rhi::DescriptorPoolSize sz{};
        sz.mType  = Orange::Rhi::DescriptorType::CombinedImageSampler;
        sz.mCount = 1;
        poolDesc.mPoolSizes.push_back(sz);
        poolDesc.mpDebugName = "orange_engine.grid.pool";
        gridPool = rhi.CreateDescriptorPool(poolDesc);
        if (!gridPool)
        {
            ORANGE_LOG_ERROR("Pipeline: grid CreateDescriptorPool 失败");
            return false;
        }
    }
    if (!gridSet)
    {
        auto set = rhi.AllocateDescriptorSet(*gridPool, *gridLayout);
        if (!set)
        {
            ORANGE_LOG_ERROR("Pipeline: grid AllocateDescriptorSet 失败");
            return false;
        }
        gridSet = std::move(set);
        gridSetBoundDepth = nullptr;  // 强制下面走 UpdateDescriptorSet
    }
    if (gridSetBoundDepth != sceneDepth.get())
    {
        Orange::Rhi::DescriptorWrite w{};
        w.mBinding             = 0;
        w.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
        w.mImageInfo.mpTexture = sceneDepth.get();
        w.mImageInfo.mpSampler = hdrSampler.get();
        rhi.UpdateDescriptorSet(*gridSet, &w, 1);
        gridSetBoundDepth = sceneDepth.get();
    }

    // 2. sceneDepth → ShaderReadOnly 供采样。主 pass 末尾留在 DSA；
    //    god rays（如果启用）已翻 ShaderReadOnly。
    if (!sceneDepthLayoutShaderReadOnly)
    {
        cmd.TransitionTexture(*sceneDepth,
                              Orange::Rhi::TextureLayout::DepthStencilAttachment,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
        sceneDepthLayoutShaderReadOnly = true;
    }

    // 3. hdrColor: ShaderReadOnly → ColorAttachment 准备 alpha-blend 上 grid。
    cmd.TransitionTexture(*hdrColor,
                          Orange::Rhi::TextureLayout::ShaderReadOnly,
                          Orange::Rhi::TextureLayout::ColorAttachment);

    Orange::Rhi::ColorAttachment colorAtt{};
    colorAtt.mpView   = hdrColor->GetDefaultView();
    colorAtt.mLoadOp  = Orange::Rhi::LoadOp::Load;
    colorAtt.mStoreOp = Orange::Rhi::StoreOp::Store;

    Orange::Rhi::RenderingDesc rd{};
    rd.mRenderArea.mWidth  = hdrWidth;
    rd.mRenderArea.mHeight = hdrHeight;
    rd.mColorAttachments.push_back(colorAtt);
    // 不挂 depth attachment —— shader 自己采 sceneDepth + 手动比较 + discard。

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

    cmd.BindGraphicsPipeline(*gridPipeline);
    cmd.SetDescriptorSet(0, *gridSet);

    struct GridPush
    {
        glm::mat4 invViewProj;
        glm::mat4 viewProj;
    };
    static_assert(sizeof(GridPush) == 128,
                  "GridPush must match grid.frag push_constant block (128 B).");

    GridPush push{};
    push.invViewProj = invViewProj;
    push.viewProj    = viewProj;
    cmd.SetPushConstants(Orange::Rhi::ShaderStage::Fragment, 0,
                         sizeof(GridPush), &push);

    cmd.Draw(3, 1, 0, 0);
    cmd.EndRendering();

    // 4. hdrColor 翻回 ShaderReadOnly：下一段（bloom / passthrough / tonemap）
    //    按 ShaderReadOnly 假设跑。sceneDepth 留在 ShaderReadOnly —— 下一
    //    帧主 pass 入口看 sceneDepthLayoutShaderReadOnly == true 会正确
    //    transition 回 DSA。
    cmd.TransitionTexture(*hdrColor,
                          Orange::Rhi::TextureLayout::ColorAttachment,
                          Orange::Rhi::TextureLayout::ShaderReadOnly);
    hdrLayoutShaderReadOnly = true;
    return true;
}

bool Pipeline::Impl::RecordDebugDrawPass(const glm::mat4& viewProj)
{
    ORANGE_PROFILE_SCOPE("DebugDraw");
    if (!debugDrawScene || !debugDrawScene->IsInitialized())
    {
        return false;
    }
    if (!debugDrawScene->IsEnabled() || debugDrawScene->IsEmptyBackend_())
    {
        // 当前帧无几何 / 整条 wrap 已 disable —— silent skip，不付 BeginRendering
        // 开销。SetViewProjBackend_ 即使不调，Flush silent 内 OR side 自然 skip。
        return true;
    }
    if (offscreenCmd == nullptr || hdrColor == nullptr)
    {
        return false;
    }

    auto& cmd = *offscreenCmd;

    // hdrColor: ShaderReadOnly → ColorAttachment 准备叠加 debug 几何。
    if (hdrLayoutShaderReadOnly)
    {
        cmd.TransitionTexture(*hdrColor,
                              Orange::Rhi::TextureLayout::ShaderReadOnly,
                              Orange::Rhi::TextureLayout::ColorAttachment);
        hdrLayoutShaderReadOnly = false;
    }

    Orange::Rhi::ColorAttachment colorAtt{};
    colorAtt.mpView   = hdrColor->GetDefaultView();
    colorAtt.mLoadOp  = Orange::Rhi::LoadOp::Load;
    colorAtt.mStoreOp = Orange::Rhi::StoreOp::Store;

    Orange::Rhi::RenderingDesc rd{};
    rd.mRenderArea.mWidth  = hdrWidth;
    rd.mRenderArea.mHeight = hdrHeight;
    rd.mColorAttachments.push_back(colorAtt);
    // 不挂 depth attachment —— DebugDraw pipeline 自身 depth-test 关，
    // 视觉上 always-on-top，与 Lumix / Godot debug viewport 同款节奏。

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

    debugDrawScene->SetViewProjBackend_(viewProj);
    debugDrawScene->FlushBackend_(cmd);

    cmd.EndRendering();

    // hdrColor 翻回 ShaderReadOnly：与 RecordGridPass 收尾同款契约，
    // 下一段 pass（bloom / passthrough / tonemap）按 ShaderReadOnly 假设跑。
    cmd.TransitionTexture(*hdrColor,
                          Orange::Rhi::TextureLayout::ColorAttachment,
                          Orange::Rhi::TextureLayout::ShaderReadOnly);
    hdrLayoutShaderReadOnly = true;
    return true;
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
    const BloomPass*   activeBloom   = impl.FindActiveBloomPass();
    const TonemapPass* activeTonemap = impl.FindActiveTonemapPass();
    const GodRaysPass* activeGodRays = impl.FindActiveGodRaysPass();
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
        const glm::mat4 lightVP = activeLight ? impl.ComputeLightViewProj(activeLightDir)
                                              : glm::mat4(1.0f);
        // 相机 worldPos：scene.MainCamera().view 是 world→view 矩阵，
        // 取 inverse 后的第 4 列即为相机在 world 中的位置。供 rim_light
        // / 后续 specular 类 fragment 取真 viewDir + sky-dome pass 反推。
        const glm::mat4 invView   = glm::inverse(impl.scene.MainCamera().view);
        cameraWorldPos            = glm::vec3(invView[3]);
        impl.UpdateLightUbo(activeLight, activeLightDir, lightVP, cameraWorldPos, iblTintIntensity);
        impl.UpdatePointLightsUbo(world);
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
            const glm::mat4 viewProj =
                impl.scene.MainCamera().projection * impl.scene.MainCamera().view;
            const glm::mat4 lightVP =
                activeLight ? impl.ComputeLightViewProj(activeLightDir) : glm::mat4(1.0f);

            // Shadow 预 pass：在主 pass 之前把场景从 light 视角渲到
            // shadow map（depth-only）。无 light 时跳过实际绘制，只清深度。
            if (impl.shadowMap)
            {
                offscreenOk = impl.RecordShadowPass(activeLight, lightVP);
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

            // grid pass：主 pass + 粒子 + AfterMainPass inserted 都画完后、
            // bloom 之前。grid 走 alpha-blend 叠加 + DepthTest Less → 几何
            // 把 grid 自然遮挡。editorGridEnabled = false 时 silent skip。
            if (offscreenOk && impl.editorGridEnabled)
            {
                const glm::mat4 invViewProjGrid = glm::inverse(viewProj);
                impl.RecordGridPass(invViewProjGrid, viewProj);
            }

            // debug draw pass：grid 之后、bloom 之前。wrap 内自管空几何 /
            // disabled silent skip。
            if (offscreenOk)
            {
                impl.RecordDebugDrawPass(viewProj);
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
