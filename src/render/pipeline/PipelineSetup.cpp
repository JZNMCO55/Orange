// Pipeline::SetupRhiResources —— 共享 RHI 资源创建路径（sampler /
// passthrough / bloom / tonemap / godrays / sky / grid / main pass UBO /
// shadow caster pipeline / dummy IBL / offscreen cmd list / DebugDrawScene
// backend）。Initialize 与 InitializeOffscreen 两条入口都调用本函数；
// 调用前必须保证 `mpImpl->renderDevice` + `mpImpl->upload` + `mpImpl->assets`
// 已就位。失败路径内部已调 Shutdown() 整体回滚。

#include "orange/engine/render/Pipeline.h"

#include "PipelineImpl.h"

#include "orange/engine/render/BuiltinShadowShaders.h"

#include <glm/geometric.hpp>

#include <cstring>
#include <random>

namespace Orange::Engine::Render
{

using namespace PipelineDetail;

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
        if (downCode.empty() || upCode.empty() || combineCode.empty()
            || tonemapVsCode.empty() || tonemapFsCode.empty()
            || godRaysCode.empty()
            || skyCode.empty() || proceduralSkyCode.empty())
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

        if (!impl.bloomDownsampleFs || !impl.bloomUpsampleFs || !impl.passthroughCombineFs
            || !impl.tonemapVs || !impl.tonemapFs || !impl.godRaysFs
            || !impl.skyFs || !impl.proceduralSkyFs)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: bloom / tonemap / god_rays / sky / "
                             "procedural_sky shader 模块创建失败");
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

        if (!impl.skyLayout)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: sky DescriptorSetLayout 创建失败");
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
        // SSAO pipelines（屏幕空间环境光遮蔽）：
        //   ssao       —— sceneDepth + ubo → R8 原始 AO（no blend）；
        //   ssao_apply —— ssaoColor 4×4 模糊 → 乘法 blend（dst×src）进 HDR。
        auto ssaoCode      = LoadSpirv("shaders/orange_engine/ssao.frag.spv");
        auto ssaoApplyCode = LoadSpirv("shaders/orange_engine/ssao_apply.frag.spv");
        auto gtaoCode      = LoadSpirv("shaders/orange_engine/gtao.frag.spv");
        if (ssaoCode.empty() || ssaoApplyCode.empty() || gtaoCode.empty())
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: SSAO / GTAO shader .spv 加载失败");
            Shutdown();
            return ResultCode::InternalError;
        }
        Orange::Rhi::ShaderModuleDesc sm{};
        sm.mStage      = Orange::Rhi::ShaderStage::Fragment;
        sm.mpCode      = ssaoCode.data();
        sm.mCodeSize   = ssaoCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_engine.ssao.frag";
        impl.ssaoFs = rhi.CreateShaderModule(sm);
        sm.mpCode      = ssaoApplyCode.data();
        sm.mCodeSize   = ssaoApplyCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_engine.ssao_apply.frag";
        impl.ssaoApplyFs = rhi.CreateShaderModule(sm);
        sm.mpCode      = gtaoCode.data();
        sm.mCodeSize   = gtaoCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_engine.gtao.frag";
        impl.gtaoFs = rhi.CreateShaderModule(sm);

        // ssaoLayout：0 = sceneDepth sampler，1 = SsaoUbo，2 = normalBuffer sampler。
        Orange::Rhi::DescriptorSetLayoutDesc lay{};
        lay.mBindings.push_back({0, Orange::Rhi::DescriptorType::CombinedImageSampler,
                                 1, Orange::Rhi::ShaderStage::Fragment});
        lay.mBindings.push_back({1, Orange::Rhi::DescriptorType::UniformBuffer,
                                 1, Orange::Rhi::ShaderStage::Fragment});
        lay.mBindings.push_back({2, Orange::Rhi::DescriptorType::CombinedImageSampler,
                                 1, Orange::Rhi::ShaderStage::Fragment});
        lay.mpDebugName = "orange_engine.ssao.layout";
        impl.ssaoLayout = rhi.CreateDescriptorSetLayout(lay);

        Orange::Rhi::BufferDesc ub{};
        ub.mSize        = sizeof(Pipeline::Impl::SsaoUboData);
        ub.mUsage       = Orange::Rhi::BufferUsage::Uniform;
        ub.mMemoryUsage = Orange::Rhi::MemoryUsage::CpuToGpu;
        impl.ssaoUbo = rhi.CreateBuffer(ub);

        if (!impl.ssaoFs || !impl.ssaoApplyFs || !impl.gtaoFs
            || !impl.ssaoLayout || !impl.ssaoUbo)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: SSAO 资源创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }

        {
            // ssao pipeline → R8Unorm AO target，no blend，layout = ssaoLayout。
            Orange::Rhi::GraphicsPipelineDesc d{};
            d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,
                                       impl.fullscreenVs.get(), "main"});
            d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment,
                                       impl.ssaoFs.get(), "main"});
            d.mInputAssembly.mTopology        = Orange::Rhi::PrimitiveTopology::TriangleList;
            d.mRasterizer.mCullMode           = Orange::Rhi::CullMode::None;
            d.mDepthStencil.mDepthTestEnable  = false;
            d.mDepthStencil.mDepthWriteEnable = false;
            d.mColorBlend.mAttachments.push_back({});  // no blend
            d.mRenderTargets.mColorFormats.push_back(Orange::Rhi::TextureFormat::R8Unorm);
            d.mDescriptorSetLayouts.push_back(impl.ssaoLayout.get());
            d.mpDebugName = "orange_engine.ssao";
            impl.ssaoPipeline = rhi.CreateGraphicsPipeline(d);
        }
        {
            // gtao pipeline → 与 ssao pipeline 同 attachment / layout / ssaoSet，
            // 仅 fragment 换 gtao.frag（horizon-based）。RecordSsaoPass 按
            // SsaoPass.useGtao 选 ssaoPipeline / gtaoPipeline。
            Orange::Rhi::GraphicsPipelineDesc d{};
            d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,
                                       impl.fullscreenVs.get(), "main"});
            d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment,
                                       impl.gtaoFs.get(), "main"});
            d.mInputAssembly.mTopology        = Orange::Rhi::PrimitiveTopology::TriangleList;
            d.mRasterizer.mCullMode           = Orange::Rhi::CullMode::None;
            d.mDepthStencil.mDepthTestEnable  = false;
            d.mDepthStencil.mDepthWriteEnable = false;
            d.mColorBlend.mAttachments.push_back({});  // no blend
            d.mRenderTargets.mColorFormats.push_back(Orange::Rhi::TextureFormat::R8Unorm);
            d.mDescriptorSetLayouts.push_back(impl.ssaoLayout.get());
            d.mpDebugName = "orange_engine.gtao";
            impl.gtaoPipeline = rhi.CreateGraphicsPipeline(d);
        }
        {
            // ssao_apply pipeline → HDR target，乘法 blend（out = dst×src =
            // HDR×AO）；layout 复用 bloomLayout（1 binding sampler = ssaoColor）。
            Orange::Rhi::GraphicsPipelineDesc d{};
            d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,
                                       impl.fullscreenVs.get(), "main"});
            d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment,
                                       impl.ssaoApplyFs.get(), "main"});
            d.mInputAssembly.mTopology        = Orange::Rhi::PrimitiveTopology::TriangleList;
            d.mRasterizer.mCullMode           = Orange::Rhi::CullMode::None;
            d.mDepthStencil.mDepthTestEnable  = false;
            d.mDepthStencil.mDepthWriteEnable = false;
            Orange::Rhi::ColorBlendAttachmentDesc blend{};
            blend.mBlendEnable         = true;
            blend.mSrcColorBlendFactor = Orange::Rhi::BlendFactor::Zero;
            blend.mDstColorBlendFactor = Orange::Rhi::BlendFactor::SrcColor;  // dst×src
            blend.mColorBlendOp        = Orange::Rhi::BlendOp::Add;
            blend.mSrcAlphaBlendFactor = Orange::Rhi::BlendFactor::Zero;
            blend.mDstAlphaBlendFactor = Orange::Rhi::BlendFactor::One;
            blend.mAlphaBlendOp        = Orange::Rhi::BlendOp::Add;
            d.mColorBlend.mAttachments.push_back(blend);
            d.mRenderTargets.mColorFormats.push_back(kHdrColorFormat);
            d.mDescriptorSetLayouts.push_back(impl.bloomLayout.get());
            d.mpDebugName = "orange_engine.ssao_apply";
            impl.ssaoApplyPipeline = rhi.CreateGraphicsPipeline(d);
        }
        if (!impl.ssaoPipeline || !impl.gtaoPipeline || !impl.ssaoApplyPipeline)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: SSAO pipeline 创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }

        // 半球 kernel（一次性、确定性 seed）：半球内随机方向 × 随机长度，
        // scale 用 t² 向中心加速分布（近处样本密、远处疏，经典 SSAO 取法）。
        std::mt19937                          rng(1337u);
        std::uniform_real_distribution<float> d01(0.0f, 1.0f);
        std::uniform_real_distribution<float> dn1(-1.0f, 1.0f);
        for (std::uint32_t i = 0; i < Pipeline::Impl::kSsaoKernelSize; ++i)
        {
            glm::vec3 s(dn1(rng), dn1(rng), d01(rng));  // 半球 z>=0
            s = glm::normalize(s) * d01(rng);
            const float t     = static_cast<float>(i)
                              / static_cast<float>(Pipeline::Impl::kSsaoKernelSize);
            const float scale = 0.1f + 0.9f * (t * t);
            s *= scale;
            impl.ssaoKernel[i] = glm::vec4(s, 0.0f);
        }
    }
    {
        // SSR pipelines（屏幕空间反射）：
        //   ssr           —— sceneDepth + hdrColor + ubo → ssrColor（反射色×权重，
        //                     RGBA16F，no blend）；
        //   ssr_composite —— ssrColor 加性 blend 进 HDR。
        auto ssrCode      = LoadSpirv("shaders/orange_engine/ssr.frag.spv");
        auto ssrCompCode  = LoadSpirv("shaders/orange_engine/ssr_composite.frag.spv");
        if (ssrCode.empty() || ssrCompCode.empty())
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: SSR shader .spv 加载失败");
            Shutdown();
            return ResultCode::InternalError;
        }
        Orange::Rhi::ShaderModuleDesc sm{};
        sm.mStage      = Orange::Rhi::ShaderStage::Fragment;
        sm.mpCode      = ssrCode.data();
        sm.mCodeSize   = ssrCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_engine.ssr.frag";
        impl.ssrFs = rhi.CreateShaderModule(sm);
        sm.mpCode      = ssrCompCode.data();
        sm.mCodeSize   = ssrCompCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_engine.ssr_composite.frag";
        impl.ssrCompositeFs = rhi.CreateShaderModule(sm);

        // ssrLayout：0 = sceneDepth，1 = hdrColor，2 = SsrUbo，3 = normalBuffer sampler。
        Orange::Rhi::DescriptorSetLayoutDesc lay{};
        lay.mBindings.push_back({0, Orange::Rhi::DescriptorType::CombinedImageSampler,
                                 1, Orange::Rhi::ShaderStage::Fragment});
        lay.mBindings.push_back({1, Orange::Rhi::DescriptorType::CombinedImageSampler,
                                 1, Orange::Rhi::ShaderStage::Fragment});
        lay.mBindings.push_back({2, Orange::Rhi::DescriptorType::UniformBuffer,
                                 1, Orange::Rhi::ShaderStage::Fragment});
        lay.mBindings.push_back({3, Orange::Rhi::DescriptorType::CombinedImageSampler,
                                 1, Orange::Rhi::ShaderStage::Fragment});
        lay.mpDebugName = "orange_engine.ssr.layout";
        impl.ssrLayout = rhi.CreateDescriptorSetLayout(lay);

        Orange::Rhi::BufferDesc ub{};
        ub.mSize        = sizeof(Pipeline::Impl::SsrUboData);
        ub.mUsage       = Orange::Rhi::BufferUsage::Uniform;
        ub.mMemoryUsage = Orange::Rhi::MemoryUsage::CpuToGpu;
        impl.ssrUbo = rhi.CreateBuffer(ub);

        if (!impl.ssrFs || !impl.ssrCompositeFs || !impl.ssrLayout || !impl.ssrUbo)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: SSR 资源创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }

        {
            // ssr pipeline → RGBA16F ssrColor，no blend，layout = ssrLayout。
            Orange::Rhi::GraphicsPipelineDesc d{};
            d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,
                                       impl.fullscreenVs.get(), "main"});
            d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment,
                                       impl.ssrFs.get(), "main"});
            d.mInputAssembly.mTopology        = Orange::Rhi::PrimitiveTopology::TriangleList;
            d.mRasterizer.mCullMode           = Orange::Rhi::CullMode::None;
            d.mDepthStencil.mDepthTestEnable  = false;
            d.mDepthStencil.mDepthWriteEnable = false;
            d.mColorBlend.mAttachments.push_back({});  // no blend
            d.mRenderTargets.mColorFormats.push_back(kHdrColorFormat);
            d.mDescriptorSetLayouts.push_back(impl.ssrLayout.get());
            d.mpDebugName = "orange_engine.ssr";
            impl.ssrPipeline = rhi.CreateGraphicsPipeline(d);
        }
        {
            // ssr_composite pipeline → HDR，加性 blend（与 bloom upsample / god
            // rays 同款）；layout 复用 bloomLayout（1 binding = ssrColor）。
            Orange::Rhi::GraphicsPipelineDesc d{};
            d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,
                                       impl.fullscreenVs.get(), "main"});
            d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment,
                                       impl.ssrCompositeFs.get(), "main"});
            d.mInputAssembly.mTopology        = Orange::Rhi::PrimitiveTopology::TriangleList;
            d.mRasterizer.mCullMode           = Orange::Rhi::CullMode::None;
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
            d.mpDebugName = "orange_engine.ssr_composite";
            impl.ssrCompositePipeline = rhi.CreateGraphicsPipeline(d);
        }
        if (!impl.ssrPipeline || !impl.ssrCompositePipeline)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: SSR pipeline 创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }
    }
    {
        // 接触阴影 pipeline —— sceneDepth + ubo → 阴影因子，乘法 blend 直接进
        // HDR（与 ssao_apply 同款 dst×src blend，无独立 target）。单 pass。
        auto csCode = LoadSpirv("shaders/orange_engine/contact_shadow.frag.spv");
        if (csCode.empty())
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: contact_shadow shader .spv 加载失败");
            Shutdown();
            return ResultCode::InternalError;
        }
        Orange::Rhi::ShaderModuleDesc sm{};
        sm.mStage      = Orange::Rhi::ShaderStage::Fragment;
        sm.mpCode      = csCode.data();
        sm.mCodeSize   = csCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_engine.contact_shadow.frag";
        impl.contactShadowFs = rhi.CreateShaderModule(sm);

        // contactShadowLayout：0 = sceneDepth sampler，1 = CsUbo，2 = normalBuffer sampler。
        Orange::Rhi::DescriptorSetLayoutDesc lay{};
        lay.mBindings.push_back({0, Orange::Rhi::DescriptorType::CombinedImageSampler,
                                 1, Orange::Rhi::ShaderStage::Fragment});
        lay.mBindings.push_back({1, Orange::Rhi::DescriptorType::UniformBuffer,
                                 1, Orange::Rhi::ShaderStage::Fragment});
        lay.mBindings.push_back({2, Orange::Rhi::DescriptorType::CombinedImageSampler,
                                 1, Orange::Rhi::ShaderStage::Fragment});
        lay.mpDebugName = "orange_engine.contact_shadow.layout";
        impl.contactShadowLayout = rhi.CreateDescriptorSetLayout(lay);

        Orange::Rhi::BufferDesc ub{};
        ub.mSize        = sizeof(Pipeline::Impl::ContactShadowUboData);
        ub.mUsage       = Orange::Rhi::BufferUsage::Uniform;
        ub.mMemoryUsage = Orange::Rhi::MemoryUsage::CpuToGpu;
        impl.contactShadowUbo = rhi.CreateBuffer(ub);

        if (!impl.contactShadowFs || !impl.contactShadowLayout || !impl.contactShadowUbo)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: 接触阴影资源创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }

        // pipeline → HDR target，乘法 blend（out = dst×src = HDR×shadow）。
        Orange::Rhi::GraphicsPipelineDesc d{};
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,
                                   impl.fullscreenVs.get(), "main"});
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment,
                                   impl.contactShadowFs.get(), "main"});
        d.mInputAssembly.mTopology        = Orange::Rhi::PrimitiveTopology::TriangleList;
        d.mRasterizer.mCullMode           = Orange::Rhi::CullMode::None;
        d.mDepthStencil.mDepthTestEnable  = false;
        d.mDepthStencil.mDepthWriteEnable = false;
        Orange::Rhi::ColorBlendAttachmentDesc blend{};
        blend.mBlendEnable         = true;
        blend.mSrcColorBlendFactor = Orange::Rhi::BlendFactor::Zero;
        blend.mDstColorBlendFactor = Orange::Rhi::BlendFactor::SrcColor;  // dst×src
        blend.mColorBlendOp        = Orange::Rhi::BlendOp::Add;
        blend.mSrcAlphaBlendFactor = Orange::Rhi::BlendFactor::Zero;
        blend.mDstAlphaBlendFactor = Orange::Rhi::BlendFactor::One;
        blend.mAlphaBlendOp        = Orange::Rhi::BlendOp::Add;
        d.mColorBlend.mAttachments.push_back(blend);
        d.mRenderTargets.mColorFormats.push_back(kHdrColorFormat);
        d.mDescriptorSetLayouts.push_back(impl.contactShadowLayout.get());
        d.mpDebugName = "orange_engine.contact_shadow";
        impl.contactShadowPipeline = rhi.CreateGraphicsPipeline(d);
        if (!impl.contactShadowPipeline)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: 接触阴影 pipeline 创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }
    }
    {
        // 景深（DoF）pipelines：
        //   dof           —— hdrColor + depth + ubo → dofColor（CoC 圆盘 gather）；
        //   dof_composite —— dofColor → HDR（replace blend，复用 bloomLayout）。
        auto dofCode     = LoadSpirv("shaders/orange_engine/dof.frag.spv");
        auto dofCompCode = LoadSpirv("shaders/orange_engine/dof_composite.frag.spv");
        if (dofCode.empty() || dofCompCode.empty())
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: DoF shader .spv 加载失败");
            Shutdown();
            return ResultCode::InternalError;
        }
        Orange::Rhi::ShaderModuleDesc sm{};
        sm.mStage      = Orange::Rhi::ShaderStage::Fragment;
        sm.mpCode      = dofCode.data();
        sm.mCodeSize   = dofCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_engine.dof.frag";
        impl.dofFs = rhi.CreateShaderModule(sm);
        sm.mpCode      = dofCompCode.data();
        sm.mCodeSize   = dofCompCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_engine.dof_composite.frag";
        impl.dofCompositeFs = rhi.CreateShaderModule(sm);

        // dofLayout：0 = hdrColor，1 = DofUbo，2 = sceneDepth。
        Orange::Rhi::DescriptorSetLayoutDesc lay{};
        lay.mBindings.push_back({0, Orange::Rhi::DescriptorType::CombinedImageSampler,
                                 1, Orange::Rhi::ShaderStage::Fragment});
        lay.mBindings.push_back({1, Orange::Rhi::DescriptorType::UniformBuffer,
                                 1, Orange::Rhi::ShaderStage::Fragment});
        lay.mBindings.push_back({2, Orange::Rhi::DescriptorType::CombinedImageSampler,
                                 1, Orange::Rhi::ShaderStage::Fragment});
        lay.mpDebugName = "orange_engine.dof.layout";
        impl.dofLayout = rhi.CreateDescriptorSetLayout(lay);

        Orange::Rhi::BufferDesc ub{};
        ub.mSize        = sizeof(Pipeline::Impl::DofUboData);
        ub.mUsage       = Orange::Rhi::BufferUsage::Uniform;
        ub.mMemoryUsage = Orange::Rhi::MemoryUsage::CpuToGpu;
        impl.dofUbo = rhi.CreateBuffer(ub);

        if (!impl.dofFs || !impl.dofCompositeFs || !impl.dofLayout || !impl.dofUbo)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: DoF 资源创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }

        {
            // dof gather pipeline → RGBA16F dofColor，no blend。
            Orange::Rhi::GraphicsPipelineDesc d{};
            d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,
                                       impl.fullscreenVs.get(), "main"});
            d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment,
                                       impl.dofFs.get(), "main"});
            d.mInputAssembly.mTopology        = Orange::Rhi::PrimitiveTopology::TriangleList;
            d.mRasterizer.mCullMode           = Orange::Rhi::CullMode::None;
            d.mDepthStencil.mDepthTestEnable  = false;
            d.mDepthStencil.mDepthWriteEnable = false;
            d.mColorBlend.mAttachments.push_back({});  // no blend
            d.mRenderTargets.mColorFormats.push_back(kHdrColorFormat);
            d.mDescriptorSetLayouts.push_back(impl.dofLayout.get());
            d.mpDebugName = "orange_engine.dof";
            impl.dofPipeline = rhi.CreateGraphicsPipeline(d);
        }
        {
            // dof_composite pipeline → HDR，replace blend（One/Zero），复用 bloomLayout。
            Orange::Rhi::GraphicsPipelineDesc d{};
            d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,
                                       impl.fullscreenVs.get(), "main"});
            d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment,
                                       impl.dofCompositeFs.get(), "main"});
            d.mInputAssembly.mTopology        = Orange::Rhi::PrimitiveTopology::TriangleList;
            d.mRasterizer.mCullMode           = Orange::Rhi::CullMode::None;
            d.mDepthStencil.mDepthTestEnable  = false;
            d.mDepthStencil.mDepthWriteEnable = false;
            d.mColorBlend.mAttachments.push_back({});  // no blend = replace（src 覆盖 dst）
            d.mRenderTargets.mColorFormats.push_back(kHdrColorFormat);
            d.mDescriptorSetLayouts.push_back(impl.bloomLayout.get());
            d.mpDebugName = "orange_engine.dof_composite";
            impl.dofCompositePipeline = rhi.CreateGraphicsPipeline(d);
        }
        if (!impl.dofPipeline || !impl.dofCompositePipeline)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: DoF pipeline 创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }
    }
    {
        // TAA resolve pipeline —— current HDR + prevHistory + depth + ubo →
        // curHistory（RGBA16F，no blend）。copy 步骤复用 dofCompositePipeline。
        auto taaCode = LoadSpirv("shaders/orange_engine/taa.frag.spv");
        if (taaCode.empty())
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: TAA shader .spv 加载失败");
            Shutdown();
            return ResultCode::InternalError;
        }
        Orange::Rhi::ShaderModuleDesc sm{};
        sm.mStage      = Orange::Rhi::ShaderStage::Fragment;
        sm.mpCode      = taaCode.data();
        sm.mCodeSize   = taaCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_engine.taa.frag";
        impl.taaResolveFs = rhi.CreateShaderModule(sm);

        // taaLayout：0 = current HDR，1 = prevHistory，2 = sceneDepth，3 = TaaUbo。
        Orange::Rhi::DescriptorSetLayoutDesc lay{};
        lay.mBindings.push_back({0, Orange::Rhi::DescriptorType::CombinedImageSampler,
                                 1, Orange::Rhi::ShaderStage::Fragment});
        lay.mBindings.push_back({1, Orange::Rhi::DescriptorType::CombinedImageSampler,
                                 1, Orange::Rhi::ShaderStage::Fragment});
        lay.mBindings.push_back({2, Orange::Rhi::DescriptorType::CombinedImageSampler,
                                 1, Orange::Rhi::ShaderStage::Fragment});
        lay.mBindings.push_back({3, Orange::Rhi::DescriptorType::UniformBuffer,
                                 1, Orange::Rhi::ShaderStage::Fragment});
        lay.mpDebugName = "orange_engine.taa.layout";
        impl.taaLayout = rhi.CreateDescriptorSetLayout(lay);

        Orange::Rhi::BufferDesc ub{};
        ub.mSize        = sizeof(Pipeline::Impl::TaaUboData);
        ub.mUsage       = Orange::Rhi::BufferUsage::Uniform;
        ub.mMemoryUsage = Orange::Rhi::MemoryUsage::CpuToGpu;
        impl.taaUbo = rhi.CreateBuffer(ub);

        if (!impl.taaResolveFs || !impl.taaLayout || !impl.taaUbo)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: TAA 资源创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }

        Orange::Rhi::GraphicsPipelineDesc d{};
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,
                                   impl.fullscreenVs.get(), "main"});
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment,
                                   impl.taaResolveFs.get(), "main"});
        d.mInputAssembly.mTopology        = Orange::Rhi::PrimitiveTopology::TriangleList;
        d.mRasterizer.mCullMode           = Orange::Rhi::CullMode::None;
        d.mDepthStencil.mDepthTestEnable  = false;
        d.mDepthStencil.mDepthWriteEnable = false;
        d.mColorBlend.mAttachments.push_back({});  // no blend
        d.mRenderTargets.mColorFormats.push_back(kHdrColorFormat);
        d.mDescriptorSetLayouts.push_back(impl.taaLayout.get());
        d.mpDebugName = "orange_engine.taa";
        impl.taaResolvePipeline = rhi.CreateGraphicsPipeline(d);
        if (!impl.taaResolvePipeline)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: TAA pipeline 创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }
    }
    {
        // 法线预通道 pipeline —— 把 view-space 法线渲到 normalBuffer（RGBA8），
        // 供 SSAO / SSR 采真实法线。几何 pipeline（带顶点输入 + depth test），
        // 与 shadow caster 同款 push constant 尺寸（128 B，仅 vertex stage）。
        auto npVsCode = LoadSpirv("shaders/orange_engine/normal_prepass.vert.spv");
        auto npFsCode = LoadSpirv("shaders/orange_engine/normal_prepass.frag.spv");
        if (npVsCode.empty() || npFsCode.empty())
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: 法线预通道 shader .spv 加载失败");
            Shutdown();
            return ResultCode::InternalError;
        }
        Orange::Rhi::ShaderModuleDesc sm{};
        sm.mStage      = Orange::Rhi::ShaderStage::Vertex;
        sm.mpCode      = npVsCode.data();
        sm.mCodeSize   = npVsCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_engine.normal_prepass.vert";
        impl.normalPrepassVs = rhi.CreateShaderModule(sm);
        sm.mStage      = Orange::Rhi::ShaderStage::Fragment;
        sm.mpCode      = npFsCode.data();
        sm.mCodeSize   = npFsCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_engine.normal_prepass.frag";
        impl.normalPrepassFs = rhi.CreateShaderModule(sm);
        if (!impl.normalPrepassVs || !impl.normalPrepassFs)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: 法线预通道 shader module 创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }

        Orange::Rhi::GraphicsPipelineDesc d{};
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,
                                   impl.normalPrepassVs.get(), "main"});
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment,
                                   impl.normalPrepassFs.get(), "main"});

        FillVertexInputLayout(d);

        d.mInputAssembly.mTopology        = Orange::Rhi::PrimitiveTopology::TriangleList;
        // cullMode=None 与 shadow caster 同款（背面法线由消费端"强制朝相机"处理）。
        d.mRasterizer.mCullMode           = Orange::Rhi::CullMode::None;
        d.mRasterizer.mFrontFace          = Orange::Rhi::FrontFace::CounterClockwise;
        d.mDepthStencil.mDepthTestEnable  = true;
        d.mDepthStencil.mDepthWriteEnable = true;
        d.mDepthStencil.mDepthCompareOp   = Orange::Rhi::CompareOp::LessOrEqual;
        d.mColorBlend.mAttachments.push_back({});  // no blend
        d.mRenderTargets.mColorFormats.push_back(Orange::Rhi::TextureFormat::RGBA8Unorm);
        d.mRenderTargets.mDepthStencilFormat = Orange::Rhi::TextureFormat::D32Float;

        Orange::Rhi::PushConstantRange pcRange{};
        pcRange.mStage  = Orange::Rhi::ShaderStage::Vertex;
        pcRange.mOffset = 0;
        pcRange.mSize   = 128;  // mat4 uMVP + mat4 uModelView
        d.mPushConstantRanges.push_back(pcRange);

        d.mpDebugName = "orange_engine.normal_prepass";
        impl.normalPrepassPipeline = rhi.CreateGraphicsPipeline(d);
        if (!impl.normalPrepassPipeline)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: 法线预通道 pipeline 创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }
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
    if (!impl.bloomDownsamplePipeline || !impl.bloomUpsamplePipeline ||
        !impl.passthroughCombinePipeline || !impl.tonemapPipeline ||
        !impl.godRaysPipeline ||
        !impl.skyPipeline || !impl.proceduralSkyPipeline)
    {
        ORANGE_LOG_ERROR(
            "Pipeline::Initialize: bloom / combine / tonemap / god_rays / sky pipeline 创建失败");
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
        // GAP-2026-05-26 G1：binding 6 = SpotLightsUbo（同款独立 UBO）。
        lay.mBindings.push_back({6,
                                 Orange::Rhi::DescriptorType::UniformBuffer,
                                 1,
                                 Orange::Rhi::ShaderStage::Fragment});
        // GAP-2026-05-26 G2：binding 7 = spot shadow Tex2DArray（sampler2DArray），
        // binding 8 = SpotShadowUbo（per-caster light view-proj 数组）。
        lay.mBindings.push_back({7,
                                 Orange::Rhi::DescriptorType::CombinedImageSampler,
                                 1,
                                 Orange::Rhi::ShaderStage::Fragment});
        lay.mBindings.push_back({8,
                                 Orange::Rhi::DescriptorType::UniformBuffer,
                                 1,
                                 Orange::Rhi::ShaderStage::Fragment});
        // GAP-2026-05-26 G3：binding 9..9+N-1 = point shadow cubes（每个
        // castsShadow point 一个独立 samplerCube；用独立 cube 而非 cubeArray
        // 免 imageCubeArray feature 依赖）。
        for (std::uint32_t c = 0; c < Pipeline::Impl::kMaxPointShadowCasters; ++c)
        {
            lay.mBindings.push_back({Pipeline::Impl::kPointShadowBinding0 + c,
                                     Orange::Rhi::DescriptorType::CombinedImageSampler,
                                     1,
                                     Orange::Rhi::ShaderStage::Fragment});
        }
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

        // SpotLightsUbo（同款 CpuToGpu）。
        Orange::Rhi::BufferDesc slBufDesc{};
        slBufDesc.mSize        = sizeof(Pipeline::Impl::SpotLightsUboData);
        slBufDesc.mUsage       = Orange::Rhi::BufferUsage::Uniform;
        slBufDesc.mMemoryUsage = Orange::Rhi::MemoryUsage::CpuToGpu;
        impl.spotLightsUbo = rhi.CreateBuffer(slBufDesc);

        // SpotShadowUbo（G2 per-caster light view-proj 数组，同款 CpuToGpu）。
        Orange::Rhi::BufferDesc ssBufDesc{};
        ssBufDesc.mSize        = sizeof(Pipeline::Impl::SpotShadowUboData);
        ssBufDesc.mUsage       = Orange::Rhi::BufferUsage::Uniform;
        ssBufDesc.mMemoryUsage = Orange::Rhi::MemoryUsage::CpuToGpu;
        impl.spotShadowUbo = rhi.CreateBuffer(ssBufDesc);

        // Main desc pool: 1 set，CombinedImageSampler = dir shadow + 3 dummy
        // IBL + spot shadow array + N point shadow cube；UBO = light / point /
        // spot / spot shadow（4）。
        Orange::Rhi::DescriptorPoolDesc pool{};
        pool.mMaxSets = 1;
        pool.mPoolSizes.push_back({Orange::Rhi::DescriptorType::CombinedImageSampler,
                                   5 + Pipeline::Impl::kMaxPointShadowCasters});
        pool.mPoolSizes.push_back({Orange::Rhi::DescriptorType::UniformBuffer, 4});
        pool.mpDebugName = "orange_engine.main.pool";
        impl.mainDescPool = rhi.CreateDescriptorPool(pool);

        if (!impl.mainDescLayout || !impl.lightUbo || !impl.pointLightsUbo
            || !impl.spotLightsUbo || !impl.spotShadowUbo || !impl.mainDescPool)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: main desc layout / pool / lightUbo / pointLightsUbo / spotLightsUbo / spotShadowUbo 创建失败");
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

        Orange::Rhi::DescriptorWrite writeSl{};
        writeSl.mBinding             = 6;
        writeSl.mType                = Orange::Rhi::DescriptorType::UniformBuffer;
        writeSl.mBufferInfo.mpBuffer = impl.spotLightsUbo.get();
        writeSl.mBufferInfo.mOffset  = 0;
        writeSl.mBufferInfo.mRange   = sizeof(Pipeline::Impl::SpotLightsUboData);
        rhi.UpdateDescriptorSet(*impl.mainDescSet, &writeSl, 1);

        Orange::Rhi::DescriptorWrite writeSs{};
        writeSs.mBinding             = 8;
        writeSs.mType                = Orange::Rhi::DescriptorType::UniformBuffer;
        writeSs.mBufferInfo.mpBuffer = impl.spotShadowUbo.get();
        writeSs.mBufferInfo.mOffset  = 0;
        writeSs.mBufferInfo.mRange   = sizeof(Pipeline::Impl::SpotShadowUboData);
        rhi.UpdateDescriptorSet(*impl.mainDescSet, &writeSs, 1);
        // binding 7（spot shadow array sampler）等 EnsureSpotShadowArray 建出
        // array 后再写——与 binding 0（dir shadow）同款延迟绑定。
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

        // 两次独立 cmd submit：
        //   (1) FillDummyIblIrradiance —— 按 impl.dummyIblAmbient 字段（公共
        //       API SetDummyIblAmbient 设置，默认 (0,0,0) engine 中性化）填
        //       irradiance cube 的 6 face × 1×1 half-RGBA 像素
        //   (2) 下面 inline 段 —— 把 prefiltered cube + BRDF LUT 一次性填全 0
        //       （specular 反射保持 0 避免没环境时"灰雾"破坏 PBR 数学，BRDF
        //       LUT 全 0 等同 IBL 总贡献被两项乘子双 0 抹平）；这两条 dummy
        //       永远是 0，不参与公共 API 控制
        //
        // (1) (2) 走两次独立 Begin/End/Submit/WaitIdle，是为了让 (1) helper
        // 在 SetDummyIblAmbient 运行时再调时也走相同 cmd lifecycle 路径，
        // 避免共享 staging buffer 跨函数边界的生命周期问题。
        if (!impl.FillDummyIblIrradiance(/*pBootCmd=*/nullptr))
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: dummy IBL irradiance 填充失败");
            Shutdown();
            return ResultCode::InternalError;
        }

        Orange::Rhi::BufferDesc zeroStagingDesc{};
        zeroStagingDesc.mSize        = 8;
        zeroStagingDesc.mUsage       = Orange::Rhi::BufferUsage::Transfer;
        zeroStagingDesc.mMemoryUsage = Orange::Rhi::MemoryUsage::CpuToGpu;
        auto zeroStaging = rhi.CreateBuffer(zeroStagingDesc);
        if (!zeroStaging)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: dummy IBL prefilter/BRDF staging buffer 创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }
        {
            void* mapped = zeroStaging->Map();
            if (mapped == nullptr)
            {
                ORANGE_LOG_ERROR("Pipeline::Initialize: dummy IBL prefilter/BRDF staging Map 失败");
                Shutdown();
                return ResultCode::InternalError;
            }
            std::memset(mapped, 0, 8);
            zeroStaging->Unmap();
        }

        auto& cmd = *impl.offscreenCmd;
        if (cmd.Begin() != Orange::ResultCode::Success)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: dummy IBL prefilter/BRDF cmd.Begin 失败");
            Shutdown();
            return ResultCode::InternalError;
        }

        // prefiltered cube 6 face × 1×1 全 0
        cmd.TransitionTexture(*impl.dummyPrefilteredCube,
                              Orange::Rhi::TextureLayout::Undefined,
                              Orange::Rhi::TextureLayout::TransferDst);
        for (std::uint32_t layer = 0; layer < 6; ++layer)
        {
            Orange::Rhi::BufferTextureCopyRegion r{};
            r.mBufferOffset = 0;
            r.mMipLevel     = 0;
            r.mArrayLayer   = layer;
            r.mWidth        = 1;
            r.mHeight       = 1;
            r.mDepth        = 1;
            cmd.CopyBufferToTexture(*zeroStaging, *impl.dummyPrefilteredCube, r);
        }
        cmd.TransitionTexture(*impl.dummyPrefilteredCube,
                              Orange::Rhi::TextureLayout::TransferDst,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);

        // 2D BRDF LUT —— 单 layer 单 copy，全 0
        cmd.TransitionTexture(*impl.dummyBrdfLut,
                              Orange::Rhi::TextureLayout::Undefined,
                              Orange::Rhi::TextureLayout::TransferDst);
        {
            Orange::Rhi::BufferTextureCopyRegion r{};
            r.mBufferOffset = 0;
            r.mMipLevel     = 0;
            r.mArrayLayer   = 0;
            r.mWidth        = 1;
            r.mHeight       = 1;
            r.mDepth        = 1;
            cmd.CopyBufferToTexture(*zeroStaging, *impl.dummyBrdfLut, r);
        }
        cmd.TransitionTexture(*impl.dummyBrdfLut,
                              Orange::Rhi::TextureLayout::TransferDst,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);

        if (cmd.End() != Orange::ResultCode::Success)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: dummy IBL prefilter/BRDF cmd.End 失败");
            Shutdown();
            return ResultCode::InternalError;
        }
        if (rhi.SubmitCommandList(cmd) != Orange::ResultCode::Success)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: dummy IBL prefilter/BRDF SubmitCommandList 失败");
            Shutdown();
            return ResultCode::InternalError;
        }
        // 等待 copy 落盘后再让 zeroStaging 出作用域；0.x 阶段 WaitIdle 够用
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

    // 7.8 set 1 · per-instance material 贴图基础设施（GAP-2026-05-25 A2/G1）
    {
        // 1×1 default 贴图：白（baseColor/MR/AO 缺省 → ×scalar = scalar）+
        // flat-normal (128,128,255) → 解码 (0,0,1) → TBN 不扰动法线。两张建出
        // 后，没绑贴图的 PBR 材质渲染与纯 scalar 路径完全一致（零回归）。
        Orange::Rhi::SamplerDesc matSamp{};  // 默认 linear filter + Repeat wrap
        impl.materialSampler = rhi.CreateSampler(matSamp);

        Orange::Rhi::DescriptorSetLayoutDesc lay{};
        for (std::uint32_t b = 0; b < Pipeline::Impl::kMaterialTexBindings; ++b)
        {
            lay.mBindings.push_back({b,
                                     Orange::Rhi::DescriptorType::CombinedImageSampler,
                                     1,
                                     Orange::Rhi::ShaderStage::Fragment});
        }
        lay.mpDebugName = "orange_engine.material.set1.layout";
        impl.materialTexLayout = rhi.CreateDescriptorSetLayout(lay);

        const std::uint32_t maxSets = Pipeline::Impl::kMaxMaterialSets + 1u;  // +1 = default set
        Orange::Rhi::DescriptorPoolDesc pool{};
        pool.mMaxSets = maxSets;
        pool.mPoolSizes.push_back({Orange::Rhi::DescriptorType::CombinedImageSampler,
                                   maxSets * Pipeline::Impl::kMaterialTexBindings});
        pool.mpDebugName = "orange_engine.material.set1.pool";
        impl.materialTexPool = rhi.CreateDescriptorPool(pool);

        auto make1x1 = [&]() -> std::unique_ptr<Orange::Rhi::RHITexture> {
            Orange::Rhi::TextureDesc t{};
            t.mWidth       = 1;
            t.mHeight      = 1;
            t.mFormat      = Orange::Rhi::TextureFormat::RGBA8Unorm;
            t.mDimension   = Orange::Rhi::TextureDimension::Tex2D;
            t.mArrayLayers = 1u;
            t.mUsage       = Orange::Rhi::TextureUsage::Sampled
                           | Orange::Rhi::TextureUsage::TransferDst;
            return rhi.CreateTexture(t);
        };
        const std::uint8_t whitePx[4] = {255u, 255u, 255u, 255u};
        const std::uint8_t flatPx[4]  = {128u, 128u, 255u, 255u};  // tangent-space (0,0,1)
        impl.defaultWhiteTex  = make1x1();
        impl.defaultNormalTex = make1x1();

        if (!impl.materialSampler || !impl.materialTexLayout || !impl.materialTexPool
            || !impl.defaultWhiteTex || !impl.defaultNormalTex)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: set 1 material 资源创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }

        // 上传两张 default 贴图（offscreenCmd staging，与 dummy IBL 同款）。
        Orange::Rhi::BufferDesc sd{};
        sd.mSize        = 8;  // 2 × RGBA8
        sd.mUsage       = Orange::Rhi::BufferUsage::Transfer;
        sd.mMemoryUsage = Orange::Rhi::MemoryUsage::CpuToGpu;
        auto staging = rhi.CreateBuffer(sd);
        if (!staging)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: default 贴图 staging buffer 创建失败");
            Shutdown();
            return ResultCode::InternalError;
        }
        {
            void* mapped = staging->Map();
            if (mapped == nullptr)
            {
                ORANGE_LOG_ERROR("Pipeline::Initialize: default 贴图 staging Map 失败");
                Shutdown();
                return ResultCode::InternalError;
            }
            std::memcpy(mapped, whitePx, 4);
            std::memcpy(static_cast<std::uint8_t*>(mapped) + 4, flatPx, 4);
            staging->Unmap();
        }
        auto& cmd = *impl.offscreenCmd;
        if (cmd.Begin() != Orange::ResultCode::Success)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: default 贴图 cmd.Begin 失败");
            Shutdown();
            return ResultCode::InternalError;
        }
        cmd.TransitionTexture(*impl.defaultWhiteTex,
                              Orange::Rhi::TextureLayout::Undefined,
                              Orange::Rhi::TextureLayout::TransferDst);
        cmd.TransitionTexture(*impl.defaultNormalTex,
                              Orange::Rhi::TextureLayout::Undefined,
                              Orange::Rhi::TextureLayout::TransferDst);
        {
            Orange::Rhi::BufferTextureCopyRegion r{};
            r.mBufferOffset = 0;
            r.mMipLevel = 0; r.mArrayLayer = 0;
            r.mWidth = 1; r.mHeight = 1; r.mDepth = 1;
            cmd.CopyBufferToTexture(*staging, *impl.defaultWhiteTex, r);
            r.mBufferOffset = 4;
            cmd.CopyBufferToTexture(*staging, *impl.defaultNormalTex, r);
        }
        cmd.TransitionTexture(*impl.defaultWhiteTex,
                              Orange::Rhi::TextureLayout::TransferDst,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
        cmd.TransitionTexture(*impl.defaultNormalTex,
                              Orange::Rhi::TextureLayout::TransferDst,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
        if (cmd.End() != Orange::ResultCode::Success
            || rhi.SubmitCommandList(cmd) != Orange::ResultCode::Success)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: default 贴图上传 cmd 提交失败");
            Shutdown();
            return ResultCode::InternalError;
        }
        impl.renderDevice->WaitIdle();

        // defaultMaterialSet：4 槽全喂 default 贴图（null instance / 非覆盖 PBR draw）。
        impl.defaultMaterialSet =
            rhi.AllocateDescriptorSet(*impl.materialTexPool, *impl.materialTexLayout);
        if (impl.defaultMaterialSet)
        {
            Orange::Rhi::RHITexture* defs[Pipeline::Impl::kMaterialTexBindings] = {
                impl.defaultWhiteTex.get(), impl.defaultNormalTex.get(),
                impl.defaultWhiteTex.get(), impl.defaultWhiteTex.get()};
            Orange::Rhi::DescriptorWrite w[Pipeline::Impl::kMaterialTexBindings]{};
            for (std::uint32_t b = 0; b < Pipeline::Impl::kMaterialTexBindings; ++b)
            {
                w[b].mBinding             = b;
                w[b].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
                w[b].mImageInfo.mpTexture = defs[b];
                w[b].mImageInfo.mpSampler = impl.materialSampler.get();
            }
            rhi.UpdateDescriptorSet(*impl.defaultMaterialSet, w,
                                    Pipeline::Impl::kMaterialTexBindings);
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

bool Pipeline::Impl::FillDummyIblIrradiance(Orange::Rhi::RHICommandList* /*pBootCmd*/)
{
    // 当前实现：忽略 pBootCmd（caller 即使传入 cmd 也内部自管 lifecycle，让
    // staging buffer 的生命周期紧贴 Submit + WaitIdle）。pBootCmd 参数保留
    // 作为未来"批量初始化打包到外层 cmd"的预留入口。
    if (renderDevice == nullptr || dummyIrradianceCube == nullptr || offscreenCmd == nullptr)
    {
        return false;
    }
    auto& rhi = renderDevice->GetRhiDevice();

    Orange::Rhi::BufferDesc stagingDesc{};
    stagingDesc.mSize        = 8;
    stagingDesc.mUsage       = Orange::Rhi::BufferUsage::Transfer;
    stagingDesc.mMemoryUsage = Orange::Rhi::MemoryUsage::CpuToGpu;
    auto staging = rhi.CreateBuffer(stagingDesc);
    if (!staging)
    {
        ORANGE_LOG_ERROR("Pipeline::FillDummyIblIrradiance: staging buffer 创建失败");
        return false;
    }
    {
        void* mapped = staging->Map();
        if (mapped == nullptr)
        {
            ORANGE_LOG_ERROR("Pipeline::FillDummyIblIrradiance: staging Map 失败");
            return false;
        }
        // 4 个 half float：RGB 从 dummyIblAmbient 字段算，A 固定 1.0（half
        // 0x3C00）。LE 平台直写 uint16；MSVC + RTX 5070 Ti 都是 LE，需要 BE
        // 平台时再换 byteswap。
        const std::uint16_t halfPx[4] = {
            FloatToHalf(dummyIblAmbient.x),
            FloatToHalf(dummyIblAmbient.y),
            FloatToHalf(dummyIblAmbient.z),
            std::uint16_t{0x3C00},
        };
        std::memcpy(mapped, halfPx, 8);
        staging->Unmap();
    }

    auto& cmd = *offscreenCmd;
    if (cmd.Begin() != Orange::ResultCode::Success)
    {
        ORANGE_LOG_ERROR("Pipeline::FillDummyIblIrradiance: cmd.Begin 失败");
        return false;
    }
    // 首次 Initialize 时 dummyIrradianceCube 处于 Undefined；运行时 SetDummy
    // IblAmbient 二次调用时已是 ShaderReadOnly（Pipeline 主 pass 采过）。两
    // 路径都从当前 layout 经 TransferDst 再回 ShaderReadOnly —— 用
    // 性更宽松的 Undefined（Vulkan spec：Undefined 当作 "内容可丢" 路径，
    // 与从 ShaderReadOnly 实际语义一致，因为下面要全覆盖 copy）。
    cmd.TransitionTexture(*dummyIrradianceCube,
                          Orange::Rhi::TextureLayout::Undefined,
                          Orange::Rhi::TextureLayout::TransferDst);
    for (std::uint32_t layer = 0; layer < 6; ++layer)
    {
        Orange::Rhi::BufferTextureCopyRegion r{};
        r.mBufferOffset = 0;
        r.mMipLevel     = 0;
        r.mArrayLayer   = layer;
        r.mWidth        = 1;
        r.mHeight       = 1;
        r.mDepth        = 1;
        cmd.CopyBufferToTexture(*staging, *dummyIrradianceCube, r);
    }
    cmd.TransitionTexture(*dummyIrradianceCube,
                          Orange::Rhi::TextureLayout::TransferDst,
                          Orange::Rhi::TextureLayout::ShaderReadOnly);

    if (cmd.End() != Orange::ResultCode::Success)
    {
        ORANGE_LOG_ERROR("Pipeline::FillDummyIblIrradiance: cmd.End 失败");
        return false;
    }
    if (rhi.SubmitCommandList(cmd) != Orange::ResultCode::Success)
    {
        ORANGE_LOG_ERROR("Pipeline::FillDummyIblIrradiance: SubmitCommandList 失败");
        return false;
    }
    renderDevice->WaitIdle();
    return true;
}

}  // namespace Orange::Engine::Render
