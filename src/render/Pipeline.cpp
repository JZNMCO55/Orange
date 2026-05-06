// Pipeline 实现：把"World → RenderScene"的 ECS 侧产物接到 OrangeRender
// 的 BeginFrame / SubmitItem / EndFrame 帧生命周期。
//
// 当前阶段（Phase 2 / Task 07）的"绘制单个 mesh"采用最小可运行实
// 现：
//   * 内置 GraphicsPipeline + minimal_mesh.vert/.frag.spv（已通过
//     CMake 在 build 顶层用 glslangValidator 编出来）；
//   * 顶点 shader 用 gl_VertexIndex 写出一个三角形，**不读 vertex
//     buffer**——证明 Renderer / SwapChain / draw call 全链路通；
//   * Render(world) 仍然 Clear+Collect 跑一遍 RenderScene，把 mesh
//     upload 与按 drawable 驱动的 RenderItem 推到后续 task 接进来。
//
// 这是刻意为之的范围：把"接通 OrangeRender"和"按 ECS 数据驱动绘
// 制"拆成两步——前者本 task 落地，后者在 sample 真正需要可视化时
// （Task 09 textured_quad）一并接通，配合一次实跑视觉验证。

#include "orange/engine/render/Pipeline.h"

#include "orange/engine/core/Log.h"
#include "orange/engine/platform/Window.h"
#include "orange/engine/render/RenderScene.h"

#include "orange/renderer/RenderDevice.h"
#include "orange/renderer/Renderer.h"
#include "orange/renderer/RenderTypes.h"
#include "orange/rhi/RHI.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <vector>

namespace Orange::Engine::Render
{
namespace
{

// 把 .spv 文件按 32-bit word 流读进 vector。文件不存在 / 字节数非
// 4 的倍数都视为错误。
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
        ORANGE_LOG_ERROR("Pipeline: SPIR-V 大小非法 ({} 字节) for {}",
                         static_cast<long long>(size), path);
        return {};
    }
    std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), size);
    return words;
}

constexpr const char* kBuiltinVertSpv = "shaders/orange_engine/minimal_mesh.vert.spv";
constexpr const char* kBuiltinFragSpv = "shaders/orange_engine/minimal_mesh.frag.spv";

}  // namespace

struct Pipeline::Impl
{
    RenderScene scene;

    bool initialized{false};

    std::unique_ptr<Orange::Renderer::RenderDevice> renderDevice;
    std::unique_ptr<Orange::Renderer::IRenderer>    renderer;

    // SPIR-V 字节流 + RHI shader module；ShaderModuleDesc 持有指
    // 针，必须保活 SPIR-V 直到 pipeline 创建完成（实际更稳的做法
    // 是把它一直留到 Shutdown）。
    std::vector<std::uint32_t>                  vertSpirv;
    std::vector<std::uint32_t>                  fragSpirv;
    std::unique_ptr<Orange::Rhi::RHIShaderModule> vertShader;
    std::unique_ptr<Orange::Rhi::RHIShaderModule> fragShader;

    std::unique_ptr<Orange::Rhi::RHIPipeline> graphicsPipeline;

    std::uint64_t frameIndex{0};
    double        startTime{0.0};   // 用 std::chrono 也可以；这里保持简单
    double        lastTime{0.0};
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

Result<void, ResultCode> Pipeline::Initialize(Platform::Window& window)
{
    auto& impl = *mpImpl;
    if (impl.initialized)
    {
        return ResultCode::AlreadyInitialized;
    }

    // 1. RenderDevice ----------------------------------------------------
    Orange::Renderer::RenderDeviceDesc deviceDesc{};
    deviceDesc.mBackend          = Orange::Renderer::BackendType::Default;
    deviceDesc.mEnableValidation = true;  // dev 默认开 validation；release 时由 build flag 关
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

    // 注：mpNativeWindowHandle 需要 GLFWwindow*（Renderer 内部用
    // glfwCreateWindowSurface 之类的接口）。Platform::Window 提供的
    // GetNativeWindowHandle() 返回的是 HWND；OrangeRender 的当前
    // RendererDesc 接收 GLFWwindow*——等于说我们必须把 Window 的
    // GLFW handle 暴露出来。Window.h 当前刻意只对外露 HWND/void*。
    //
    // 暂时的折中：通过 Window 的 native HWND 走不通，所以这一版
    // Initialize 仍依赖于 RendererDesc 接受 HWND 这条路径——若
    // OrangeRender 不接受 HWND，Renderer::Initialize 会返回错误，
    // 我们把它向上传递。后续 OrangeRender 切到 native-handle-only
    // 接口时（Phase 3+）这里的耦合会自然解掉。
    Orange::Renderer::RendererDesc rendererDesc{};
    rendererDesc.mpDevice             = &impl.renderDevice->GetRhiDevice();
    rendererDesc.mpNativeWindowHandle = window.GetNativeWindowHandle();
    rendererDesc.mFramesInFlight      = 2;

    if (Orange::Failed(impl.renderer->Initialize(rendererDesc)))
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: Renderer::Initialize 失败");
        impl.renderer.reset();
        impl.renderDevice.reset();
        return ResultCode::InternalError;
    }

    // 3. Shader modules --------------------------------------------------
    impl.vertSpirv = LoadSpirv(kBuiltinVertSpv);
    impl.fragSpirv = LoadSpirv(kBuiltinFragSpv);
    if (impl.vertSpirv.empty() || impl.fragSpirv.empty())
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: 加载内置 SPIR-V 失败 (cwd 应能解析 \"{}\")",
                         kBuiltinVertSpv);
        impl.renderer->Shutdown();
        impl.renderer.reset();
        impl.renderDevice.reset();
        return ResultCode::IoError;
    }

    auto& rhi = impl.renderDevice->GetRhiDevice();

    Orange::Rhi::ShaderModuleDesc vsDesc{};
    vsDesc.mpCode      = impl.vertSpirv.data();
    vsDesc.mCodeSize   = impl.vertSpirv.size() * sizeof(std::uint32_t);
    vsDesc.mStage      = Orange::Rhi::ShaderStage::Vertex;
    vsDesc.mpDebugName = "orange_engine.minimal_mesh.vert";
    impl.vertShader = rhi.CreateShaderModule(vsDesc);

    Orange::Rhi::ShaderModuleDesc fsDesc{};
    fsDesc.mpCode      = impl.fragSpirv.data();
    fsDesc.mCodeSize   = impl.fragSpirv.size() * sizeof(std::uint32_t);
    fsDesc.mStage      = Orange::Rhi::ShaderStage::Fragment;
    fsDesc.mpDebugName = "orange_engine.minimal_mesh.frag";
    impl.fragShader = rhi.CreateShaderModule(fsDesc);

    if (!impl.vertShader || !impl.fragShader)
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: CreateShaderModule 失败");
        impl.fragShader.reset();
        impl.vertShader.reset();
        impl.renderer->Shutdown();
        impl.renderer.reset();
        impl.renderDevice.reset();
        return ResultCode::InternalError;
    }

    // 4. Graphics pipeline ----------------------------------------------
    Orange::Rhi::GraphicsPipelineDesc pipelineDesc{};
    pipelineDesc.mShaderStages.push_back(
        {Orange::Rhi::ShaderStage::Vertex,   impl.vertShader.get(), "main"});
    pipelineDesc.mShaderStages.push_back(
        {Orange::Rhi::ShaderStage::Fragment, impl.fragShader.get(), "main"});
    pipelineDesc.mInputAssembly.mTopology = Orange::Rhi::PrimitiveTopology::TriangleList;
    pipelineDesc.mRasterizer.mCullMode    = Orange::Rhi::CullMode::None;
    pipelineDesc.mDepthStencil.mDepthTestEnable  = false;
    pipelineDesc.mDepthStencil.mDepthWriteEnable = false;
    pipelineDesc.mColorBlend.mAttachments.push_back({});
    // 必须与 swap-chain 实际选定的 color format 对齐——OrangeRender 的
    // VulkanSwapchain 当前固定 BGRA8Unorm。
    pipelineDesc.mRenderTargets.mColorFormats.push_back(Orange::Rhi::TextureFormat::BGRA8Unorm);
    pipelineDesc.mpDebugName = "orange_engine.minimal_mesh";

    impl.graphicsPipeline = rhi.CreateGraphicsPipeline(pipelineDesc);
    if (!impl.graphicsPipeline)
    {
        ORANGE_LOG_ERROR("Pipeline::Initialize: CreateGraphicsPipeline 失败");
        impl.fragShader.reset();
        impl.vertShader.reset();
        impl.renderer->Shutdown();
        impl.renderer.reset();
        impl.renderDevice.reset();
        return ResultCode::InternalError;
    }

    impl.startTime   = 0.0;
    impl.lastTime    = 0.0;
    impl.frameIndex  = 0;
    impl.initialized = true;
    return Result<void, ResultCode>{};
}

void Pipeline::Shutdown()
{
    if (!mpImpl || !mpImpl->initialized)
    {
        return;
    }
    auto& impl = *mpImpl;

    if (impl.renderDevice)
    {
        impl.renderDevice->WaitIdle();
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

    impl.initialized = false;
}

void Pipeline::Render(Orange::Engine::World& world)
{
    auto& impl = *mpImpl;

    // 把 ECS 翻成 RenderScene——本步骤不依赖 Initialize 是否成功，
    // 让 RenderScene 收集行为可被独立测试。
    impl.scene.Clear();
    impl.scene.Collect(world);

    if (!impl.initialized)
    {
        return;
    }

    // 帧时间：暂时用 frame index 派生粗粒度时间值（OrangeRender 的
    // FrameTimeInfo 会用在 motion blur / temporal AA 等后处理；本
    // task 不进任何后处理，给一个稳定递增的近似值即可）。
    constexpr double kAssumedDt = 1.0 / 60.0;
    Orange::Renderer::FrameTimeInfo timeInfo{};
    timeInfo.mTotalTimeSeconds = impl.frameIndex * kAssumedDt;
    timeInfo.mDeltaTimeSeconds = static_cast<float>(kAssumedDt);

    if (Orange::Failed(impl.renderer->BeginFrame(timeInfo)))
    {
        ORANGE_LOG_ERROR("Pipeline::Render: BeginFrame 失败 (frame={})", impl.frameIndex);
        return;
    }

    // 当前 task 提交一个内置硬编码 triangle —— gl_VertexIndex 走 3
    // 个固定顶点。后续 task 把 RenderScene::Drawables() 的 mesh 数
    // 据真正接进来时，会在这里循环一遍 SubmitItem。
    Orange::Renderer::RenderItem item{};
    item.mpPipeline           = impl.graphicsPipeline.get();
    item.mDraw.mVertexCount   = 3;
    item.mDraw.mInstanceCount = 1;
    impl.renderer->SubmitItem(item);

    if (Orange::Failed(impl.renderer->EndFrame()))
    {
        ORANGE_LOG_ERROR("Pipeline::Render: EndFrame 失败 (frame={})", impl.frameIndex);
        return;
    }

    ++impl.frameIndex;
}

}  // namespace Orange::Engine::Render
