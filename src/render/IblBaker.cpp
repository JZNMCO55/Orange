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
#include "orange/rhi/RHISampler.h"
#include "orange/rhi/RHIShaderModule.h"
#include "orange/rhi/RHITexture.h"
#include "orange/rhi/RHITypes.h"

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
    wchar_t buffer[MAX_PATH];
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

}  // namespace

// ---------------------------------------------------------------------------
// Impl —— 持有 device / registry 引用 + lazy-init 的 compute pipeline 资源。
// 各烘焙路径按前缀分组：`equirect` / `brdfLut` / 后续 `irradiance` /
// `prefilter`。每路径自带 shader + descriptor layout + pipeline，sampler
// 按需复用（equirect 路径用，brdfLut 无 input texture 不需要）。
// ---------------------------------------------------------------------------
struct IblBaker::Impl
{
    Orange::Rhi::RHIDevice&       device;
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

    Impl(Orange::Rhi::RHIDevice& d, Orange::Engine::Asset::AssetRegistry& r)
        : device(d)
        , registry(r)
    {
    }

    // 共享 helper：把 "shaders/orange_engine/<file>" 加载为 RHIShaderModule。
    // 走 AssetRegistry::Load<ShaderAsset> 做 dedup + Get 拿 SPIR-V bytes。
    // 失败返回 nullptr，调用方按 unique_ptr 真伪判定。`debugName` 透传给
    // RHI 后端（GPU capture 工具用）。
    std::unique_ptr<Orange::Rhi::RHIShaderModule>
    LoadComputeShader(const char* spvRelative, const char* debugName)
    {
        const auto spvPath = ResolveBuiltinShaderPath(spvRelative);
        auto spvResult = registry.Load<Orange::Engine::Asset::ShaderAsset>(spvPath);
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
        modDesc.mStage      = Orange::Rhi::ShaderStage::Compute;
        modDesc.mpDebugName = debugName;
        auto module = device.CreateShaderModule(modDesc);
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

        equirectShader = LoadComputeShader(
            "shaders/orange_engine/ibl_equirect_to_cube.comp.spv",
            "IblBaker/EquirectToCube");
        if (!equirectShader)
        {
            return false;
        }

        // ---- 2. Descriptor set layout：binding 0 sampler、binding 1 storage cube ----
        Orange::Rhi::DescriptorSetLayoutDesc layoutDesc{};
        layoutDesc.mBindings = {
            { 0u, Orange::Rhi::DescriptorType::CombinedImageSampler, 1u,
              Orange::Rhi::ShaderStage::Compute },
            { 1u, Orange::Rhi::DescriptorType::StorageImage, 1u,
              Orange::Rhi::ShaderStage::Compute },
        };
        layoutDesc.mpDebugName = "IblBaker/EquirectToCube/Layout";
        equirectLayout = device.CreateDescriptorSetLayout(layoutDesc);
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
            { Orange::Rhi::ShaderStage::Compute, 0u, sizeof(std::uint32_t) });
        pipeDesc.mpDebugName = "IblBaker/EquirectToCube/Pipeline";
        equirectPipeline = device.CreateComputePipeline(pipeDesc);
        if (!equirectPipeline)
        {
            ORANGE_LOG_ERROR("IblBaker: CreateComputePipeline(equirect→cube) 失败");
            return false;
        }

        // ---- 4. 共享 linear sampler：equirect 2D 是低频环境光，linear filter
        //          + clamp-to-edge 避免接缝 wrap 引入瑕疵。----
        Orange::Rhi::SamplerDesc sampDesc{};
        sampDesc.mMinFilter   = Orange::Rhi::SamplerFilter::Linear;
        sampDesc.mMagFilter   = Orange::Rhi::SamplerFilter::Linear;
        sampDesc.mMipmapMode  = Orange::Rhi::SamplerMipmapMode::Nearest;
        sampDesc.mAddressU    = Orange::Rhi::SamplerAddressMode::Repeat;       // 经度 wrap
        sampDesc.mAddressV    = Orange::Rhi::SamplerAddressMode::ClampToEdge;  // 纬度 clamp
        sampDesc.mAddressW    = Orange::Rhi::SamplerAddressMode::ClampToEdge;
        sampDesc.mpDebugName  = "IblBaker/EquirectSampler";
        equirectSampler = device.CreateSampler(sampDesc);
        if (!equirectSampler)
        {
            ORANGE_LOG_ERROR("IblBaker: CreateSampler(equirect) 失败");
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

        brdfLutShader = LoadComputeShader(
            "shaders/orange_engine/ibl_brdf_lut.comp.spv",
            "IblBaker/BrdfLut");
        if (!brdfLutShader)
        {
            return false;
        }

        Orange::Rhi::DescriptorSetLayoutDesc layoutDesc{};
        layoutDesc.mBindings = {
            { 0u, Orange::Rhi::DescriptorType::StorageImage, 1u,
              Orange::Rhi::ShaderStage::Compute },
        };
        layoutDesc.mpDebugName = "IblBaker/BrdfLut/Layout";
        brdfLutLayout = device.CreateDescriptorSetLayout(layoutDesc);
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
            { Orange::Rhi::ShaderStage::Compute, 0u, 2u * sizeof(std::uint32_t) });
        pipeDesc.mpDebugName = "IblBaker/BrdfLut/Pipeline";
        brdfLutPipeline = device.CreateComputePipeline(pipeDesc);
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
    cubeDesc.mUsage       = Orange::Rhi::TextureUsage::Sampled
                          | Orange::Rhi::TextureUsage::Storage
                          | Orange::Rhi::TextureUsage::TransferSrc;
    auto cubeTex = device.CreateTexture(cubeDesc);
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
        { Orange::Rhi::DescriptorType::CombinedImageSampler, 1u },
        { Orange::Rhi::DescriptorType::StorageImage,         1u },
    };
    poolDesc.mpDebugName = "IblBaker/EquirectToCube/Pool";
    auto descPool = device.CreateDescriptorPool(poolDesc);
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
    writes[0].mBinding             = 0u;
    writes[0].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
    writes[0].mImageInfo.mpTexture = &equirectHdr;
    writes[0].mImageInfo.mpSampler = mpImpl->equirectSampler.get();

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
    lutDesc.mUsage       = Orange::Rhi::TextureUsage::Sampled
                         | Orange::Rhi::TextureUsage::Storage
                         | Orange::Rhi::TextureUsage::TransferSrc;
    auto lutTex = device.CreateTexture(lutDesc);
    if (!lutTex)
    {
        ORANGE_LOG_ERROR("IblBaker::BakeBrdfLut: CreateTexture({}^2 RG16F) 失败", lutSize);
        return nullptr;
    }

    // ---- per-call descriptor pool + set ----
    Orange::Rhi::DescriptorPoolDesc poolDesc{};
    poolDesc.mMaxSets   = 1u;
    poolDesc.mPoolSizes = {
        { Orange::Rhi::DescriptorType::StorageImage, 1u },
    };
    poolDesc.mpDebugName = "IblBaker/BrdfLut/Pool";
    auto descPool = device.CreateDescriptorPool(poolDesc);
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
    const std::uint32_t pcData[2] = { lutSize, sampleCount };
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

}  // namespace Orange::Engine::Render
