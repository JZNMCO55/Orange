// Pipeline::SetupRhiResources —— 共享 RHI 资源创建路径（sampler /
// passthrough / bloom / tonemap / godrays / sky / grid / main pass UBO /
// shadow caster pipeline / dummy IBL / offscreen cmd list / DebugDrawScene
// backend）。Initialize 与 InitializeOffscreen 两条入口都调用本函数；
// 调用前必须保证 `mpImpl->renderDevice` + `mpImpl->upload` + `mpImpl->assets`
// 已就位。失败路径内部已调 Shutdown() 整体回滚。

#include "orange/engine/render/Pipeline.h"

#include "PipelineImpl.h"

#include "orange/engine/render/BuiltinShadowShaders.h"

#include <cstring>

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
        // 处理几何遮挡，比 gl_FragDepth 路径稳。push 128B (mat4 invVP + mat4 VP)。
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

}  // namespace Orange::Engine::Render
