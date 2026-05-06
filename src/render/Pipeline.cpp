// Pipeline 实现：把 RenderScene 的 drawable 列表真正变成 OrangeRender
// 的 RenderItem 提交序列。
//
// 内部职责：
//   * Initialize 走 RenderDevice / Renderer / 内置 GraphicsPipeline /
//     textured-mesh shader / UploadContext 的完整启动；
//   * Render 每帧顶部 Clear+Collect RenderScene；接着遍历 drawables，
//     对未上传过的 mesh 调 UploadContext::UploadBuffer 把
//     pos+uv interleaved 顶点 / uint32 索引推到 GPU；最后 SubmitItem
//     带上 push-constant MVP（projection * view * world）。
//
// 当前阶段的 mesh GPU cache 只看 AssetHandle 的 64-bit 值——同 mesh
// 资源跨实体共享时只上传一次。texture 的 GPU 上传 / sampler 绑定还
// 没接（fragment shader 当前从 uv 程序式合成 checker，与 OrangeRender
// 自己的 textured_mesh sample 同思路），等 OrangeRender 的 RHI 把
// descriptor-set / sampler 路径暴露上来后再补。

#include "orange/engine/render/Pipeline.h"

#include "orange/engine/asset/AssetRegistry.h"
#include "orange/engine/asset/MeshAsset.h"
#include "orange/engine/core/Log.h"
#include "orange/engine/platform/Window.h"
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

constexpr const char* kBuiltinVertSpvRelative = "shaders/orange_engine/textured_mesh.vert.spv";
constexpr const char* kBuiltinFragSpvRelative = "shaders/orange_engine/textured_mesh.frag.spv";

// 解析当前可执行体所在目录。CWD 可能与 .exe 目录不一致（尤其是
// 从 repo 根用 `build/bin/Debug/...exe` 跑时），所以把 SPIR-V 路径
// 锚定到 .exe 自身所在目录更稳。Win32 用 GetModuleFileName；其它平
// 台暂时回退到 fs::current_path（Phase 2 工程只发 Windows，未来扩
// 平台时这里再补）。
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

// 顶点 layout：interleaved 5 floats = pos(3) + uv(2)。
struct InterleavedVertex
{
    float position[3];
    float uv[2];
};

std::vector<std::uint32_t> LoadSpirv(const char* path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        ORANGE_LOG_ERROR("Pipeline: 无法打开 SPIR-V {}", path);
        return {};
    }
    const std::streamsize size = file.tellg();
    if (size <= 0 || (size % 4) != 0)
    {
        ORANGE_LOG_ERROR("Pipeline: SPIR-V 大小非法 ({}) for {}",
                         static_cast<long long>(size), path);
        return {};
    }
    std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), size);
    return words;
}

}  // namespace

struct Pipeline::Impl
{
    RenderScene                            scene;
    Asset::AssetRegistry*                  assets{nullptr};

    bool initialized{false};

    std::unique_ptr<Orange::Renderer::RenderDevice> renderDevice;
    std::unique_ptr<Orange::Renderer::IRenderer>    renderer;

    std::vector<std::uint32_t>                    vertSpirv;
    std::vector<std::uint32_t>                    fragSpirv;
    std::unique_ptr<Orange::Rhi::RHIShaderModule> vertShader;
    std::unique_ptr<Orange::Rhi::RHIShaderModule> fragShader;
    std::unique_ptr<Orange::Rhi::RHIPipeline>     graphicsPipeline;

    std::unique_ptr<Orange::Resource::UploadContext> upload;

    struct MeshGpu
    {
        std::unique_ptr<Orange::Rhi::RHIBuffer> vertexBuffer;
        std::unique_ptr<Orange::Rhi::RHIBuffer> indexBuffer;
        std::uint32_t                           indexCount{0};
    };
    // key = AssetHandle<MeshAsset>::Value()
    std::unordered_map<std::uint64_t, MeshGpu>    meshCache;

    std::uint64_t frameIndex{0};
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
    // OrangeRender 的 RendererDesc::mpNativeWindowHandle 是 GLFWwindow*
    // （见 Renderer.h 注释）；用 Window 的 GLFW handle，而非 HWND。
    rendererDesc.mpNativeWindowHandle = window.GetGlfwWindowHandle();
    rendererDesc.mFramesInFlight      = 2;

    if (Orange::Failed(impl.renderer->Initialize(rendererDesc)))
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: Renderer::Initialize 失败");
        impl.renderer.reset();
        impl.renderDevice.reset();
        return ResultCode::InternalError;
    }

    auto& rhi = impl.renderDevice->GetRhiDevice();

    // 3. Shader modules --------------------------------------------------
    const auto exeDir   = GetExecutableDir();
    const auto vertPath = (exeDir / kBuiltinVertSpvRelative).string();
    const auto fragPath = (exeDir / kBuiltinFragSpvRelative).string();
    impl.vertSpirv = LoadSpirv(vertPath.c_str());
    impl.fragSpirv = LoadSpirv(fragPath.c_str());
    if (impl.vertSpirv.empty() || impl.fragSpirv.empty())
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: 加载内置 SPIR-V 失败 (exe dir={})",
                         exeDir.string());
        impl.renderer->Shutdown();
        impl.renderer.reset();
        impl.renderDevice.reset();
        return ResultCode::IoError;
    }

    Orange::Rhi::ShaderModuleDesc vsDesc{};
    vsDesc.mpCode      = impl.vertSpirv.data();
    vsDesc.mCodeSize   = impl.vertSpirv.size() * sizeof(std::uint32_t);
    vsDesc.mStage      = Orange::Rhi::ShaderStage::Vertex;
    vsDesc.mpDebugName = "orange_engine.textured_mesh.vert";
    impl.vertShader = rhi.CreateShaderModule(vsDesc);

    Orange::Rhi::ShaderModuleDesc fsDesc{};
    fsDesc.mpCode      = impl.fragSpirv.data();
    fsDesc.mCodeSize   = impl.fragSpirv.size() * sizeof(std::uint32_t);
    fsDesc.mStage      = Orange::Rhi::ShaderStage::Fragment;
    fsDesc.mpDebugName = "orange_engine.textured_mesh.frag";
    impl.fragShader = rhi.CreateShaderModule(fsDesc);

    if (!impl.vertShader || !impl.fragShader)
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: CreateShaderModule 失败");
        Shutdown();
        return ResultCode::InternalError;
    }

    // 4. Graphics pipeline ----------------------------------------------
    Orange::Rhi::GraphicsPipelineDesc pipelineDesc{};
    pipelineDesc.mShaderStages.push_back(
        {Orange::Rhi::ShaderStage::Vertex,   impl.vertShader.get(), "main"});
    pipelineDesc.mShaderStages.push_back(
        {Orange::Rhi::ShaderStage::Fragment, impl.fragShader.get(), "main"});

    Orange::Rhi::VertexBindingDesc binding{};
    binding.mBinding   = 0;
    binding.mStride    = sizeof(InterleavedVertex);
    binding.mInputRate = Orange::Rhi::VertexInputRate::Vertex;
    pipelineDesc.mVertexInput.mBindings.push_back(binding);

    Orange::Rhi::VertexAttributeDesc attrPos{};
    attrPos.mLocation = 0;
    attrPos.mBinding  = 0;
    attrPos.mOffset   = offsetof(InterleavedVertex, position);
    attrPos.mFormat   = Orange::Rhi::VertexFormat::Float32x3;
    pipelineDesc.mVertexInput.mAttributes.push_back(attrPos);

    Orange::Rhi::VertexAttributeDesc attrUV{};
    attrUV.mLocation = 1;
    attrUV.mBinding  = 0;
    attrUV.mOffset   = offsetof(InterleavedVertex, uv);
    attrUV.mFormat   = Orange::Rhi::VertexFormat::Float32x2;
    pipelineDesc.mVertexInput.mAttributes.push_back(attrUV);

    pipelineDesc.mInputAssembly.mTopology = Orange::Rhi::PrimitiveTopology::TriangleList;
    // 背面剔除 + Vulkan 默认 CCW front-face：与 OrangeRender procedural_scene
    // 同一约定（"front = CCW in framebuffer space"）。Camera 工厂的 projection
    // 内置 Y-flip，所以 sample / 内置 mesh 应按 "world-CW = NDC-CCW after Y-flip"
    // 编排索引（典型 quad 索引：(0, 2, 1, 0, 3, 2)）。3D 实体没背面剔除会有
    // 严重 overdraw；OrangeRender BeginFrame/SubmitItem 路径目前只挂 color
    // attachment，没 depth buffer，所以 cube 只靠背面剔除做显隐。
    pipelineDesc.mRasterizer.mCullMode    = Orange::Rhi::CullMode::Back;
    pipelineDesc.mRasterizer.mFrontFace   = Orange::Rhi::FrontFace::CounterClockwise;
    pipelineDesc.mDepthStencil.mDepthTestEnable  = false;
    pipelineDesc.mDepthStencil.mDepthWriteEnable = false;
    pipelineDesc.mColorBlend.mAttachments.push_back({});
    pipelineDesc.mRenderTargets.mColorFormats.push_back(Orange::Rhi::TextureFormat::BGRA8Unorm);

    Orange::Rhi::PushConstantRange pcRange{};
    pcRange.mStage  = Orange::Rhi::ShaderStage::Vertex;
    pcRange.mOffset = 0;
    pcRange.mSize   = sizeof(glm::mat4);
    pipelineDesc.mPushConstantRanges.push_back(pcRange);

    pipelineDesc.mpDebugName = "orange_engine.textured_mesh";

    impl.graphicsPipeline = rhi.CreateGraphicsPipeline(pipelineDesc);
    if (!impl.graphicsPipeline)
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: CreateGraphicsPipeline 失败");
        Shutdown();
        return ResultCode::InternalError;
    }

    // 5. UploadContext --------------------------------------------------
    impl.upload = std::make_unique<Orange::Resource::UploadContext>();
    if (Orange::Failed(impl.upload->Initialize(rhi)))
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: UploadContext::Initialize 失败");
        impl.upload.reset();
        Shutdown();
        return ResultCode::InternalError;
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

    impl.meshCache.clear();    // 释放 mesh GPU buffers

    if (impl.upload)
    {
        impl.upload->Shutdown();
        impl.upload.reset();
    }

    impl.graphicsPipeline.reset();
    impl.fragShader.reset();
    impl.vertShader.reset();
    impl.fragSpirv.clear();
    impl.vertSpirv.clear();

    if (impl.renderer)
    {
        impl.renderer->Shutdown();
    }
    impl.renderer.reset();
    impl.renderDevice.reset();

    impl.assets      = nullptr;
    impl.initialized = false;
}

namespace
{

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

    // 没相机 → 直接 EndFrame；让"World 还没人放 Camera"是受支持的
    // 退化状态（屏幕保持上一次 present 内容 / 空白）。
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
            // 首次见到本 mesh：从 AssetRegistry 取出 CPU 数据，上传
            // GPU 顶点 / 索引 buffer，缓存条目。
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

        // MVP = projection * view * world，按列向量惯例。
        const glm::mat4 mvp = viewProj * drawable.worldMatrix;

        Orange::Renderer::RenderItem item{};
        item.mpPipeline           = impl.graphicsPipeline.get();
        item.mpVertexBuffer       = gpu.vertexBuffer.get();
        item.mpIndexBuffer        = gpu.indexBuffer.get();
        item.mIndexFormat         = Orange::Rhi::IndexFormat::UInt32;
        item.mDraw.mIndexCount    = gpu.indexCount;
        item.mDraw.mInstanceCount = 1;
        item.mTransform           = drawable.worldMatrix;

        // Push constant：仅 vertex 阶段，64 字节 mat4。
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

}  // namespace Orange::Engine::Render
