// ---------------------------------------------------------------------------
// IblBaker 实装：第一个 entry point `BakeEquirectToCube`。
//
// 工作流：
//   1. `EnsureEquirectPipeline` —— 首次调用懒加载 SPIR-V + 创建
//      DescriptorSetLayout（binding 0 = CombinedImageSampler / binding 1
//      = StorageImage）+ ComputePipeline + 1 个共享 linear sampler。
//   2. 每次 `BakeEquirectToCube`：
//        - 校验参数（cubeFaceSize ≥ 8 && 8 倍数）
//        - 创建 cube map texture（Sampled | Storage | TransferSrc，
//          TexCube + arrayLayers = 6，RGBA16Float）
//        - per-call descriptor pool + 单 set 写入 (equirect sampler /
//          cube storage)
//        - 一次性 command list：Transition(cube: Common → UnorderedAccess)
//          → Dispatch(faceSize/8, faceSize/8, 6) → Transition(cube:
//          UnorderedAccess → ShaderResource)
//        - SubmitCommandList + WaitIdle（启动期烘焙，同步等待无影响）
//        - 返回 cube map 所有权
//
// SPIR-V 加载与 BuiltinShadowShaders 同节奏：走 `AssetRegistry::Load<
// ShaderAsset>(path)` dedup + 拿 SPIR-V word 流再 `RHIDevice::
// CreateShaderModule`。SPV 文件由 CMake `orange_engine_compile_builtin_shader`
// 编译到 `${RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/shaders/orange_engine/`。
//
// 后续 commit 续 `BakeBrdfLut` / `BakeIrradiance` / `BakePrefilteredEnvironment`
// 时复用本文件的 `Impl` 结构 + helper（GetExecutableDir / SPV 加载等）。
// ---------------------------------------------------------------------------

#include "orange/engine/render/IblBaker.h"

#include "orange/engine/asset/AssetRegistry.h"
#include "orange/engine/asset/ShaderAsset.h"
#include "orange/engine/core/Log.h"

#include "orange/rhi/RHICommandList.h"
#include "orange/rhi/RHIDescriptor.h"
#include "orange/rhi/RHIDevice.h"
#include "orange/rhi/RHIPipeline.h"
#include "orange/rhi/RHIRendering.h"
#include "orange/rhi/RHISampler.h"
#include "orange/rhi/RHIShaderModule.h"
#include "orange/rhi/RHITexture.h"
#include "orange/rhi/RHITypes.h"

#include <algorithm>
#include <vector>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace Orange::Engine::Render
{
    namespace
    {

        // 与 BuiltinMaterials / BuiltinShadowShaders 同款的 .exe-相对 SPV 路径
        // 解析。三处复用——等到 4 处再提到 Platform 模块统一（沿用现有"3+ 处
        // 才抽 helper"惯例）。
        std::filesystem::path GetExecutableDir()
        {
#if defined(_WIN32)
            wchar_t     buffer[MAX_PATH];
            const DWORD len = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
            if (len == 0 || len == MAX_PATH)
            {
                return std::filesystem::current_path();
            }
            return std::filesystem::path(std::wstring(buffer, len)).parent_path();
#else
            return std::filesystem::current_path();
#endif
        }

        std::string ResolveBuiltinShaderPath(const char* relative)
        {
            return (GetExecutableDir() / relative).string();
        }

    } // namespace

    // ---------------------------------------------------------------------------
    // Impl —— 持有 device / registry 引用 + lazy-init 的 compute pipeline 资源。
    // 各烘焙路径按前缀分组：`equirect` / `brdfLut` / 后续 `irradiance` /
    // `prefilter`。每路径自带 shader + descriptor layout + pipeline，sampler
    // 按需复用（equirect 路径用，brdfLut 无 input texture 不需要）。
    // ---------------------------------------------------------------------------
    struct IblBaker::Impl
    {
        Orange::Rhi::RHIDevice&               device;
        Orange::Engine::Asset::AssetRegistry& registry;

        // equirect→cube 路径
        std::unique_ptr<Orange::Rhi::RHIShaderModule>        equirectShader;
        std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> equirectLayout;
        std::unique_ptr<Orange::Rhi::RHIPipeline>            equirectPipeline;
        std::unique_ptr<Orange::Rhi::RHISampler>             equirectSampler;

        // BRDF LUT 路径（无 input texture）
        std::unique_ptr<Orange::Rhi::RHIShaderModule>        brdfLutShader;
        std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> brdfLutLayout;
        std::unique_ptr<Orange::Rhi::RHIPipeline>            brdfLutPipeline;

        // irradiance cube 路径（input: envCube + sampler；output: irradiance cube）
        std::unique_ptr<Orange::Rhi::RHIShaderModule>        irradianceShader;
        std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> irradianceLayout;
        std::unique_ptr<Orange::Rhi::RHIPipeline>            irradiancePipeline;
        std::unique_ptr<Orange::Rhi::RHISampler>             irradianceSampler;

        // prefilter cube 路径（graphics fullscreen pass；input: envCube +
        // sampler；output: cube 每 (face, mip) 作 ColorAttachment）
        std::unique_ptr<Orange::Rhi::RHIShaderModule>        prefilterVs;
        std::unique_ptr<Orange::Rhi::RHIShaderModule>        prefilterFs;
        std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> prefilterLayout;
        std::unique_ptr<Orange::Rhi::RHIPipeline>            prefilterPipeline;
        std::unique_ptr<Orange::Rhi::RHISampler>             prefilterSampler;

        Impl(Orange::Rhi::RHIDevice& d, Orange::Engine::Asset::AssetRegistry& r)
            : device(d), registry(r)
        {
        }

        // 共享 helper：把 "shaders/orange_engine/<file>" 加载为 RHIShaderModule。
        // 走 AssetRegistry::Load<ShaderAsset> 做 dedup + Get 拿 SPIR-V bytes。
        // `stage` 由调用方显式指定（Compute / Vertex / Fragment）；与
        // ShaderLoader 按文件名后缀推断的 ShaderAsset::Stage() 必须一致，调用
        // 方传错会让后端 pipeline 创建失败。`debugName` 透传给 RHI 后端（GPU
        // capture 工具用）。失败返回 nullptr，调用方按 unique_ptr 真伪判定。
        std::unique_ptr<Orange::Rhi::RHIShaderModule>
        LoadShader(const char*              spvRelative,
                   const char*              debugName,
                   Orange::Rhi::ShaderStage stage)
        {
            const auto spvPath   = ResolveBuiltinShaderPath(spvRelative);
            auto       spvResult = registry.Load<Orange::Engine::Asset::ShaderAsset>(spvPath);
            if (spvResult.IsErr())
            {
                ORANGE_LOG_ERROR("IblBaker: 加载 {} 失败 (path={}, code={})",
                                 spvRelative, spvPath,
                                 static_cast<unsigned>(spvResult.Error()));
                return nullptr;
            }
            const auto* pShaderAsset = registry.Get(spvResult.Value());
            if (pShaderAsset == nullptr || pShaderAsset->Empty())
            {
                ORANGE_LOG_ERROR("IblBaker: ShaderAsset 无效或为空 (path={})", spvPath);
                return nullptr;
            }
            Orange::Rhi::ShaderModuleDesc modDesc{};
            modDesc.mpCode      = pShaderAsset->SpirV().data();
            modDesc.mCodeSize   = pShaderAsset->ByteSize();
            modDesc.mStage      = stage;
            modDesc.mpDebugName = debugName;
            auto module         = device.CreateShaderModule(modDesc);
            if (!module)
            {
                ORANGE_LOG_ERROR("IblBaker: CreateShaderModule 失败 (path={})", spvPath);
                return nullptr;
            }
            return module;
        }

        // 首次进入 BakeEquirectToCube 时调用；幂等。失败返回 false。
        bool EnsureEquirectPipeline()
        {
            if (equirectPipeline)
            {
                return true;
            }

            equirectShader = LoadShader(
                "shaders/orange_engine/ibl_equirect_to_cube.comp.spv",
                "IblBaker/EquirectToCube",
                Orange::Rhi::ShaderStage::Compute);
            if (!equirectShader)
            {
                return false;
            }

            // ---- 2. Descriptor set layout：binding 0 sampler、binding 1 storage cube ----
            Orange::Rhi::DescriptorSetLayoutDesc layoutDesc{};
            layoutDesc.mBindings = {
                {0u, Orange::Rhi::DescriptorType::CombinedImageSampler, 1u,
                 Orange::Rhi::ShaderStage::Compute},
                {1u, Orange::Rhi::DescriptorType::StorageImage, 1u,
                 Orange::Rhi::ShaderStage::Compute},
            };
            layoutDesc.mpDebugName = "IblBaker/EquirectToCube/Layout";
            equirectLayout         = device.CreateDescriptorSetLayout(layoutDesc);
            if (!equirectLayout)
            {
                ORANGE_LOG_ERROR("IblBaker: CreateDescriptorSetLayout(equirect→cube) 失败");
                return false;
            }

            // ---- 3. Compute pipeline + push constant range (uFaceSize: uint32) ----
            Orange::Rhi::ComputePipelineDesc pipeDesc{};
            pipeDesc.mComputeShader.mStage      = Orange::Rhi::ShaderStage::Compute;
            pipeDesc.mComputeShader.mpModule    = equirectShader.get();
            pipeDesc.mComputeShader.mEntryPoint = "main";
            pipeDesc.mDescriptorSetLayouts.push_back(equirectLayout.get());
            pipeDesc.mPushConstantRanges.push_back(
                {Orange::Rhi::ShaderStage::Compute, 0u, sizeof(std::uint32_t)});
            pipeDesc.mpDebugName = "IblBaker/EquirectToCube/Pipeline";
            equirectPipeline     = device.CreateComputePipeline(pipeDesc);
            if (!equirectPipeline)
            {
                ORANGE_LOG_ERROR("IblBaker: CreateComputePipeline(equirect→cube) 失败");
                return false;
            }

            // ---- 4. 共享 linear sampler：equirect 2D 是低频环境光，linear filter
            //          + clamp-to-edge 避免接缝 wrap 引入瑕疵。----
            Orange::Rhi::SamplerDesc sampDesc{};
            sampDesc.mMinFilter  = Orange::Rhi::SamplerFilter::Linear;
            sampDesc.mMagFilter  = Orange::Rhi::SamplerFilter::Linear;
            sampDesc.mMipmapMode = Orange::Rhi::SamplerMipmapMode::Nearest;
            sampDesc.mAddressU   = Orange::Rhi::SamplerAddressMode::Repeat;      // 经度 wrap
            sampDesc.mAddressV   = Orange::Rhi::SamplerAddressMode::ClampToEdge; // 纬度 clamp
            sampDesc.mAddressW   = Orange::Rhi::SamplerAddressMode::ClampToEdge;
            sampDesc.mpDebugName = "IblBaker/EquirectSampler";
            equirectSampler      = device.CreateSampler(sampDesc);
            if (!equirectSampler)
            {
                ORANGE_LOG_ERROR("IblBaker: CreateSampler(equirect) 失败");
                return false;
            }

            return true;
        }

        // 首次进入 BakeIrradiance 时调用；幂等。失败返回 false。
        // descriptor layout 与 c1 equirect→cube 同款（CombinedImageSampler +
        // StorageImage），但 sampler 全 ClampToEdge（cube sampling 不需要经度
        // wrap）。
        bool EnsureIrradiancePipeline()
        {
            if (irradiancePipeline)
            {
                return true;
            }

            irradianceShader = LoadShader(
                "shaders/orange_engine/ibl_irradiance.comp.spv",
                "IblBaker/Irradiance",
                Orange::Rhi::ShaderStage::Compute);
            if (!irradianceShader)
            {
                return false;
            }

            Orange::Rhi::DescriptorSetLayoutDesc layoutDesc{};
            layoutDesc.mBindings = {
                {0u, Orange::Rhi::DescriptorType::CombinedImageSampler, 1u,
                 Orange::Rhi::ShaderStage::Compute},
                {1u, Orange::Rhi::DescriptorType::StorageImage, 1u,
                 Orange::Rhi::ShaderStage::Compute},
            };
            layoutDesc.mpDebugName = "IblBaker/Irradiance/Layout";
            irradianceLayout       = device.CreateDescriptorSetLayout(layoutDesc);
            if (!irradianceLayout)
            {
                ORANGE_LOG_ERROR("IblBaker: CreateDescriptorSetLayout(irradiance) 失败");
                return false;
            }

            Orange::Rhi::ComputePipelineDesc pipeDesc{};
            pipeDesc.mComputeShader.mStage      = Orange::Rhi::ShaderStage::Compute;
            pipeDesc.mComputeShader.mpModule    = irradianceShader.get();
            pipeDesc.mComputeShader.mEntryPoint = "main";
            pipeDesc.mDescriptorSetLayouts.push_back(irradianceLayout.get());
            pipeDesc.mPushConstantRanges.push_back(
                {Orange::Rhi::ShaderStage::Compute, 0u, 2u * sizeof(std::uint32_t)});
            pipeDesc.mpDebugName = "IblBaker/Irradiance/Pipeline";
            irradiancePipeline   = device.CreateComputePipeline(pipeDesc);
            if (!irradiancePipeline)
            {
                ORANGE_LOG_ERROR("IblBaker: CreateComputePipeline(irradiance) 失败");
                return false;
            }

            // cube 采样：全 ClampToEdge（cube map 硬件自动处理 face 接缝，
            // 但 sampler ClampToEdge 仍是工业惯例，避免 wrap 引入跨 face 漂移）。
            Orange::Rhi::SamplerDesc sampDesc{};
            sampDesc.mMinFilter  = Orange::Rhi::SamplerFilter::Linear;
            sampDesc.mMagFilter  = Orange::Rhi::SamplerFilter::Linear;
            sampDesc.mMipmapMode = Orange::Rhi::SamplerMipmapMode::Linear;
            sampDesc.mAddressU   = Orange::Rhi::SamplerAddressMode::ClampToEdge;
            sampDesc.mAddressV   = Orange::Rhi::SamplerAddressMode::ClampToEdge;
            sampDesc.mAddressW   = Orange::Rhi::SamplerAddressMode::ClampToEdge;
            sampDesc.mpDebugName = "IblBaker/CubeSampler";
            irradianceSampler    = device.CreateSampler(sampDesc);
            if (!irradianceSampler)
            {
                ORANGE_LOG_ERROR("IblBaker: CreateSampler(irradiance) 失败");
                return false;
            }

            return true;
        }

        // 首次进入 BakePrefilteredEnvironment 时调用；幂等。失败返回 false。
        // **graphics pipeline 路径**（与其他三个 compute pipeline 不同）：
        // fullscreen vert + ibl_prefilter.frag.glsl，per-(face, mip) BeginRendering
        // 写入对应 ColorAttachment 子资源 view。理由见 ibl_prefilter.frag.glsl
        // 顶部注释 + BakePrefilteredEnvironment 实装段说明。
        bool EnsurePrefilterPipeline()
        {
            if (prefilterPipeline)
            {
                return true;
            }

            prefilterVs = LoadShader(
                "shaders/orange_engine/fullscreen.vert.spv",
                "IblBaker/Prefilter/VS",
                Orange::Rhi::ShaderStage::Vertex);
            prefilterFs = LoadShader(
                "shaders/orange_engine/ibl_prefilter.frag.spv",
                "IblBaker/Prefilter/FS",
                Orange::Rhi::ShaderStage::Fragment);
            if (!prefilterVs || !prefilterFs)
            {
                return false;
            }

            Orange::Rhi::DescriptorSetLayoutDesc layoutDesc{};
            layoutDesc.mBindings = {
                {0u, Orange::Rhi::DescriptorType::CombinedImageSampler, 1u,
                 Orange::Rhi::ShaderStage::Fragment},
            };
            layoutDesc.mpDebugName = "IblBaker/Prefilter/Layout";
            prefilterLayout        = device.CreateDescriptorSetLayout(layoutDesc);
            if (!prefilterLayout)
            {
                ORANGE_LOG_ERROR("IblBaker: CreateDescriptorSetLayout(prefilter) 失败");
                return false;
            }

            // Push constant block：与 GLSL `PushConstants { uFaceIdx; uRoughness;
            // uSampleCount; uPad; }` 字段顺序一致；16 B 对齐占位预留扩展。
            Orange::Rhi::GraphicsPipelineDesc pipeDesc{};
            pipeDesc.mShaderStages.push_back(
                {Orange::Rhi::ShaderStage::Vertex, prefilterVs.get(), "main"});
            pipeDesc.mShaderStages.push_back(
                {Orange::Rhi::ShaderStage::Fragment, prefilterFs.get(), "main"});
            pipeDesc.mInputAssembly.mTopology        = Orange::Rhi::PrimitiveTopology::TriangleList;
            pipeDesc.mRasterizer.mCullMode           = Orange::Rhi::CullMode::None;
            pipeDesc.mDepthStencil.mDepthTestEnable  = false;
            pipeDesc.mDepthStencil.mDepthWriteEnable = false;
            pipeDesc.mColorBlend.mAttachments.push_back({});
            pipeDesc.mRenderTargets.mColorFormats.push_back(
                Orange::Rhi::TextureFormat::RGBA16Float);
            pipeDesc.mDescriptorSetLayouts.push_back(prefilterLayout.get());
            pipeDesc.mPushConstantRanges.push_back(
                {Orange::Rhi::ShaderStage::Fragment, 0u, 4u * sizeof(std::uint32_t)});
            pipeDesc.mpDebugName = "IblBaker/Prefilter/Pipeline";
            prefilterPipeline    = device.CreateGraphicsPipeline(pipeDesc);
            if (!prefilterPipeline)
            {
                ORANGE_LOG_ERROR("IblBaker: CreateGraphicsPipeline(prefilter) 失败");
                return false;
            }

            Orange::Rhi::SamplerDesc sampDesc{};
            sampDesc.mMinFilter  = Orange::Rhi::SamplerFilter::Linear;
            sampDesc.mMagFilter  = Orange::Rhi::SamplerFilter::Linear;
            sampDesc.mMipmapMode = Orange::Rhi::SamplerMipmapMode::Linear;
            sampDesc.mAddressU   = Orange::Rhi::SamplerAddressMode::ClampToEdge;
            sampDesc.mAddressV   = Orange::Rhi::SamplerAddressMode::ClampToEdge;
            sampDesc.mAddressW   = Orange::Rhi::SamplerAddressMode::ClampToEdge;
            sampDesc.mpDebugName = "IblBaker/PrefilterCubeSampler";
            prefilterSampler     = device.CreateSampler(sampDesc);
            if (!prefilterSampler)
            {
                ORANGE_LOG_ERROR("IblBaker: CreateSampler(prefilter) 失败");
                return false;
            }

            return true;
        }

        // 首次进入 BakeBrdfLut 时调用；幂等。失败返回 false。
        // BRDF LUT 与 environment 无关（仅依赖 NoV / roughness），没有 input
        // texture，所以 descriptor layout 只有 1 个 binding = StorageImage。
        bool EnsureBrdfLutPipeline()
        {
            if (brdfLutPipeline)
            {
                return true;
            }

            brdfLutShader = LoadShader(
                "shaders/orange_engine/ibl_brdf_lut.comp.spv",
                "IblBaker/BrdfLut",
                Orange::Rhi::ShaderStage::Compute);
            if (!brdfLutShader)
            {
                return false;
            }

            Orange::Rhi::DescriptorSetLayoutDesc layoutDesc{};
            layoutDesc.mBindings = {
                {0u, Orange::Rhi::DescriptorType::StorageImage, 1u,
                 Orange::Rhi::ShaderStage::Compute},
            };
            layoutDesc.mpDebugName = "IblBaker/BrdfLut/Layout";
            brdfLutLayout          = device.CreateDescriptorSetLayout(layoutDesc);
            if (!brdfLutLayout)
            {
                ORANGE_LOG_ERROR("IblBaker: CreateDescriptorSetLayout(brdfLut) 失败");
                return false;
            }

            // push constant：uLutSize + uSampleCount = 2 × uint32 = 8 B。
            Orange::Rhi::ComputePipelineDesc pipeDesc{};
            pipeDesc.mComputeShader.mStage      = Orange::Rhi::ShaderStage::Compute;
            pipeDesc.mComputeShader.mpModule    = brdfLutShader.get();
            pipeDesc.mComputeShader.mEntryPoint = "main";
            pipeDesc.mDescriptorSetLayouts.push_back(brdfLutLayout.get());
            pipeDesc.mPushConstantRanges.push_back(
                {Orange::Rhi::ShaderStage::Compute, 0u, 2u * sizeof(std::uint32_t)});
            pipeDesc.mpDebugName = "IblBaker/BrdfLut/Pipeline";
            brdfLutPipeline      = device.CreateComputePipeline(pipeDesc);
            if (!brdfLutPipeline)
            {
                ORANGE_LOG_ERROR("IblBaker: CreateComputePipeline(brdfLut) 失败");
                return false;
            }

            return true;
        }
    };

    IblBaker::IblBaker(Orange::Rhi::RHIDevice& device, Asset::AssetRegistry& registry)
        : mpImpl(std::make_unique<Impl>(device, registry))
    {
    }

    IblBaker::~IblBaker() = default;

    IblBaker::IblBaker(IblBaker&&) noexcept            = default;
    IblBaker& IblBaker::operator=(IblBaker&&) noexcept = default;

    std::unique_ptr<Orange::Rhi::RHITexture>
    IblBaker::BakeEquirectToCube(Orange::Rhi::RHITexture& equirectHdr,
                                 std::uint32_t            cubeFaceSize)
    {
        // ---- 参数校验 ----
        if (cubeFaceSize < 8u || (cubeFaceSize % 8u) != 0u)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeEquirectToCube: cubeFaceSize 必须 ≥ 8 且为 8 的倍数 (got={})",
                             cubeFaceSize);
            return nullptr;
        }

        if (!mpImpl->EnsureEquirectPipeline())
        {
            return nullptr;
        }
        auto& device = mpImpl->device;

        // ---- 创建 cube map ----
        Orange::Rhi::TextureDesc cubeDesc{};
        cubeDesc.mWidth       = cubeFaceSize;
        cubeDesc.mHeight      = cubeFaceSize;
        cubeDesc.mFormat      = Orange::Rhi::TextureFormat::RGBA16Float;
        cubeDesc.mDimension   = Orange::Rhi::TextureDimension::TexCube;
        cubeDesc.mArrayLayers = 6u;
        cubeDesc.mUsage       = Orange::Rhi::TextureUsage::Sampled | Orange::Rhi::TextureUsage::Storage | Orange::Rhi::TextureUsage::TransferSrc;
        auto cubeTex          = device.CreateTexture(cubeDesc);
        if (!cubeTex)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeEquirectToCube: CreateTexture(cube {}^2 ×6) 失败",
                             cubeFaceSize);
            return nullptr;
        }

        // ---- per-call descriptor pool + set ----
        Orange::Rhi::DescriptorPoolDesc poolDesc{};
        poolDesc.mMaxSets   = 1u;
        poolDesc.mPoolSizes = {
            {Orange::Rhi::DescriptorType::CombinedImageSampler, 1u},
            {Orange::Rhi::DescriptorType::StorageImage, 1u},
        };
        poolDesc.mpDebugName = "IblBaker/EquirectToCube/Pool";
        auto descPool        = device.CreateDescriptorPool(poolDesc);
        if (!descPool)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeEquirectToCube: CreateDescriptorPool 失败");
            return nullptr;
        }

        auto descSet = device.AllocateDescriptorSet(*descPool, *mpImpl->equirectLayout);
        if (!descSet)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeEquirectToCube: AllocateDescriptorSet 失败");
            return nullptr;
        }

        Orange::Rhi::DescriptorWrite writes[2] = {};
        writes[0].mBinding                     = 0u;
        writes[0].mType                        = Orange::Rhi::DescriptorType::CombinedImageSampler;
        writes[0].mImageInfo.mpTexture         = &equirectHdr;
        writes[0].mImageInfo.mpSampler         = mpImpl->equirectSampler.get();

        writes[1].mBinding             = 1u;
        writes[1].mType                = Orange::Rhi::DescriptorType::StorageImage;
        writes[1].mImageInfo.mpTexture = cubeTex.get();
        writes[1].mImageInfo.mpSampler = nullptr;

        device.UpdateDescriptorSet(*descSet, writes, 2u);

        // ---- 一次性 command list：Transition + Dispatch + Transition ----
        auto cmd = device.CreateCommandList(Orange::Rhi::CommandQueueType::Graphics);
        if (!cmd)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeEquirectToCube: CreateCommandList 失败");
            return nullptr;
        }
        if (cmd->Begin() != Orange::ResultCode::Success)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeEquirectToCube: cmd.Begin 失败");
            return nullptr;
        }

        // cube：Common → UnorderedAccess（VK_IMAGE_LAYOUT_GENERAL，storage write 必备）
        cmd->TransitionResource(*cubeTex,
                                Orange::Rhi::ResourceState::Common,
                                Orange::Rhi::ResourceState::UnorderedAccess);

        cmd->BindComputePipeline(*mpImpl->equirectPipeline);
        cmd->SetDescriptorSet(0u, *descSet);

        const std::uint32_t faceSizePc = cubeFaceSize;
        cmd->SetPushConstants(Orange::Rhi::ShaderStage::Compute,
                              0u, sizeof(std::uint32_t), &faceSizePc);

        // workgroup local_size_x/y = 8，z = 1；dispatch (faceSize/8, faceSize/8, 6)
        const std::uint32_t groupX = cubeFaceSize / 8u;
        const std::uint32_t groupY = cubeFaceSize / 8u;
        cmd->Dispatch(groupX, groupY, 6u);

        // cube：UnorderedAccess → ShaderResource，返回给下游 sampling 用
        cmd->TransitionResource(*cubeTex,
                                Orange::Rhi::ResourceState::UnorderedAccess,
                                Orange::Rhi::ResourceState::ShaderResource);

        if (cmd->End() != Orange::ResultCode::Success)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeEquirectToCube: cmd.End 失败");
            return nullptr;
        }
        if (device.SubmitCommandList(*cmd) != Orange::ResultCode::Success)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeEquirectToCube: SubmitCommandList 失败");
            return nullptr;
        }
        // 启动期烘焙——等 GPU 落盘后让 descSet / cmd 安全析构。
        device.WaitIdle();

        return cubeTex;
    }

    std::unique_ptr<Orange::Rhi::RHITexture>
    IblBaker::BakeBrdfLut(std::uint32_t lutSize, std::uint32_t sampleCount)
    {
        // ---- 参数校验 ----
        if (lutSize < 8u || (lutSize % 8u) != 0u)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeBrdfLut: lutSize 必须 ≥ 8 且为 8 的倍数 (got={})",
                             lutSize);
            return nullptr;
        }
        if (sampleCount == 0u)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeBrdfLut: sampleCount 必须 > 0");
            return nullptr;
        }

        if (!mpImpl->EnsureBrdfLutPipeline())
        {
            return nullptr;
        }
        auto& device = mpImpl->device;

        // ---- 创建 RG16F 2D LUT ----
        // RG16Float 在 OrangeRender FormatCoverageTest 验过 Sampled / ColorAttachment
        // / TransferDst 三项；StorageImage 能力当前没显式验证，desktop GPU 事实
        // 全支持。撞上不支持的驱动应 fallback 到 graphics fullscreen 路径（写到
        // ColorAttachment）—— c2 阶段先按主路径走，撞到再 engine-known-gaps 登记。
        Orange::Rhi::TextureDesc lutDesc{};
        lutDesc.mWidth       = lutSize;
        lutDesc.mHeight      = lutSize;
        lutDesc.mFormat      = Orange::Rhi::TextureFormat::RG16Float;
        lutDesc.mDimension   = Orange::Rhi::TextureDimension::Tex2D;
        lutDesc.mArrayLayers = 1u;
        lutDesc.mUsage       = Orange::Rhi::TextureUsage::Sampled | Orange::Rhi::TextureUsage::Storage | Orange::Rhi::TextureUsage::TransferSrc;
        auto lutTex          = device.CreateTexture(lutDesc);
        if (!lutTex)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeBrdfLut: CreateTexture({}^2 RG16F) 失败", lutSize);
            return nullptr;
        }

        // ---- per-call descriptor pool + set ----
        Orange::Rhi::DescriptorPoolDesc poolDesc{};
        poolDesc.mMaxSets   = 1u;
        poolDesc.mPoolSizes = {
            {Orange::Rhi::DescriptorType::StorageImage, 1u},
        };
        poolDesc.mpDebugName = "IblBaker/BrdfLut/Pool";
        auto descPool        = device.CreateDescriptorPool(poolDesc);
        if (!descPool)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeBrdfLut: CreateDescriptorPool 失败");
            return nullptr;
        }

        auto descSet = device.AllocateDescriptorSet(*descPool, *mpImpl->brdfLutLayout);
        if (!descSet)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeBrdfLut: AllocateDescriptorSet 失败");
            return nullptr;
        }

        Orange::Rhi::DescriptorWrite write{};
        write.mBinding             = 0u;
        write.mType                = Orange::Rhi::DescriptorType::StorageImage;
        write.mImageInfo.mpTexture = lutTex.get();
        device.UpdateDescriptorSet(*descSet, &write, 1u);

        // ---- 一次性 command list：Transition + Dispatch + Transition ----
        auto cmd = device.CreateCommandList(Orange::Rhi::CommandQueueType::Graphics);
        if (!cmd)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeBrdfLut: CreateCommandList 失败");
            return nullptr;
        }
        if (cmd->Begin() != Orange::ResultCode::Success)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeBrdfLut: cmd.Begin 失败");
            return nullptr;
        }

        cmd->TransitionResource(*lutTex,
                                Orange::Rhi::ResourceState::Common,
                                Orange::Rhi::ResourceState::UnorderedAccess);

        cmd->BindComputePipeline(*mpImpl->brdfLutPipeline);
        cmd->SetDescriptorSet(0u, *descSet);

        // push constant block layout 与 GLSL 端 PushConstants { uLutSize; uSampleCount; } 一致
        const std::uint32_t pcData[2] = {lutSize, sampleCount};
        cmd->SetPushConstants(Orange::Rhi::ShaderStage::Compute,
                              0u, sizeof(pcData), pcData);

        const std::uint32_t groupX = lutSize / 8u;
        const std::uint32_t groupY = lutSize / 8u;
        cmd->Dispatch(groupX, groupY, 1u);

        cmd->TransitionResource(*lutTex,
                                Orange::Rhi::ResourceState::UnorderedAccess,
                                Orange::Rhi::ResourceState::ShaderResource);

        if (cmd->End() != Orange::ResultCode::Success)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeBrdfLut: cmd.End 失败");
            return nullptr;
        }
        if (device.SubmitCommandList(*cmd) != Orange::ResultCode::Success)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeBrdfLut: SubmitCommandList 失败");
            return nullptr;
        }
        device.WaitIdle();

        return lutTex;
    }

    std::unique_ptr<Orange::Rhi::RHITexture>
    IblBaker::BakeIrradiance(Orange::Rhi::RHITexture& envCube,
                             std::uint32_t            cubeFaceSize,
                             std::uint32_t            sampleCount)
    {
        if (cubeFaceSize < 8u || (cubeFaceSize % 8u) != 0u)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeIrradiance: cubeFaceSize 必须 ≥ 8 且为 8 的倍数 (got={})",
                             cubeFaceSize);
            return nullptr;
        }
        if (sampleCount == 0u)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeIrradiance: sampleCount 必须 > 0");
            return nullptr;
        }

        if (!mpImpl->EnsureIrradiancePipeline())
        {
            return nullptr;
        }
        auto& device = mpImpl->device;

        Orange::Rhi::TextureDesc cubeDesc{};
        cubeDesc.mWidth       = cubeFaceSize;
        cubeDesc.mHeight      = cubeFaceSize;
        cubeDesc.mFormat      = Orange::Rhi::TextureFormat::RGBA16Float;
        cubeDesc.mDimension   = Orange::Rhi::TextureDimension::TexCube;
        cubeDesc.mArrayLayers = 6u;
        cubeDesc.mUsage       = Orange::Rhi::TextureUsage::Sampled | Orange::Rhi::TextureUsage::Storage | Orange::Rhi::TextureUsage::TransferSrc;
        auto irrTex           = device.CreateTexture(cubeDesc);
        if (!irrTex)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeIrradiance: CreateTexture({}^2 cube ×6) 失败",
                             cubeFaceSize);
            return nullptr;
        }

        Orange::Rhi::DescriptorPoolDesc poolDesc{};
        poolDesc.mMaxSets   = 1u;
        poolDesc.mPoolSizes = {
            {Orange::Rhi::DescriptorType::CombinedImageSampler, 1u},
            {Orange::Rhi::DescriptorType::StorageImage, 1u},
        };
        poolDesc.mpDebugName = "IblBaker/Irradiance/Pool";
        auto descPool        = device.CreateDescriptorPool(poolDesc);
        if (!descPool)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeIrradiance: CreateDescriptorPool 失败");
            return nullptr;
        }

        auto descSet = device.AllocateDescriptorSet(*descPool, *mpImpl->irradianceLayout);
        if (!descSet)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeIrradiance: AllocateDescriptorSet 失败");
            return nullptr;
        }

        Orange::Rhi::DescriptorWrite writes[2] = {};
        writes[0].mBinding                     = 0u;
        writes[0].mType                        = Orange::Rhi::DescriptorType::CombinedImageSampler;
        writes[0].mImageInfo.mpTexture         = &envCube;
        writes[0].mImageInfo.mpSampler         = mpImpl->irradianceSampler.get();
        writes[1].mBinding                     = 1u;
        writes[1].mType                        = Orange::Rhi::DescriptorType::StorageImage;
        writes[1].mImageInfo.mpTexture         = irrTex.get();
        device.UpdateDescriptorSet(*descSet, writes, 2u);

        auto cmd = device.CreateCommandList(Orange::Rhi::CommandQueueType::Graphics);
        if (!cmd)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeIrradiance: CreateCommandList 失败");
            return nullptr;
        }
        if (cmd->Begin() != Orange::ResultCode::Success)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeIrradiance: cmd.Begin 失败");
            return nullptr;
        }

        cmd->TransitionResource(*irrTex,
                                Orange::Rhi::ResourceState::Common,
                                Orange::Rhi::ResourceState::UnorderedAccess);

        cmd->BindComputePipeline(*mpImpl->irradiancePipeline);
        cmd->SetDescriptorSet(0u, *descSet);

        const std::uint32_t pcData[2] = {cubeFaceSize, sampleCount};
        cmd->SetPushConstants(Orange::Rhi::ShaderStage::Compute,
                              0u, sizeof(pcData), pcData);

        const std::uint32_t groupX = cubeFaceSize / 8u;
        const std::uint32_t groupY = cubeFaceSize / 8u;
        cmd->Dispatch(groupX, groupY, 6u);

        cmd->TransitionResource(*irrTex,
                                Orange::Rhi::ResourceState::UnorderedAccess,
                                Orange::Rhi::ResourceState::ShaderResource);

        if (cmd->End() != Orange::ResultCode::Success)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeIrradiance: cmd.End 失败");
            return nullptr;
        }
        if (device.SubmitCommandList(*cmd) != Orange::ResultCode::Success)
        {
            ORANGE_LOG_ERROR("IblBaker::BakeIrradiance: SubmitCommandList 失败");
            return nullptr;
        }
        device.WaitIdle();

        return irrTex;
    }

    std::unique_ptr<Orange::Rhi::RHITexture>
    IblBaker::BakePrefilteredEnvironment(Orange::Rhi::RHITexture& envCube,
                                         std::uint32_t            baseFaceSize,
                                         std::uint32_t            mipLevels,
                                         std::uint32_t            sampleCount)
    {
        if (baseFaceSize < 8u || (baseFaceSize % 8u) != 0u)
        {
            ORANGE_LOG_ERROR("IblBaker::BakePrefilteredEnvironment: baseFaceSize 必须 ≥ 8 且为 8 的倍数 (got={})",
                             baseFaceSize);
            return nullptr;
        }
        if (sampleCount == 0u)
        {
            ORANGE_LOG_ERROR("IblBaker::BakePrefilteredEnvironment: sampleCount 必须 > 0");
            return nullptr;
        }

        // mipLevels 上限：log2(baseFaceSize) + 1（典型 256 → 9 mips）。
        std::uint32_t maxMips = 1u;
        {
            std::uint32_t s = baseFaceSize;
            while (s > 1u)
            {
                s >>= 1u;
                ++maxMips;
            }
        }
        if (mipLevels == 0u || mipLevels > maxMips)
        {
            mipLevels = maxMips;
        }

        if (!mpImpl->EnsurePrefilterPipeline())
        {
            return nullptr;
        }
        auto& device = mpImpl->device;

        // ---- 创建多 mip cube ----
        // usage: RenderTarget（per-mip view 作 ColorAttachment）+ Sampled（运行
        // 时 pbr.frag 采样）+ TransferSrc（保留 readback 入口，可选）
        Orange::Rhi::TextureDesc cubeDesc{};
        cubeDesc.mWidth       = baseFaceSize;
        cubeDesc.mHeight      = baseFaceSize;
        cubeDesc.mMipLevels   = mipLevels;
        cubeDesc.mArrayLayers = 6u;
        cubeDesc.mFormat      = Orange::Rhi::TextureFormat::RGBA16Float;
        cubeDesc.mDimension   = Orange::Rhi::TextureDimension::TexCube;
        cubeDesc.mUsage       = Orange::Rhi::TextureUsage::Sampled | Orange::Rhi::TextureUsage::RenderTarget | Orange::Rhi::TextureUsage::TransferSrc;
        auto prefilteredTex   = device.CreateTexture(cubeDesc);
        if (!prefilteredTex)
        {
            ORANGE_LOG_ERROR("IblBaker::BakePrefilteredEnvironment: CreateTexture(base={}^2, mips={}, cube) 失败",
                             baseFaceSize, mipLevels);
            return nullptr;
        }

        // ---- 6 face × N mip 子资源 view（Tex2D + 单 face 单 mip）----
        const std::uint32_t                                       totalViews = 6u * mipLevels;
        std::vector<std::unique_ptr<Orange::Rhi::RHITextureView>> subviews;
        subviews.reserve(totalViews);
        for (std::uint32_t face = 0u; face < 6u; ++face)
        {
            for (std::uint32_t mip = 0u; mip < mipLevels; ++mip)
            {
                Orange::Rhi::TextureViewDesc viewDesc{};
                viewDesc.mViewDimension  = Orange::Rhi::TextureDimension::Tex2D;
                viewDesc.mBaseMipLevel   = mip;
                viewDesc.mLevelCount     = 1u;
                viewDesc.mBaseArrayLayer = face;
                viewDesc.mLayerCount     = 1u;
                auto pView               = device.CreateTextureView(*prefilteredTex, viewDesc);
                if (!pView)
                {
                    ORANGE_LOG_ERROR("IblBaker::BakePrefilteredEnvironment: CreateTextureView(face={}, mip={}) 失败",
                                     face, mip);
                    return nullptr;
                }
                subviews.push_back(std::move(pView));
            }
        }

        // ---- per-call descriptor pool 容纳 6 × mipLevels 个 set（每 dispatch 一个）----
        Orange::Rhi::DescriptorPoolDesc poolDesc{};
        poolDesc.mMaxSets   = totalViews;
        poolDesc.mPoolSizes = {
            {Orange::Rhi::DescriptorType::CombinedImageSampler, totalViews},
        };
        poolDesc.mpDebugName = "IblBaker/Prefilter/Pool";
        auto descPool        = device.CreateDescriptorPool(poolDesc);
        if (!descPool)
        {
            ORANGE_LOG_ERROR("IblBaker::BakePrefilteredEnvironment: CreateDescriptorPool 失败");
            return nullptr;
        }

        std::vector<std::unique_ptr<Orange::Rhi::RHIDescriptorSet>> sets;
        sets.reserve(totalViews);
        for (std::uint32_t i = 0u; i < totalViews; ++i)
        {
            auto set = device.AllocateDescriptorSet(*descPool, *mpImpl->prefilterLayout);
            if (!set)
            {
                ORANGE_LOG_ERROR("IblBaker::BakePrefilteredEnvironment: AllocateDescriptorSet (idx={}) 失败", i);
                return nullptr;
            }
            Orange::Rhi::DescriptorWrite w{};
            w.mBinding             = 0u;
            w.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            w.mImageInfo.mpTexture = &envCube;
            w.mImageInfo.mpSampler = mpImpl->prefilterSampler.get();
            device.UpdateDescriptorSet(*set, &w, 1u);
            sets.push_back(std::move(set));
        }

        // ---- 录制：54 个 BeginRendering / Draw 3 / EndRendering ----
        auto cmd = device.CreateCommandList(Orange::Rhi::CommandQueueType::Graphics);
        if (!cmd)
        {
            ORANGE_LOG_ERROR("IblBaker::BakePrefilteredEnvironment: CreateCommandList 失败");
            return nullptr;
        }
        if (cmd->Begin() != Orange::ResultCode::Success)
        {
            ORANGE_LOG_ERROR("IblBaker::BakePrefilteredEnvironment: cmd.Begin 失败");
            return nullptr;
        }

        // 整张 cube：Common → RenderTarget（一次性 transition 所有 subresource）
        cmd->TransitionResource(*prefilteredTex,
                                Orange::Rhi::ResourceState::Common,
                                Orange::Rhi::ResourceState::RenderTarget);

        const float maxMipF = (mipLevels > 1u) ? static_cast<float>(mipLevels - 1u) : 1.0f;
        for (std::uint32_t face = 0u; face < 6u; ++face)
        {
            for (std::uint32_t mip = 0u; mip < mipLevels; ++mip)
            {
                const std::uint32_t idx       = face * mipLevels + mip;
                const std::uint32_t mipDim    = std::max<std::uint32_t>(1u, baseFaceSize >> mip);
                const float         roughness = (mipLevels == 1u) ? 0.0f
                                                                  : static_cast<float>(mip) / maxMipF;

                Orange::Rhi::ColorAttachment color{};
                color.mpView   = subviews[idx].get();
                color.mLoadOp  = Orange::Rhi::LoadOp::Clear;
                color.mStoreOp = Orange::Rhi::StoreOp::Store;

                Orange::Rhi::RenderingDesc rd{};
                rd.mRenderArea.mWidth  = mipDim;
                rd.mRenderArea.mHeight = mipDim;
                rd.mColorAttachments.push_back(color);
                cmd->BeginRendering(rd);

                cmd->BindGraphicsPipeline(*mpImpl->prefilterPipeline);
                cmd->SetDescriptorSet(0u, *sets[idx]);

                // viewport + scissor 必须覆盖整个 attachment（per-mip 尺寸变化）
                Orange::Rhi::RHIViewport vp{};
                vp.mX        = 0.0f;
                vp.mY        = 0.0f;
                vp.mWidth    = static_cast<float>(mipDim);
                vp.mHeight   = static_cast<float>(mipDim);
                vp.mMinDepth = 0.0f;
                vp.mMaxDepth = 1.0f;
                cmd->SetViewport(vp);

                Orange::Rhi::RHIScissor sc{};
                sc.mOffsetX = 0;
                sc.mOffsetY = 0;
                sc.mWidth   = mipDim;
                sc.mHeight  = mipDim;
                cmd->SetScissor(sc);

                // push constant：{ faceIdx, roughness, sampleCount, pad } = 16 B
                struct PrefilterPC
                {
                    std::uint32_t faceIdx;
                    float         roughness;
                    std::uint32_t sampleCount;
                    std::uint32_t pad;
                };
                const PrefilterPC pcData{face, roughness, sampleCount, 0u};
                cmd->SetPushConstants(Orange::Rhi::ShaderStage::Fragment,
                                      0u, sizeof(pcData), &pcData);

                // fullscreen big-triangle: 3 vertices, no vertex buffer
                cmd->Draw(3u, 1u, 0u, 0u);

                cmd->EndRendering();
            }
        }

        // 整张 cube：RenderTarget → ShaderResource（供下游 sampling）
        cmd->TransitionResource(*prefilteredTex,
                                Orange::Rhi::ResourceState::RenderTarget,
                                Orange::Rhi::ResourceState::ShaderResource);

        if (cmd->End() != Orange::ResultCode::Success)
        {
            ORANGE_LOG_ERROR("IblBaker::BakePrefilteredEnvironment: cmd.End 失败");
            return nullptr;
        }
        if (device.SubmitCommandList(*cmd) != Orange::ResultCode::Success)
        {
            ORANGE_LOG_ERROR("IblBaker::BakePrefilteredEnvironment: SubmitCommandList 失败");
            return nullptr;
        }
        device.WaitIdle();

        return prefilteredTex;
    }

} // namespace Orange::Engine::Render
