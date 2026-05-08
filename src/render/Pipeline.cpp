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
// 后处理链 / 自定义 RenderPass 真正接通在 06.04（Bloom）/ 06.05（Tonemap +
// LUT）/ Phase 5（自定义 InsertPass）跟进；本 task 只把 Pipeline 双段流
// 程铺出来 + 给后续子任务留好 SetPostProcessChain / SetMaterialSystem 入
// 口与 IPostProcessPass context 字段。

#include "orange/engine/render/Pipeline.h"

#include "orange/engine/asset/AssetRegistry.h"
#include "orange/engine/asset/MeshAsset.h"
#include "orange/engine/asset/ShaderAsset.h"
#include "orange/engine/core/Log.h"
#include "orange/engine/platform/Window.h"
#include "orange/engine/render/BuiltinMaterials.h"
#include "orange/engine/render/Material.h"
#include "orange/engine/render/MaterialInstance.h"
#include "orange/engine/render/MaterialTypes.h"
#include "orange/engine/render/PostProcessChain.h"
#include "orange/engine/render/PostProcessPasses.h"
#include "orange/engine/render/RenderScene.h"

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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
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
namespace
{

// 顶点 layout：interleaved 5 floats = pos(3) + uv(2)。所有内置模板共
// 用本布局；未来引入 vertex normal / tangent 时在 Material 上再加一个
// 描述字段，本布局保持稳定。
struct InterleavedVertex
{
    float position[3];
    float uv[2];
};

// std430 push-constant 字节占用——vec3 padded 到 16，与内置 toon /
// rim_light 的 push_constant block 注释一致。
std::uint32_t PushConstantBytesFor(MaterialUniformType type) noexcept
{
    switch (type)
    {
        case MaterialUniformType::Float: return 4;
        case MaterialUniformType::Int:   return 4;
        case MaterialUniformType::Vec2:  return 8;
        case MaterialUniformType::Vec3:  return 16;
        case MaterialUniformType::Vec4:  return 16;
        case MaterialUniformType::Mat4:  return 64;
    }
    return 0;
}

std::uint32_t ComputePushConstantSize(const Material& mat) noexcept
{
    std::uint32_t total = 0;
    for (const auto& u : mat.uniforms)
    {
        total += PushConstantBytesFor(u.type);
    }
    return total;
}

void FillVertexInputLayout(Orange::Rhi::GraphicsPipelineDesc& desc)
{
    Orange::Rhi::VertexBindingDesc binding{};
    binding.mBinding   = 0;
    binding.mStride    = sizeof(InterleavedVertex);
    binding.mInputRate = Orange::Rhi::VertexInputRate::Vertex;
    desc.mVertexInput.mBindings.push_back(binding);

    Orange::Rhi::VertexAttributeDesc attrPos{};
    attrPos.mLocation = 0;
    attrPos.mBinding  = 0;
    attrPos.mOffset   = offsetof(InterleavedVertex, position);
    attrPos.mFormat   = Orange::Rhi::VertexFormat::Float32x3;
    desc.mVertexInput.mAttributes.push_back(attrPos);

    Orange::Rhi::VertexAttributeDesc attrUV{};
    attrUV.mLocation = 1;
    attrUV.mBinding  = 0;
    attrUV.mOffset   = offsetof(InterleavedVertex, uv);
    attrUV.mFormat   = Orange::Rhi::VertexFormat::Float32x2;
    desc.mVertexInput.mAttributes.push_back(attrUV);
}

// 给 GraphicsPipelineDesc 加单条 Vertex stage 的 push range。
//
// 历史决策：曾尝试同 offset / 同 size 同时声明 Vertex + Fragment 双
// range（让 toon / rim_light 的 fragment 端 push_constant 引用合法），
// 但 Vulkan 校验要求 vkCmdPushConstants 的 stageFlags 必须涵盖所有重叠
// range 的 stage——OrangeRender 当前的 `PushConstantRange::mStage` 与
// `SetPushConstants` 都只支持单 stage，没法一次发到两段 stage，重叠双
// range 会跑出 VK_ERROR-级 validation。
//
// 折中：只声明 Vertex range。textured 只 vertex 用 push_constant、不
// 受影响；toon / rim_light 的 fragment 端读取 push_constant 会触发
// "shader 在 X 阶段用 push constant 但 layout 没声明 X" 的 validation
// 提示，pipeline 仍能创建但 fragment 端读到 undefined 内容——视觉正确
// 性等到 OrangeRender 把 PushConstantRange 升级为多 stage（或 SetPushConstants
// 支持多 stage flag）后跟进。当前 0.x 阶段 toon / rim_light 视觉表现
// 不在 Phase 3 / Task 06 验收范围（Task 07 接通 light UBO + shadow 时
// 视觉才进入"正式" 状态）。
void FillPushConstantRanges(Orange::Rhi::GraphicsPipelineDesc& desc, std::uint32_t size)
{
    if (size == 0)
    {
        return;
    }
    Orange::Rhi::PushConstantRange vs{};
    vs.mStage  = Orange::Rhi::ShaderStage::Vertex;
    vs.mOffset = 0;
    vs.mSize   = size;
    desc.mPushConstantRanges.push_back(vs);
}

std::vector<InterleavedVertex> InterleaveMesh(const Asset::MeshAsset& mesh)
{
    const auto& positions = mesh.Positions();
    const auto& uvs       = mesh.UVs();
    std::vector<InterleavedVertex> out(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i)
    {
        out[i].position[0] = positions[i].x;
        out[i].position[1] = positions[i].y;
        out[i].position[2] = positions[i].z;
        if (i < uvs.size())
        {
            out[i].uv[0] = uvs[i].u;
            out[i].uv[1] = uvs[i].v;
        }
        else
        {
            out[i].uv[0] = 0.0f;
            out[i].uv[1] = 0.0f;
        }
    }
    return out;
}

// 解析当前可执行体所在目录。CWD 与 .exe 目录可能不一致；把内置
// passthrough / fullscreen .spv 锚定到 .exe 同目录的
// `shaders/orange_engine/` 更稳——同 BuiltinMaterials 的路径解析风格。
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

// 把 .exe 同目录下的 SPIR-V 直接读成 word 流——不走 AssetRegistry
// （内部 fullscreen / passthrough shader 不挂资源句柄、生命周期与
// Pipeline 同进退）。
std::vector<std::uint32_t> LoadSpirv(const char* relativePath)
{
    const auto fullPath = (GetExecutableDir() / relativePath).string();
    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (!file)
    {
        ORANGE_LOG_ERROR("Pipeline: 无法打开内置 SPIR-V {}", fullPath);
        return {};
    }
    const std::streamsize size = file.tellg();
    if (size <= 0 || (size % 4) != 0)
    {
        ORANGE_LOG_ERROR("Pipeline: SPIR-V 大小非法 ({}) for {}",
                         static_cast<long long>(size), fullPath);
        return {};
    }
    std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), size);
    return words;
}

constexpr Orange::Rhi::TextureFormat kHdrColorFormat      = Orange::Rhi::TextureFormat::RGBA16Float;
constexpr Orange::Rhi::TextureFormat kSwapchainColorFormat = Orange::Rhi::TextureFormat::BGRA8Unorm;

// Bloom mip-chain：6 张 RGBA16F，从 HDR/2 一路下采到 HDR/64。下采 6 次
// 喂出 6 张 mip；上采 5 次按 mip[N+1] tent → additive blend 累加进 mip[N]；
// 最终 mip[0] 作为"bloom 末态"喂给 stage B 的 passthrough_combine。
constexpr std::size_t kBloomMipCount = 6;

}  // namespace

struct Pipeline::Impl
{
    RenderScene                            scene;
    Asset::AssetRegistry*                  assets{nullptr};
    Platform::Window*                      window{nullptr};

    bool initialized{false};

    std::unique_ptr<Orange::Renderer::RenderDevice> renderDevice;
    std::unique_ptr<Orange::Renderer::IRenderer>    renderer;
    std::unique_ptr<Orange::Resource::UploadContext> upload;

    // drawable.materialInstance == nullptr 时的 fallback Material。第一
    // 次需要时 lazy-load——sample 即便不挂 MaterialSystem 也能跑通。
    Material builtinTexturedMaterial;
    bool     builtinTexturedLoaded{false};

    // ShaderModule 缓存：键 = AssetHandle<ShaderAsset>::Value()。同 .spv
    // 跨 Material 复用。
    std::unordered_map<std::uint64_t, std::unique_ptr<Orange::Rhi::RHIShaderModule>> shaderModules;

    // Per-template Pipeline 缓存：键 = const Material*（地址稳定来自
    // MaterialSystem 内表 / Pipeline 自身的 builtinTexturedMaterial）。
    // 这些 pipeline 输出格式是 RGBA16F（HDR off-screen），与 swap-chain
    // 的 BGRA8Unorm 不同——离屏段绑 BeginRendering 时要求格式匹配。
    std::unordered_map<const Material*, std::unique_ptr<Orange::Rhi::RHIPipeline>> templatePipelines;

    struct MeshGpu
    {
        std::unique_ptr<Orange::Rhi::RHIBuffer> vertexBuffer;
        std::unique_ptr<Orange::Rhi::RHIBuffer> indexBuffer;
        std::uint32_t                           indexCount{0};
    };
    std::unordered_map<std::uint64_t, MeshGpu> meshCache;

    // ---- 双段 frame 流程相关资源 -----------------------------------------
    // 离屏 HDR scene color。每次 OnResize / 首帧前按 framebuffer extent
    // 重建。
    std::unique_ptr<Orange::Rhi::RHITexture> hdrColor;
    std::uint32_t                            hdrWidth{0};
    std::uint32_t                            hdrHeight{0};
    bool                                     hdrLayoutShaderReadOnly{false};

    // Pipeline 持续复用的离屏 cmd list（Begin/End/Submit 反复触发）。
    std::unique_ptr<Orange::Rhi::RHICommandList> offscreenCmd;

    // Passthrough 收尾资源 —— sampler / descriptor set layout / pool /
    // set / shader modules / pipeline。set 的 binding=0 在每次 hdrColor
    // 重建后通过 UpdateDescriptorSet 重新指向新 view。
    std::unique_ptr<Orange::Rhi::RHISampler>             hdrSampler;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> passthroughLayout;
    std::unique_ptr<Orange::Rhi::RHIDescriptorPool>      passthroughPool;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       passthroughSet;
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        fullscreenVs;
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        passthroughFs;
    std::unique_ptr<Orange::Rhi::RHIPipeline>            passthroughPipeline;

    // 期望的 framebuffer 尺寸——OnResize 写入，Render 顶部按它重建 HDR。
    std::uint32_t pendingWidth{0};
    std::uint32_t pendingHeight{0};
    bool          hdrDirty{true};

    std::uint64_t frameIndex{0};

    // 非拥有指针；当前阶段仅持有，下游子任务真正消费。
    PostProcessChain* postProcessChain{nullptr};
    MaterialSystem*   materialSystem{nullptr};

    // ---- Bloom mip-chain 资源 ------------------------------------------
    // chain 含 BloomPass 时按 HDR target 尺寸建 6 张 RGBA16F；HDR 重建 /
    // chain 切换 / Pipeline 重建时同步重建。
    struct BloomMip
    {
        std::unique_ptr<Orange::Rhi::RHITexture> texture;
        std::uint32_t width{0};
        std::uint32_t height{0};
        bool layoutShaderReadOnly{false};
    };
    std::array<BloomMip, kBloomMipCount> bloomMips;
    bool bloomMipsReady{false};

    // bloom layout & pool 与 stage B 的 passthrough 各自独立，避免 set
    // 数与 binding 数互相挤压。
    std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> bloomLayout;       // 1 binding
    std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> combineLayout;     // 2 bindings
    std::unique_ptr<Orange::Rhi::RHIDescriptorPool>      bloomPool;
    // 6 个 downsample set + 5 个 upsample set + 1 个 combine set = 12
    // sets。每个 down/up set 1 个 CombinedImageSampler；combine set 2 个。
    // Pool 总量按 12 sets / 13 CombinedImageSampler 预算。
    std::array<std::unique_ptr<Orange::Rhi::RHIDescriptorSet>, kBloomMipCount>     bloomDownsampleSets;
    std::array<std::unique_ptr<Orange::Rhi::RHIDescriptorSet>, kBloomMipCount - 1> bloomUpsampleSets;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>                                  bloomCombineSet;

    std::unique_ptr<Orange::Rhi::RHIShaderModule> bloomDownsampleFs;
    std::unique_ptr<Orange::Rhi::RHIShaderModule> bloomUpsampleFs;
    std::unique_ptr<Orange::Rhi::RHIShaderModule> passthroughCombineFs;

    std::unique_ptr<Orange::Rhi::RHIPipeline> bloomDownsamplePipeline;
    std::unique_ptr<Orange::Rhi::RHIPipeline> bloomUpsamplePipeline;
    std::unique_ptr<Orange::Rhi::RHIPipeline> passthroughCombinePipeline;

    // -----------------------------------------------------------------

    const Material* EnsureBuiltinTexturedMaterial()
    {
        if (builtinTexturedLoaded)
        {
            return &builtinTexturedMaterial;
        }
        if (assets == nullptr)
        {
            return nullptr;
        }
        builtinTexturedMaterial = BuiltinMaterials::LoadTextured(*assets);
        builtinTexturedLoaded   = true;
        return &builtinTexturedMaterial;
    }

    Orange::Rhi::RHIShaderModule* GetOrCreateShaderModule(
        const Asset::AssetHandle<Asset::ShaderAsset>& handle,
        Orange::Rhi::ShaderStage                       stage,
        const char*                                    debugName)
    {
        if (!handle.IsValid() || assets == nullptr || renderDevice == nullptr)
        {
            return nullptr;
        }
        if (auto it = shaderModules.find(handle.Value()); it != shaderModules.end())
        {
            return it->second.get();
        }
        const auto* asset = assets->Get(handle);
        if (asset == nullptr || asset->Empty())
        {
            ORANGE_LOG_ERROR("Pipeline: ShaderAsset 无效或为空 (handle={})",
                             static_cast<unsigned long long>(handle.Value()));
            return nullptr;
        }
        Orange::Rhi::ShaderModuleDesc desc{};
        desc.mpCode      = asset->SpirV().data();
        desc.mCodeSize   = asset->ByteSize();
        desc.mStage      = stage;
        desc.mpDebugName = debugName;
        auto module = renderDevice->GetRhiDevice().CreateShaderModule(desc);
        if (!module)
        {
            ORANGE_LOG_ERROR("Pipeline: CreateShaderModule 失败 (handle={})",
                             static_cast<unsigned long long>(handle.Value()));
            return nullptr;
        }
        auto* raw = module.get();
        shaderModules.emplace(handle.Value(), std::move(module));
        return raw;
    }

    Orange::Rhi::RHIPipeline* GetOrCompilePipeline(const Material& mat)
    {
        if (auto it = templatePipelines.find(&mat); it != templatePipelines.end())
        {
            return it->second.get();
        }
        if (renderDevice == nullptr)
        {
            return nullptr;
        }
        if (!mat.vertexShader.IsValid() || !mat.fragmentShader.IsValid())
        {
            ORANGE_LOG_ERROR("Pipeline: Material '{}' 的 shader handle 无效，跳过编 pipeline",
                             mat.name);
            return nullptr;
        }

        const std::string vsLabel = "orange_engine.material." + mat.name + ".vert";
        const std::string fsLabel = "orange_engine.material." + mat.name + ".frag";
        auto* vsModule = GetOrCreateShaderModule(mat.vertexShader,
                                                 Orange::Rhi::ShaderStage::Vertex,
                                                 vsLabel.c_str());
        auto* fsModule = GetOrCreateShaderModule(mat.fragmentShader,
                                                 Orange::Rhi::ShaderStage::Fragment,
                                                 fsLabel.c_str());
        if (vsModule == nullptr || fsModule == nullptr)
        {
            return nullptr;
        }

        Orange::Rhi::GraphicsPipelineDesc desc{};
        desc.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,   vsModule, "main"});
        desc.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment, fsModule, "main"});

        FillVertexInputLayout(desc);

        desc.mInputAssembly.mTopology = Orange::Rhi::PrimitiveTopology::TriangleList;
        desc.mRasterizer.mCullMode    = Orange::Rhi::CullMode::Back;
        desc.mRasterizer.mFrontFace   = Orange::Rhi::FrontFace::CounterClockwise;
        desc.mDepthStencil.mDepthTestEnable  = false;
        desc.mDepthStencil.mDepthWriteEnable = false;
        desc.mColorBlend.mAttachments.push_back({});
        // 离屏 HDR 目标 = RGBA16F；与 BeginRendering 喂的 attachment 一致。
        desc.mRenderTargets.mColorFormats.push_back(kHdrColorFormat);

        FillPushConstantRanges(desc, ComputePushConstantSize(mat));

        desc.mpDebugName = nullptr;

        auto pipeline = renderDevice->GetRhiDevice().CreateGraphicsPipeline(desc);
        if (!pipeline)
        {
            ORANGE_LOG_ERROR("Pipeline: CreateGraphicsPipeline 失败 (material='{}')", mat.name);
            return nullptr;
        }
        auto* raw = pipeline.get();
        templatePipelines.emplace(&mat, std::move(pipeline));
        return raw;
    }

    // Initialize 时一次性按 window 查询当前 framebuffer 尺寸；之后由
    // OnResize 推动尺寸更新——Render 顶部不再回查 window，避免覆盖
    // 调用方显式 OnResize 推过来的尺寸（典型场景：headless ctest 不
    // 触发 GLFW resize 但希望 OnResize 模拟尺寸切换）。
    void SeedExtentFromWindow()
    {
        if (window == nullptr)
        {
            return;
        }
        std::uint32_t w = 0;
        std::uint32_t h = 0;
        window->GetFramebufferSize(w, h);
        pendingWidth  = w;
        pendingHeight = h;
        hdrDirty      = true;
    }

    // 离屏段开始之前先把所有 mesh 上传到 GPU——UploadContext 的内部
    // transient cmd 必须在 offscreenCmd.Begin 之前完成。
    void EnsureMeshGpuCache();

    // 自管 cmd list 跑离屏 HDR 主 pass。返回 false 表示本帧离屏失败、
    // 调用方应跳过 Stage B 的 SubmitItem。
    bool RecordOffscreenPass(const glm::mat4& viewProj);

    // 在已经 Begin 的 offscreenCmd 上追加 6 round downsample + 5 round
    // upsample。预期调用顺序：主 pass 已 transition HDR 到 ShaderReadOnly。
    // 失败 → 返回 false，调用方跳过 stage B 的 combine。
    bool RecordBloomChain(const BloomPass& bloomDesc);

    // 检测当前 chain 是否含 BloomPass + 取其参数。chain==nullptr / 没找
    // 到 → 返回 nullptr。
    const BloomPass* FindActiveBloomPass() const noexcept;

    // 创建 / 重建 bloom 6 张 mip + 描述符 set（首次激活、HDR 尺寸变化、
    // chain 切到含 BloomPass 时触发）。失败返回 false。
    bool EnsureBloomResources();

    // 释放 bloom 资源（chain 切回不含 BloomPass、Shutdown 时调用）。
    void ReleaseBloomResources();

    // 创建 / 重建 HDR off-screen target；descriptor set 同步重写指向新
    // view。size == 0 时跳过——窗口最小化、initialize 早期的 windows 没
    // framebuffer extent 都走这一路。
    bool EnsureHdrTarget()
    {
        if (!hdrDirty && hdrColor &&
            hdrWidth == pendingWidth && hdrHeight == pendingHeight)
        {
            return true;
        }
        if (pendingWidth == 0 || pendingHeight == 0)
        {
            return false;
        }
        if (renderDevice == nullptr || !passthroughSet || !hdrSampler)
        {
            return false;
        }

        // GPU 可能还在 sample 旧 hdrColor —— 等空再释放，0.x 阶段够用。
        renderDevice->WaitIdle();

        Orange::Rhi::TextureDesc t{};
        t.mWidth     = pendingWidth;
        t.mHeight    = pendingHeight;
        t.mFormat    = kHdrColorFormat;
        t.mUsage     = Orange::Rhi::TextureUsage::RenderTarget
                     | Orange::Rhi::TextureUsage::Sampled;
        auto newTex = renderDevice->GetRhiDevice().CreateTexture(t);
        if (!newTex)
        {
            ORANGE_LOG_ERROR("Pipeline: CreateTexture (HDR off-screen) 失败 ({}x{})",
                             pendingWidth, pendingHeight);
            return false;
        }
        hdrColor = std::move(newTex);
        hdrWidth = pendingWidth;
        hdrHeight = pendingHeight;
        hdrLayoutShaderReadOnly = false;  // 新建出来层 = Undefined
        hdrDirty = false;

        // 把 descriptor set 指向新 view。
        Orange::Rhi::DescriptorWrite w{};
        w.mBinding             = 0;
        w.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
        w.mImageInfo.mpTexture = hdrColor.get();
        w.mImageInfo.mpSampler = hdrSampler.get();
        renderDevice->GetRhiDevice().UpdateDescriptorSet(*passthroughSet, &w, 1);

        // HDR 重建 → bloom mip 全部失效（descriptor set 里 binding 0
        // 还指向旧 hdrColor）。下一次 EnsureBloomResources 触发完整重建。
        if (bloomMipsReady)
        {
            ReleaseBloomResources();
        }
        return true;
    }
};

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

    // 1. RenderDevice ----------------------------------------------------
    Orange::Renderer::RenderDeviceDesc deviceDesc{};
    deviceDesc.mBackend          = Orange::Renderer::BackendType::Default;
    deviceDesc.mEnableValidation = true;
    impl.renderDevice = Orange::Renderer::RenderDevice::Create(deviceDesc);
    if (!impl.renderDevice)
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: RenderDevice::Create 失败");
        return ResultCode::InternalError;
    }

    // 2. Renderer --------------------------------------------------------
    impl.renderer = Orange::Renderer::CreateRenderer();
    if (!impl.renderer)
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: CreateRenderer 失败");
        impl.renderDevice.reset();
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
        impl.renderDevice.reset();
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
        impl.renderDevice.reset();
        return ResultCode::InternalError;
    }

    auto& rhi = impl.renderDevice->GetRhiDevice();

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
        auto downCode    = LoadSpirv("shaders/orange_engine/bloom_downsample.frag.spv");
        auto upCode      = LoadSpirv("shaders/orange_engine/bloom_upsample.frag.spv");
        auto combineCode = LoadSpirv("shaders/orange_engine/passthrough_combine.frag.spv");
        if (downCode.empty() || upCode.empty() || combineCode.empty())
        {
            Shutdown();
            return ResultCode::IoError;
        }

        Orange::Rhi::ShaderModuleDesc sm{};
        sm.mStage = Orange::Rhi::ShaderStage::Fragment;

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

        if (!impl.bloomDownsampleFs || !impl.bloomUpsampleFs || !impl.passthroughCombineFs)
        {
            ORANGE_LOG_ERROR("Pipeline::Initialize: bloom shader 模块创建失败");
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
    if (!impl.bloomDownsamplePipeline || !impl.bloomUpsamplePipeline ||
        !impl.passthroughCombinePipeline)
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: bloom / combine pipeline 创建失败");
        Shutdown();
        return ResultCode::InternalError;
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

    impl.ReleaseBloomResources();
    impl.passthroughCombinePipeline.reset();
    impl.bloomUpsamplePipeline.reset();
    impl.bloomDownsamplePipeline.reset();
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
    impl.offscreenCmd.reset();

    if (impl.upload)
    {
        impl.upload->Shutdown();
        impl.upload.reset();
    }

    impl.builtinTexturedMaterial = Material{};
    impl.builtinTexturedLoaded   = false;

    if (impl.renderer)
    {
        impl.renderer->Shutdown();
    }
    impl.renderer.reset();
    impl.renderDevice.reset();

    impl.assets       = nullptr;
    impl.window       = nullptr;
    impl.initialized  = false;
    impl.pendingWidth = 0;
    impl.pendingHeight= 0;
    impl.hdrWidth     = 0;
    impl.hdrHeight    = 0;
    impl.hdrDirty     = true;
    impl.hdrLayoutShaderReadOnly = false;
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

// Helpers expecting Pipeline::Impl access live as friend free functions
// declared inside the class — keep this anonymous namespace empty.
namespace
{

// 离屏段开始之前先把所有 mesh 上传到 GPU——UploadContext 的内部
// transient cmd 必须在 offscreenCmd.Begin 之前完成。
[[maybe_unused]] void EnsureMeshGpuCacheStub() {}

}  // namespace

bool Pipeline::Impl::RecordOffscreenPass(const glm::mat4& viewProj)
{
    auto& impl = *this;
    auto& cmd = *impl.offscreenCmd;
    // Caller (Render) 已经 cmd.Begin() —— 这里只录制主 pass + transition，
    // 后续 bloom 链 / End / Submit 由 Render 顶层负责，让所有 GPU 工作进
    // 入同一 cmd list 同一 Submit。

    const auto fromLayout = impl.hdrLayoutShaderReadOnly
        ? Orange::Rhi::TextureLayout::ShaderReadOnly
        : Orange::Rhi::TextureLayout::Undefined;
    cmd.TransitionTexture(*impl.hdrColor, fromLayout,
                          Orange::Rhi::TextureLayout::ColorAttachment);

    Orange::Rhi::ColorAttachment att{};
    att.mpView          = impl.hdrColor->GetDefaultView();
    att.mLoadOp         = Orange::Rhi::LoadOp::Clear;
    att.mStoreOp        = Orange::Rhi::StoreOp::Store;
    att.mClear.mColor[0] = 0.05f;
    att.mClear.mColor[1] = 0.07f;
    att.mClear.mColor[2] = 0.10f;
    att.mClear.mColor[3] = 1.0f;

    Orange::Rhi::RenderingDesc rd{};
    rd.mRenderArea.mWidth  = impl.hdrWidth;
    rd.mRenderArea.mHeight = impl.hdrHeight;
    rd.mColorAttachments.push_back(att);

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
            mat = impl.EnsureBuiltinTexturedMaterial();
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
        }

        // Push constant：仍只发 64 字节 uMVP 给 vertex 阶段。后续子任
        // 务接通 MaterialInstance 覆盖打包时再扩到完整 size + 双 stage。
        const glm::mat4 mvp = viewProj * drawable.worldMatrix;
        cmd.SetPushConstants(Orange::Rhi::ShaderStage::Vertex,
                             /*offset=*/0,
                             static_cast<std::uint32_t>(sizeof(glm::mat4)),
                             &mvp);

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

void Pipeline::Render(Orange::Engine::World& world)
{
    auto& impl = *mpImpl;

    impl.scene.Clear();
    impl.scene.Collect(world);

    if (!impl.initialized)
    {
        return;
    }

    // 0. HDR target 同步。OnResize 已经把新尺寸写到 pendingWidth/Height；
    // 这里负责按需重建。窗口最小化（extent == 0×0）时 EnsureHdrTarget 返
    // 回 false，Pipeline 仍跑 Renderer.BeginFrame/EndFrame 但跳过 stage A。
    const bool hdrReady = impl.EnsureHdrTarget();

    // 检测 chain 里的 BloomPass —— 决定是否在 stage A 末尾追加 bloom mip
    // chain，并决定 stage B 走 passthrough 还是 passthrough_combine。
    const BloomPass* activeBloom = impl.FindActiveBloomPass();
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

    // 1. mesh GPU 上传必须在自管 cmd 之外完成（UploadContext 内部 transient
    // cmd 与我们的 offscreenCmd 不能嵌套）。
    if (hdrReady && impl.scene.HasCamera())
    {
        impl.EnsureMeshGpuCache();
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
            offscreenOk = impl.RecordOffscreenPass(viewProj);

            if (offscreenOk && activeBloom != nullptr && impl.bloomMipsReady)
            {
                offscreenOk = impl.RecordBloomChain(*activeBloom);
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

        if (activeBloom != nullptr && impl.bloomMipsReady)
        {
            // Bloom 路径：passthrough_combine + 双 binding (HDR + bloom)
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
