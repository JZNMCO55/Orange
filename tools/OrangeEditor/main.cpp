// OrangeEditor —— Phase 6 / Task 06-02：ImGui dock space + multi-viewport。
//
// 架构选择（Task 06-02 范围内）：
//   * AppHost / LayerStack —— 来自 OrangeEngine，提供窗口 + 事件分发 +
//     主循环
//   * RenderDevice + IRenderer —— 编辑器自管，**不**用 engine Pipeline。
//     原因：编辑器当前只渲染 ImGui，不渲染 3D scene；engine Pipeline 是
//     给 game 渲染场景用的，强行套上反而要做"自定义 RenderPass 注入 +
//     overlay 回调"两层中转。等 Task 06-04（组件检视器需要 scene viewport
//     预览）时再桥接 Pipeline → off-screen RT → ImGui::Image
//   * ImGui Vulkan / GLFW backend —— vendored via FetchContent (docking
//     branch v1.91.5)；通过 OrangeRender 新交付的 Interop opt-in 头
//     (FEATURE-2026-05-09-vulkan-interop-handles) 拿 raw Vulkan handle 喂
//     ImGui_ImplVulkan_InitInfo
//   * 多视口（窗口可拖拽悬停成独立 native window）—— 启用 ImGuiConfigFlags
//     _DockingEnable + ViewportsEnable；ImGui 自带的 multi-viewport
//     platform / renderer interface 接管额外 viewport 的窗口 / swapchain
//     创建 + 渲染
//
// 当前 UI 内容：dock space + ImGui demo window + 一个"about OrangeEditor"
// 小窗口。Task 06-03 起填实体树 / 检视器 / 资源浏览器 / 控制台。

#include <orange/engine/app/AppConfig.h>
#include <orange/engine/app/AppHost.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/app/Layer.h>
#include <orange/engine/platform/Window.h>
#include <orange/engine/platform/WindowEvent.h>

#include <orange/renderer/RenderDevice.h>
#include <orange/renderer/Renderer.h>
#include <orange/renderer/VulkanInterop.h>
#include <orange/rhi/RHICommandList.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <vulkan/vulkan.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <variant>

namespace
{

constexpr std::int32_t kEscapeKeyRaw = 256;  // GLFW_KEY_ESCAPE，与 Input::KeyCode::Escape 同值

// ImGui 把 ImGui_ImplVulkan_LoaderFunc 用作 vkGet*Addr 的解析入口；不喂
// 它就 panic。我们没用 volk，直接走 vulkan-1.lib 静态符号。提供一个
// 简单 loader 转发到 vkGetInstanceProcAddr。
PFN_vkVoidFunction ImguiVulkanLoader(const char* funcName, void* userData)
{
    auto* instance = reinterpret_cast<VkInstance>(userData);
    return vkGetInstanceProcAddr(instance, funcName);
}

// 编辑器侧需要在每帧 BeginFrame 之前调 ImGui::NewFrame 等。把这件事
// 封到一个 layer，让 AppHost 主循环按 LayerStack 的 OnUpdate 顺序自动
// 触发。Render layer 在最后 push，确保 ImGui::NewFrame → user UI →
// ImGui::Render 在 BeginFrame 之前完成；BeginFrame / EndFrame 内 overlay
// callback 拿 ImGui 的 DrawData 录制。
class EditorRenderLayer : public Orange::Engine::Layer
{
public:
    EditorRenderLayer(Orange::Engine::AppHost&             host,
                      Orange::Renderer::IRenderer&         renderer,
                      VkDescriptorPool                     descriptorPool,
                      VkDevice                             device)
        : Orange::Engine::Layer("EditorRender")
        , mHost(host)
        , mRenderer(renderer)
        , mDescriptorPool(descriptorPool)
        , mDevice(device)
    {
        // 注册 swap-chain overlay callback —— 引擎 EndFrame 内 swap-chain
        // 渲染窗口里调一次 ImGui_ImplVulkan_RenderDrawData，把当前帧 ImGui
        // DrawData 录到主窗口 swap-chain image。
        mRenderer.SetSwapchainOverlayCallback(
            [](Orange::Rhi::RHICommandList& cmd,
               std::uint32_t /*w*/, std::uint32_t /*h*/, std::uint64_t /*frameIndex*/)
            {
                void* rawCmd = Orange::Renderer::Interop::GetVulkanCommandBuffer(cmd);
                if (rawCmd != nullptr) {
                    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),
                                                    static_cast<VkCommandBuffer>(rawCmd));
                }
            });
    }

    ~EditorRenderLayer() override
    {
        // 清 callback 避免捕获已销毁资源
        mRenderer.SetSwapchainOverlayCallback({});
    }

    void OnUpdate(const Orange::Engine::FrameContext& frame) override
    {
        // ---- ImGui 帧开始 ---------------------------------------------
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // dock space —— 让所有 imgui window 可以拖到主窗口里组成 dock 布局
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        // demo + about 占位窗口（Task 06-03 起替换为实体树 / 检视器 / etc.）
        ImGui::ShowDemoWindow();

        ImGui::Begin("About OrangeEditor");
        ImGui::Text("OrangeEditor v0.0.2 (Task 06-02 scaffold)");
        ImGui::Separator();
        ImGui::Text("frame index: %llu",
                    static_cast<unsigned long long>(frame.time.frameIndex));
        ImGui::Text("delta: %.3f ms", frame.time.deltaSeconds * 1000.0);
        ImGui::Separator();
        ImGui::TextWrapped(
            "Drag any window's title bar OUT of this main window to detach it "
            "into a floating native OS window (multi-viewport).");
        ImGui::Separator();
        if (ImGui::Button("Quit (or press Esc)")) {
            mHost.RequestExit();
        }
        ImGui::End();

        ImGui::Render();

        // ---- engine frame：BeginFrame → (overlay callback fires
        //      ImGui_ImplVulkan_RenderDrawData) → EndFrame ----------------
        Orange::Renderer::FrameTimeInfo time{};
        time.mTotalTimeSeconds = frame.time.totalSeconds;
        time.mDeltaTimeSeconds = static_cast<float>(frame.time.deltaSeconds);
        if (Orange::Failed(mRenderer.BeginFrame(time))) {
            std::fprintf(stderr, "[OrangeEditor] BeginFrame failed\n");
            mHost.RequestExit();
            return;
        }
        // 编辑器不渲染任何 SubmitItem 内容 —— 仅靠 overlay callback 内的
        // ImGui draw data。FrameLifecycle 在 hasDraw=false + overlay 已
        // 注册时会强制走 begin/end rendering 路径（FEATURE-2026-05-09 修
        // 复的 overlay-on-empty-frame bug），callback 仍能正常触发。
        if (Orange::Failed(mRenderer.EndFrame())) {
            std::fprintf(stderr, "[OrangeEditor] EndFrame failed\n");
            mHost.RequestExit();
            return;
        }

        // ---- multi-viewport：让 ImGui 渲染所有"已拖出主窗口"的额外
        //      viewport 到它们各自的 native window 上 -------------------
        ImGuiIO& io = ImGui::GetIO();
        if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

    bool OnEvent(const Orange::Engine::Platform::WindowEvent& event) override
    {
        // ImGui_ImplGlfw_InitForVulkan(true) 时 install_callbacks=true，
        // ImGui 会自己装 GLFW 回调拿到所有事件 —— 我们这里**不**再转发
        // KeyEvent，避免双触发。仅手动处理 Esc 退出（如果按下时焦点没
        // 在 ImGui 任何 widget 上）。
        const auto* key = std::get_if<Orange::Engine::Platform::KeyEvent>(&event);
        if (key == nullptr) { return false; }
        if (key->action != Orange::Engine::Platform::KeyAction::Press) { return false; }
        if (key->key != kEscapeKeyRaw) { return false; }
        if (ImGui::GetIO().WantCaptureKeyboard) { return false; }
        std::fprintf(stdout, "[OrangeEditor] Esc 按下，请求退出\n");
        mHost.RequestExit();
        return true;
    }

private:
    Orange::Engine::AppHost&      mHost;
    Orange::Renderer::IRenderer&  mRenderer;
    VkDescriptorPool              mDescriptorPool;  // owned by main, not by layer
    VkDevice                      mDevice;
};

VkDescriptorPool MakeImguiDescriptorPool(VkInstance instance, VkDevice device)
{
    // 通过 loader 解析 vkCreateDescriptorPool —— 编辑器 exe 不直接 link
    // vulkan-1.lib（避免与 OrangeRender 内部 volk loader 路径冲突），
    // 也就拿不到静态 dispatch stub；走 vkGetDeviceProcAddr 拿 device-
    // specific function pointer 是最干净的路径。
    auto vkGetDeviceProcAddrFn =
        reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            vkGetInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
    if (vkGetDeviceProcAddrFn == nullptr) {
        std::fprintf(stderr, "[OrangeEditor] vkGetInstanceProcAddr(vkGetDeviceProcAddr) failed\n");
        return VK_NULL_HANDLE;
    }
    auto vkCreateDescriptorPoolFn =
        reinterpret_cast<PFN_vkCreateDescriptorPool>(
            vkGetDeviceProcAddrFn(device, "vkCreateDescriptorPool"));
    if (vkCreateDescriptorPoolFn == nullptr) {
        std::fprintf(stderr, "[OrangeEditor] vkGetDeviceProcAddr(vkCreateDescriptorPool) failed\n");
        return VK_NULL_HANDLE;
    }

    constexpr VkDescriptorPoolSize sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
    };
    VkDescriptorPoolCreateInfo desc{};
    desc.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    desc.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    desc.maxSets       = 1000;
    desc.poolSizeCount = sizeof(sizes) / sizeof(sizes[0]);
    desc.pPoolSizes    = sizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPoolFn(device, &desc, nullptr, &pool) != VK_SUCCESS) {
        std::fprintf(stderr, "[OrangeEditor] vkCreateDescriptorPool failed\n");
    }
    return pool;
}

// 与 MakeImguiDescriptorPool 同模式 —— 走 loader 拿 vkDestroyDescriptorPool
// fn ptr 用于关停期清理。
void DestroyImguiDescriptorPool(VkInstance instance, VkDevice device, VkDescriptorPool pool)
{
    if (pool == VK_NULL_HANDLE) { return; }
    auto vkGetDeviceProcAddrFn =
        reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            vkGetInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
    if (vkGetDeviceProcAddrFn == nullptr) { return; }
    auto vkDestroyDescriptorPoolFn =
        reinterpret_cast<PFN_vkDestroyDescriptorPool>(
            vkGetDeviceProcAddrFn(device, "vkDestroyDescriptorPool"));
    if (vkDestroyDescriptorPoolFn != nullptr) {
        vkDestroyDescriptorPoolFn(device, pool, nullptr);
    }
}

}  // namespace

int main()
{
    using namespace Orange::Engine;

    // ---- AppHost（窗口 + 主循环）---------------------------------------
    AppConfig cfg{};
    cfg.window.title  = "OrangeEditor v0.0.2 (Task 06-02 ImGui dock + multi-viewport)";
    cfg.window.width  = 1600;
    cfg.window.height = 900;
    auto hostRes = AppHost::Create(cfg);
    if (hostRes.IsErr()) {
        std::fprintf(stderr, "[OrangeEditor] AppHost::Create failed (code=%u)\n",
                     static_cast<unsigned>(hostRes.Error()));
        return 1;
    }
    auto host = std::move(hostRes).Value();
    auto* glfwWindow = static_cast<GLFWwindow*>(host->GetWindow().GetGlfwWindowHandle());

    // ---- 编辑器自管 RenderDevice + IRenderer ---------------------------
    Orange::Renderer::RenderDeviceDesc rdDesc{};
    rdDesc.mBackend          = Orange::Renderer::BackendType::Default;
    rdDesc.mEnableValidation = true;
    auto pRenderDevice = Orange::Renderer::RenderDevice::Create(rdDesc);
    if (pRenderDevice == nullptr) {
        std::fprintf(stderr, "[OrangeEditor] RenderDevice::Create failed\n");
        return 1;
    }

    auto pRenderer = Orange::Renderer::CreateRenderer();
    Orange::Renderer::RendererDesc rendererDesc{};
    rendererDesc.mpDevice             = &pRenderDevice->GetRhiDevice();
    rendererDesc.mpNativeWindowHandle = glfwWindow;
    rendererDesc.mFramesInFlight      = 2;
    if (Orange::Failed(pRenderer->Initialize(rendererDesc))) {
        std::fprintf(stderr, "[OrangeEditor] Renderer::Initialize failed\n");
        return 1;
    }

    // ---- 取 OrangeRender 透出的 Vulkan handle（FEATURE-2026-05-09）-----
    const auto handles = Orange::Renderer::Interop::GetVulkanDeviceHandles(*pRenderDevice);
    // 跑一帧让 swap-chain ready，再取 swap-chain info（min/count + format）
    {
        Orange::Renderer::FrameTimeInfo dummy{};
        dummy.mTotalTimeSeconds = 0.0;
        dummy.mDeltaTimeSeconds = 0.0f;
        if (Orange::Failed(pRenderer->BeginFrame(dummy))) {
            std::fprintf(stderr, "[OrangeEditor] dummy BeginFrame failed\n");
            return 1;
        }
        if (Orange::Failed(pRenderer->EndFrame())) {
            std::fprintf(stderr, "[OrangeEditor] dummy EndFrame failed\n");
            return 1;
        }
    }
    const auto sci = Orange::Renderer::Interop::GetVulkanSwapchainInfo(*pRenderer);
    std::fprintf(stdout,
                 "[OrangeEditor] Vulkan handles: instance=%p device=%p qFamily=%u\n"
                 "[OrangeEditor] swap-chain: min=%u count=%u format=%d %ux%u\n",
                 handles.vkInstance, handles.vkDevice, handles.graphicsQueueFamilyIndex,
                 sci.minImageCount, sci.imageCount, sci.colorFormat, sci.imageWidth, sci.imageHeight);

    auto vkInstance       = static_cast<VkInstance>(handles.vkInstance);
    auto vkPhysicalDevice = static_cast<VkPhysicalDevice>(handles.vkPhysicalDevice);
    auto vkDevice         = static_cast<VkDevice>(handles.vkDevice);
    auto vkQueue          = static_cast<VkQueue>(handles.vkGraphicsQueue);
    auto colorFmt         = static_cast<VkFormat>(sci.colorFormat);

    // ---- ImGui Init -----------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui::StyleColorsDark();
    // 多视口模式下让"detached" window 看起来跟主窗口风格一致
    if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // GLFW backend —— install_callbacks=true 让 ImGui 自动装 GLFW key /
    // mouse / focus 回调；与 AppHost 共享同一 window，事件分发上 ImGui
    // 拦在 AppHost 之前（GLFW 回调链顺序）
    if (!ImGui_ImplGlfw_InitForVulkan(glfwWindow, true)) {
        std::fprintf(stderr, "[OrangeEditor] ImGui_ImplGlfw_InitForVulkan failed\n");
        return 1;
    }

    // ImGui Vulkan backend —— v1.91.5-docking 的 LoadFunctions 签名是
    // (loader_fn, user_data)，不带 api_version 参数。
    ImGui_ImplVulkan_LoadFunctions(&ImguiVulkanLoader, vkInstance);

    VkDescriptorPool imguiDescPool = MakeImguiDescriptorPool(vkInstance, vkDevice);
    if (imguiDescPool == VK_NULL_HANDLE) { return 1; }

    ImGui_ImplVulkan_InitInfo vkInfo{};
    vkInfo.Instance        = vkInstance;
    vkInfo.PhysicalDevice  = vkPhysicalDevice;
    vkInfo.Device          = vkDevice;
    vkInfo.QueueFamily     = handles.graphicsQueueFamilyIndex;
    vkInfo.Queue           = vkQueue;
    vkInfo.DescriptorPool  = imguiDescPool;
    vkInfo.MinImageCount   = sci.minImageCount;
    vkInfo.ImageCount      = sci.imageCount;
    vkInfo.MSAASamples     = VK_SAMPLE_COUNT_1_BIT;
    vkInfo.UseDynamicRendering = true;
    vkInfo.PipelineRenderingCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    vkInfo.PipelineRenderingCreateInfo.colorAttachmentCount    = 1;
    vkInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFmt;
    if (!ImGui_ImplVulkan_Init(&vkInfo)) {
        std::fprintf(stderr, "[OrangeEditor] ImGui_ImplVulkan_Init failed\n");
        DestroyImguiDescriptorPool(vkInstance, vkDevice, imguiDescPool);
        return 1;
    }

    // ---- Layer 注入 -----------------------------------------------------
    host->PushLayer(std::make_unique<EditorRenderLayer>(*host, *pRenderer,
                                                        imguiDescPool, vkDevice));

    std::fprintf(stdout, "[OrangeEditor] ImGui dock + multi-viewport ready. Esc 退出。\n");

    const int rc = host->Run();

    // ---- 关停（顺序：等 GPU idle → 清 callback → ImGui shutdown → renderer
    //      shutdown → render device → host）
    pRenderDevice->WaitIdle();
    // overlay callback 在 EditorRenderLayer 析构时清除（dtor）
    host.reset();   // → AppHost dtor → LayerStack dtor → EditorRenderLayer dtor
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    DestroyImguiDescriptorPool(vkInstance, vkDevice, imguiDescPool);
    pRenderer->Shutdown();
    pRenderer.reset();
    pRenderDevice.reset();

    std::fprintf(stdout, "[OrangeEditor] clean shutdown.\n");
    return rc;
}
