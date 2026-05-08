// Pipeline 实现：把 RenderScene 的 drawable 列表真正变成 OrangeRender
// 的 RenderItem 提交序列。
//
// 路由策略：每个 drawable 通过自身的 MaterialInstance 反查 Material →
// 走 per-template `RHIPipeline` 缓存（unordered_map<const Material*,
// std::unique_ptr<RHIPipeline>>）。同一 Material 多个 instance 只会编
// 译一次 pipeline；不同 Material 各自编一份。
//
// drawable.materialInstance == nullptr 是受支持的退化态：Pipeline 自
// 己 lazy-load 内置 textured Material（BuiltinMaterials::LoadTextured），
// 把它当默认 instance-less 路径——sample 不挂 MaterialSystem 也能跑。
//
// 共享的 ShaderModule 缓存按 `AssetHandle<ShaderAsset>::Value()` 去重，
// 避免同一 .spv 在多个 Material 引用时重复创建。
//
// mesh GPU cache 仍按 AssetHandle<MeshAsset> 值去重，跨实体共享。
// texture 真采样路径在 OrangeRender 暴露 sampler 后续 task 接通，本
// 阶段 Material.textureSlots 仅作 schema 占位。

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
#include "orange/engine/render/RenderScene.h"

#include "orange/renderer/RenderDevice.h"
#include "orange/renderer/Renderer.h"
#include "orange/renderer/RenderTypes.h"
#include "orange/resource/UploadContext.h"
#include "orange/rhi/RHI.h"

#include <glm/mat4x4.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Orange::Engine::Render
{
namespace
{

// 顶点 layout：interleaved 5 floats = pos(3) + uv(2)。所有内置模板共
// 用此布局；未来引入 vertex normal / tangent 时在 Material 上再加一个
// 描述字段，本布局保持稳定。
struct InterleavedVertex
{
    float position[3];
    float uv[2];
};

// 把 MaterialUniformDesc 的类型映射到 push-constant std430 字节占用。
// std430 下 vec3 与 vec4 同样吃 16 字节（vec3 尾部 4 字节 padding），
// 与内置 toon / rim_light 的 push_constant block 注释一致。
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

// 把 MaterialAsset 反查到的 SPIR-V 字节流装进 RHIShaderModule。
// 失败语义：handle 无效 / 资源未加载 / SPIR-V 为空 → 返回 nullptr，
// 让上层放弃当前 drawable。
std::unique_ptr<Orange::Rhi::RHIShaderModule> CreateShaderModule(
    Orange::Rhi::RHIDevice&         rhi,
    const Asset::ShaderAsset&       asset,
    Orange::Rhi::ShaderStage        stage,
    const char*                     debugName)
{
    Orange::Rhi::ShaderModuleDesc desc{};
    desc.mpCode      = asset.SpirV().data();
    desc.mCodeSize   = asset.ByteSize();
    desc.mStage      = stage;
    desc.mpDebugName = debugName;
    return rhi.CreateShaderModule(desc);
}

// 把 InterleavedVertex 顶点流构出 GraphicsPipelineDesc 的 vertex input
// 段。所有内置模板共用同一布局——单个 binding，pos(loc=0) + uv(loc=1)。
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

// 按 Material.uniforms 推算 push-constant 总大小，给 GraphicsPipelineDesc
// 加一对 Vertex/Fragment range（同 offset / 同 size，不同 stage——
// OrangeRender 的 PushConstantRange::mStage 只支持单 stage，多 stage
// 需要拆成多条 range）。
//
// 内置 toon / rim_light 的 push_constant block 在 vertex + fragment 双侧
// 共用，必须把两段 stage 都声明，否则 Vulkan 校验会因"shader 在 X 阶段
// 用了 push constant 但 pipeline layout 未声明该 stage"报错。textured
// 只 vertex 用 push constant，多声明一段 fragment range 是无害的——
// 没用到的 range 不被任何 shader 引用。
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

    Orange::Rhi::PushConstantRange fs{};
    fs.mStage  = Orange::Rhi::ShaderStage::Fragment;
    fs.mOffset = 0;
    fs.mSize   = size;
    desc.mPushConstantRanges.push_back(fs);
}

// 把 MeshAsset 的 positions + uvs 打成 interleaved 顶点流。当 mesh
// 没 UV 时填 (0, 0)，让 vertex layout 仍然成立。
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

}  // namespace

struct Pipeline::Impl
{
    RenderScene                            scene;
    Asset::AssetRegistry*                  assets{nullptr};

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
    std::unordered_map<const Material*, std::unique_ptr<Orange::Rhi::RHIPipeline>> templatePipelines;

    struct MeshGpu
    {
        std::unique_ptr<Orange::Rhi::RHIBuffer> vertexBuffer;
        std::unique_ptr<Orange::Rhi::RHIBuffer> indexBuffer;
        std::uint32_t                           indexCount{0};
    };
    // key = AssetHandle<MeshAsset>::Value()
    std::unordered_map<std::uint64_t, MeshGpu> meshCache;

    std::uint64_t frameIndex{0};

    // 解析 / 缓存 builtin textured Material；首次调用时通过 BuiltinMaterials
    // 工厂把 SPIR-V 注册到 AssetRegistry。失败 → 返回 nullptr，调用方
    // 跳过该 drawable。
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

    // ShaderModule lazy compile / cache by AssetHandle.Value()。
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
        auto module = CreateShaderModule(renderDevice->GetRhiDevice(), *asset, stage, debugName);
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

    // 取 / 编 per-template Pipeline。同一 Material 多次调用返回同一指针。
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
        // 与原 hardcoded textured pipeline 一致：背面剔除 + Vulkan CCW
        // front-face；与 OrangeRender procedural_scene 同约定。
        desc.mRasterizer.mCullMode    = Orange::Rhi::CullMode::Back;
        desc.mRasterizer.mFrontFace   = Orange::Rhi::FrontFace::CounterClockwise;
        desc.mDepthStencil.mDepthTestEnable  = false;
        desc.mDepthStencil.mDepthWriteEnable = false;
        desc.mColorBlend.mAttachments.push_back({});
        desc.mRenderTargets.mColorFormats.push_back(Orange::Rhi::TextureFormat::BGRA8Unorm);

        FillPushConstantRanges(desc, ComputePushConstantSize(mat));

        // 不挂 mpDebugName ——指针指向临时局部 string，OrangeRender 内部
        // 不会拷贝。后续可考虑把 debug name 跟着 pipeline 一起持久化。
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

    // ShaderModule / per-template Pipeline 不在 Initialize 阶段编译——
    // 第一次 Render 看到某个 Material 时再 lazy compile，避免"装了引擎
    // 但还没决定 Material 模板"时无谓启动。

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

    impl.assets      = nullptr;
    impl.initialized = false;
}

void Pipeline::OnResize(std::uint32_t width, std::uint32_t height)
{
    auto& impl = *mpImpl;
    if (!impl.initialized || !impl.renderer)
    {
        return;
    }
    impl.renderer->OnResize(width, height);
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

    constexpr double kAssumedDt = 1.0 / 60.0;
    Orange::Renderer::FrameTimeInfo timeInfo{};
    timeInfo.mTotalTimeSeconds = impl.frameIndex * kAssumedDt;
    timeInfo.mDeltaTimeSeconds = static_cast<float>(kAssumedDt);

    if (Orange::Failed(impl.renderer->BeginFrame(timeInfo)))
    {
        ORANGE_LOG_ERROR("Pipeline::Render: BeginFrame 失败 (frame={})", impl.frameIndex);
        return;
    }

    if (!impl.scene.HasCamera())
    {
        if (Orange::Failed(impl.renderer->EndFrame()))
        {
            ORANGE_LOG_ERROR("Pipeline::Render: EndFrame 失败 (frame={})", impl.frameIndex);
        }
        ++impl.frameIndex;
        return;
    }

    const glm::mat4 viewProj = impl.scene.MainCamera().projection * impl.scene.MainCamera().view;

    auto& rhi = impl.renderDevice->GetRhiDevice();

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

            Impl::MeshGpu gpu;

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
            cacheIt = impl.meshCache.emplace(key, std::move(gpu)).first;
        }

        const auto& gpu = cacheIt->second;

        // 路由 Material：drawable 显式挂的 instance 优先；未挂时落到
        // Pipeline 自己 lazy-load 的内置 textured Material。
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

        // MVP = projection * view * world，按列向量惯例。
        const glm::mat4 mvp = viewProj * drawable.worldMatrix;

        Orange::Renderer::RenderItem item{};
        item.mpPipeline           = rhiPipeline;
        item.mpVertexBuffer       = gpu.vertexBuffer.get();
        item.mpIndexBuffer        = gpu.indexBuffer.get();
        item.mIndexFormat         = Orange::Rhi::IndexFormat::UInt32;
        item.mDraw.mIndexCount    = gpu.indexCount;
        item.mDraw.mInstanceCount = 1;
        item.mTransform           = drawable.worldMatrix;

        // Push constant：仍只发 64 字节 uMVP 给 vertex 阶段。后续子任
        // 务接通 MaterialInstance 覆盖打包时再扩到完整 size + 双 stage。
        std::memcpy(item.mPushConstantData.data(), &mvp, sizeof(glm::mat4));
        item.mPushConstantSize   = sizeof(glm::mat4);
        item.mPushConstantOffset = 0;
        item.mPushConstantStage  = Orange::Rhi::ShaderStage::Vertex;

        impl.renderer->SubmitItem(item);
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

}  // namespace Orange::Engine::Render
