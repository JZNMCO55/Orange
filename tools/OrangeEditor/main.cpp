// OrangeEditor —— ImGui dock space + multi-viewport 起步骨架。
//
// 架构选择：
//   * AppHost / LayerStack —— 来自 OrangeEngine，提供窗口 + 事件分发 +
//     主循环
//   * RenderDevice + IRenderer —— 编辑器自管，**不**用 engine Pipeline。
//     原因：编辑器当前只渲染 ImGui，不渲染 3D scene；engine Pipeline 是
//     给 game 渲染场景用的，强行套上反而要做"自定义 RenderPass 注入 +
//     overlay 回调"两层中转。后续若组件检视器需要 scene viewport 预览，
//     再桥接 Pipeline → off-screen RT → ImGui::Image
//   * ImGui Vulkan / GLFW backend —— vendored via FetchContent (docking
//     branch v1.91.5)
//   * Vulkan loader 路径统一 —— ImGui 静态库以 IMGUI_IMPL_VULKAN_NO_PROTOTYPES
//     编译，启动期通过 `Interop::GetVulkanGetInstanceProcAddr()` 取
//     OrangeRender 内 volk 已加载的 loader fn 喂给 ImGui_ImplVulkan_LoadFunctions；
//     编辑器自身需要的 vk* 解析（descriptor pool 创建 / 销毁）也走这条
//     loader。原因详见 OrangeRender `docs/api_guide.md §6.8.1` 与
//     FEATURE-2026-05-10-vulkan-loader-export 的 CHANGELOG 条目。所有
//     Vulkan handle 仍由 `Interop::GetVulkanDeviceHandles` /
//     `Interop::GetVulkanSwapchainInfo` 提供
//   * 多视口（窗口可拖拽悬停成独立 native window）—— 启用 ImGuiConfigFlags
//     _DockingEnable + ViewportsEnable；ImGui 自带的 multi-viewport
//     platform / renderer interface 接管额外 viewport 的窗口 / swapchain
//     创建 + 渲染
//
// 当前 UI 内容：dock space 上五个固定占位面板 —— Scene / Entity Tree /
// Inspector / Assets / Console。前四个仅 TextDisabled 占位，由 Task 06-03
// 起逐个填实；Console 当前放帧统计 + Esc 退出按钮，等接 Core::Log 时换
// 成日志流。默认 dock 布局首帧通过 DockBuilder* 编程式建立，之后用户调
// 整由 imgui.ini 持久化。

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
#include <imgui_internal.h>  // DockBuilder* API（仅在编辑器侧首帧建默认布局用）
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <variant>

namespace
{

constexpr std::int32_t kEscapeKeyRaw = 256;  // GLFW_KEY_ESCAPE，与 Input::KeyCode::Escape 同值

// 编辑器默认 UI 字体 size（像素）。ImGui 内嵌 ProggyClean 默认 13px，在
// 1080p+ 屏上对编辑器使用偏小；这里拉到 18px。后续要做的扩展点：把这个
// 数值抽到一个"编辑器 Settings"面板里让用户运行时调整 —— 改后重建
// io.Fonts atlas 并触发 ImGui_ImplVulkan_CreateFontsTexture 重传到 GPU。
// 现在硬编码即可，后续 task 真做 Settings 面板时再抽。
constexpr float kDefaultFontSizePx = 28.0f;

// ImGui 在 NO_PROTOTYPES 编译下不再 extern 引用 vulkan-1.lib 的静态 vkXxx
// 符号，启动期通过 `ImGui_ImplVulkan_LoadFunctions(loader, userData)` 让
// loader 把它内部需要的 ~30 个 vk 函数指针逐个 resolve 出来。loader 必须
// 拿"真 VkInstance"才能解析 instance/device 级函数（passing NULL 仅对
// 4 个 global 函数有保证）。打包 (pfn, instance) 为 user_data。
//
// pfn 取自 `Interop::GetVulkanGetInstanceProcAddr()`——OrangeRender 内
// volk 已加载的 loader entry；VkInstance 取自 `Interop::GetVulkanDeviceHandles`。
// 这样 ImGui 与 OrangeRender 共用同一个 loader 解析路径，避免两条独立路径
// 让 instance dispatch 状态错位。
struct ImguiVulkanLoaderCtx
{
    PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr   pfnGetDeviceProcAddr;  // 二级回退用，可为 null
    VkInstance                vkInstance;
    VkDevice                  vkDevice;              // 二级回退用，可为 VK_NULL_HANDLE
};

PFN_vkVoidFunction ImguiVulkanLoader(const char* funcName, void* userData)
{
    const auto* ctx = static_cast<const ImguiVulkanLoaderCtx*>(userData);

    // ImGui docking v1.91.5 的 imgui_impl_vulkan.cpp（line ~1100）硬编码用
    // KHR 后缀名解析 dynamic rendering 两个命令：
    //     ImGuiImplVulkanFuncs_vkCmdBeginRenderingKHR =
    //         loader_func("vkCmdBeginRenderingKHR", user_data);
    //     ImGuiImplVulkanFuncs_vkCmdEndRenderingKHR =
    //         loader_func("vkCmdEndRenderingKHR", user_data);
    //
    // 这里我们**必须**拦截这两个名字并改用 vkGetDeviceProcAddr 解析到 core
    // 名字（无 KHR 后缀），原因是：
    //
    // [trampoline 陷阱] 1.3 SDK 的 Vulkan loader 在 `vkGetInstanceProcAddr(
    // inst, "vkCmdBeginRenderingKHR")` 上**总是**返回一个非 null 的 loader
    // trampoline —— 不管 device 有没有 enable VK_KHR_dynamic_rendering 扩展。
    // 调用时 trampoline 才去查 device dispatch 表里 KHR 槽位；OrangeRender
    // 启用的是 Vulkan 1.3 core 的 `dynamicRendering` feature，不是 KHR 扩
    // 展，KHR 槽位在 dispatch 表里是 null —— trampoline 一旦被调用就跳
    // 0x0000_0000_0000_0000 访问冲突。
    //
    // 实测现象：watch 窗口里 ImGuiImplVulkanFuncs_vkCmdBeginRenderingKHR
    // 显示 vulkan-1.dll!0x...3790（非 null，是 loader trampoline），但抛
    // 异常 0xC0000005 在 0x0000_0000_0000_0000 —— 进了 trampoline、查不到
    // dispatch、空跳。
    //
    // 主视口看不出问题：overlay callback 在 OrangeRender 外层 begin/end
    // rendering scope 里调 RenderDrawData，ImGui 不会自己 call KHR 入口；
    // OrangeRender 自己的 vkCmdBeginRendering 走的是 volk → vkGetDeviceProcAddr
    // 拿到的驱动直接函数指针，绕开 loader trampoline 那层。
    //
    // 出路就是这里 —— 解析 KHR 名时改用 vkGetDeviceProcAddr 拿 core 名字。
    // vkGetDeviceProcAddr 直接落到驱动 ICD，没有 loader trampoline 这层，
    // dispatch 不依赖扩展启用状态、只看 feature；core `dynamicRendering`
    // feature 已 enable，驱动会返回有效函数指针。Vulkan 1.3 promote 这两
    // 个 KHR 命令时是纯名字 promotion、签名完全一致，强制 cast 安全。
    //
    // 注意：仅对这两个**被 promote 的**命令做替换。其它 KHR 命令（如
    // vkAcquireNextImageKHR、vkCreateSwapchainKHR）是真扩展、未被 promote，
    // 不能做同样替换。
    if (ctx->pfnGetDeviceProcAddr != nullptr && ctx->vkDevice != VK_NULL_HANDLE) {
        const char* coreName = nullptr;
        if (std::strcmp(funcName, "vkCmdBeginRenderingKHR") == 0) {
            coreName = "vkCmdBeginRendering";
        } else if (std::strcmp(funcName, "vkCmdEndRenderingKHR") == 0) {
            coreName = "vkCmdEndRendering";
        }
        if (coreName != nullptr) {
            PFN_vkVoidFunction core =
                ctx->pfnGetDeviceProcAddr(ctx->vkDevice, coreName);
            if (core != nullptr) { return core; }
            // 兜底：万一驱动只导出 KHR 名字（极不常见），最后再回 instance
            // proc addr 试一次。
        }
    }

    return ctx->pfnGetInstanceProcAddr(ctx->vkInstance, funcName);
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

        // dock space —— 占满主 viewport，所有 imgui window 都可以 dock 进来。
        // DockSpaceOverViewport 返回的 ID 在主 viewport 生命周期内稳定，下面
        // DockBuilder 系列 API 用它建默认布局。
        const ImGuiID dockspaceId =
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        BuildDefaultLayoutOnce(dockspaceId);

        // 五个固定面板（Task 06-02 占位骨架）：
        //   Scene        —— 场景视口预览（Task 06-04 真要画 viewport 时填）
        //   Entity Tree  —— ECS World 实体树（Task 06-03 填）
        //   Inspector    —— 组件检视器（Task 06-04 填）
        //   Assets       —— 资源浏览器（Phase 6 后续填）
        //   Console      —— 编辑器日志 / 帧统计（本 task 已能放调试信息）
        DrawScenePanel();
        DrawEntityTreePanel();
        DrawInspectorPanel();
        DrawAssetsPanel();
        DrawConsolePanel(frame);

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
        // ImGui 会自己装 GLFW 回调拿到所有事件 —— 这里**不**再转发
        // KeyEvent，避免双触发。仅把 Esc 作为编辑器的全局退出快捷键拦
        // 截。不检查 WantCaptureKeyboard：NavEnableKeyboard 开启后 ImGui
        // 几乎永远占着键盘焦点（demo / dock 任一可导航 widget 在就会拿
        // WantCaptureKeyboard=true），那样 Esc 永远到不了这里。Esc=quit
        // 是 scaffold 选定的开发期约定，与 ImGui 的常规键盘交互不会冲突。
        const auto* key = std::get_if<Orange::Engine::Platform::KeyEvent>(&event);
        if (key == nullptr) { return false; }
        if (key->action != Orange::Engine::Platform::KeyAction::Press) { return false; }
        if (key->key != kEscapeKeyRaw) { return false; }
        std::fprintf(stdout, "[OrangeEditor] Esc 按下，请求退出\n");
        mHost.RequestExit();
        return true;
    }

private:
    // 首帧（或 imgui.ini 还没存过布局时）建默认 dock 布局。判定条件用
    // DockBuilderGetNode → 子节点为空，这样能兼容两种场景：
    //   * 首次启动 / 删了 imgui.ini —— 节点存在但无子，建布局；
    //   * 已有保存的布局 —— 节点有子，跳过、尊重用户调整。
    // 注意 DockBuilder* 来自 imgui_internal.h，是 ImGui 公开但内部稳定度
    // 比 imgui.h 略低的 API；编辑器侧使用是 ImGui 官方推荐路径。
    static void BuildDefaultLayoutOnce(ImGuiID dockspaceId)
    {
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
        if (node != nullptr && node->IsSplitNode()) { return; }

        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId,
                                  ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId,
                                      ImGui::GetMainViewport()->Size);

        // 布局：左 20% Entity Tree；右 25% Inspector；下 30% Assets/Console
        // tab；剩余中央留给 Scene。比例与 Unity / Unreal 默认 layout 接近，
        // 后续可让用户调；ImGui 会把改动写回 imgui.ini，下次启动恢复。
        ImGuiID center = dockspaceId;
        ImGuiID left   = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left,
                                                    0.20f, nullptr, &center);
        ImGuiID right  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right,
                                                    0.25f, nullptr, &center);
        ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down,
                                                    0.30f, nullptr, &center);

        ImGui::DockBuilderDockWindow("Entity Tree", left);
        ImGui::DockBuilderDockWindow("Inspector",   right);
        ImGui::DockBuilderDockWindow("Assets",      bottom);
        ImGui::DockBuilderDockWindow("Console",     bottom);  // 同节点 = tab
        ImGui::DockBuilderDockWindow("Scene",       center);

        ImGui::DockBuilderFinish(dockspaceId);
    }

    // 占位面板：仅一行 placeholder 文案。每个面板的实际内容由后续 task 填
    // —— Entity Tree 由 06-03、Inspector 由 06-04、Scene viewport 由 06-04、
    // Assets 由 Phase 6 后续 task。这里只保证默认 dock 布局里这些名字真的
    // 存在，dock layout 才能建得起来。
    static void DrawScenePanel()
    {
        ImGui::Begin("Scene");
        ImGui::TextDisabled("scene viewport — Task 06-04");
        ImGui::End();
    }

    static void DrawEntityTreePanel()
    {
        ImGui::Begin("Entity Tree");
        ImGui::TextDisabled("ECS world entity tree — Task 06-03");
        ImGui::End();
    }

    static void DrawInspectorPanel()
    {
        ImGui::Begin("Inspector");
        ImGui::TextDisabled("component inspector — Task 06-04");
        ImGui::End();
    }

    static void DrawAssetsPanel()
    {
        ImGui::Begin("Assets");
        ImGui::TextDisabled("asset browser — Phase 6 后续");
        ImGui::End();
    }

    // Console 面板放调试信息：帧 index、deltaTime、Esc 退出按钮、Vulkan
    // multi-viewport 提示。Task 06-02 阶段编辑器没有日志系统，先把这些
    // 当作 "console" 的内容，等真接 Core::Log 时换成日志流。
    void DrawConsolePanel(const Orange::Engine::FrameContext& frame)
    {
        ImGui::Begin("Console");
        ImGui::Text("OrangeEditor v0.0.3 (Task 06-02)");
        ImGui::Separator();
        ImGui::Text("frame index: %llu",
                    static_cast<unsigned long long>(frame.time.frameIndex));
        ImGui::Text("delta: %.3f ms",
                    frame.time.deltaSeconds * 1000.0);
        ImGui::Separator();
        ImGui::TextWrapped(
            "Drag any panel's tab OUT of the main window to detach it as a "
            "floating native OS window (ImGui multi-viewport).");
        ImGui::Separator();
        if (ImGui::Button("Quit (or press Esc)")) {
            mHost.RequestExit();
        }
        ImGui::End();
    }

    Orange::Engine::AppHost&      mHost;
    Orange::Renderer::IRenderer&  mRenderer;
    VkDescriptorPool              mDescriptorPool;  // owned by main, not by layer
    VkDevice                      mDevice;
};

// 创建 ImGui Vulkan backend 用的 descriptor pool。
//
// 通过 OrangeRender 暴露的 loader fn (`Interop::GetVulkanGetInstanceProcAddr`)
// 级联解 `vkGetDeviceProcAddr` → `vkCreateDescriptorPool`，与 ImGui 共用同
// 一条 loader 解析路径；不再调静态 vulkan-1.lib stub 的 `vkGetInstanceProcAddr`。
VkDescriptorPool MakeImguiDescriptorPool(PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr,
                                          VkInstance               instance,
                                          VkDevice                 device)
{
    auto vkGetDeviceProcAddrFn =
        reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            pfnGetInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
    if (vkGetDeviceProcAddrFn == nullptr) {
        std::fprintf(stderr, "[OrangeEditor] resolve vkGetDeviceProcAddr failed\n");
        return VK_NULL_HANDLE;
    }
    auto vkCreateDescriptorPoolFn =
        reinterpret_cast<PFN_vkCreateDescriptorPool>(
            vkGetDeviceProcAddrFn(device, "vkCreateDescriptorPool"));
    if (vkCreateDescriptorPoolFn == nullptr) {
        std::fprintf(stderr, "[OrangeEditor] resolve vkCreateDescriptorPool failed\n");
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

// 对应 MakeImguiDescriptorPool 的关停期清理；同样走 OrangeRender 的 loader。
void DestroyImguiDescriptorPool(PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr,
                                 VkInstance               instance,
                                 VkDevice                 device,
                                 VkDescriptorPool         pool)
{
    if (pool == VK_NULL_HANDLE) { return; }
    auto vkGetDeviceProcAddrFn =
        reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            pfnGetInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
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
    cfg.window.title  = "OrangeEditor v0.0.3 (Task 06-02 default dock layout)";
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

    // ---- 取 OrangeRender 透出的 Vulkan handle + loader fn ---------------
    // handles 来自 FEATURE-2026-05-09；loader fn 来自 FEATURE-2026-05-10。
    // loader fn 是 OrangeRender 内 volk 已加载的 vkGetInstanceProcAddr，
    // 编辑器 ImGui + descriptor pool 创建全部走这一份 loader，与 OrangeRender
    // 共用 instance dispatch 状态。
    const auto handles = Orange::Renderer::Interop::GetVulkanDeviceHandles(*pRenderDevice);
    auto pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        Orange::Renderer::Interop::GetVulkanGetInstanceProcAddr());
    if (pfnGetInstanceProcAddr == nullptr) {
        std::fprintf(stderr,
                     "[OrangeEditor] Interop::GetVulkanGetInstanceProcAddr 返回 null —— "
                     "非 Vulkan 后端或 RenderDevice 尚未 Initialize\n");
        return 1;
    }
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

    // 默认 UI 字体加大：优先用 Windows 系统 Segoe UI（TrueType，任意 size
    // 都清晰），找不到回退到 ImGui 内嵌 ProggyClean 拉大 SizePixels（位图
    // 字体非原生 size 略糊但保底可用）。**必须**在 ImGui_ImplVulkan_Init
    // 之前完成 —— Vulkan backend 在 Init 阶段从 io.Fonts atlas 创建 font
    // texture，后改动 atlas 需要重建 + 重上传。
    {
        ImFont* fontMain = io.Fonts->AddFontFromFileTTF(
            "C:\\Windows\\Fonts\\segoeui.ttf", kDefaultFontSizePx);
        if (fontMain == nullptr) {
            ImFontConfig fontCfg;
            fontCfg.SizePixels = kDefaultFontSizePx;
            io.Fonts->AddFontDefault(&fontCfg);
            std::fprintf(stdout,
                         "[OrangeEditor] Segoe UI 加载失败，回退 ImGui 默认字体 @%.0fpx\n",
                         kDefaultFontSizePx);
        }
    }

    // GLFW backend —— install_callbacks=true 让 ImGui 自动装 GLFW key /
    // mouse / focus 回调；与 AppHost 共享同一 window，事件分发上 ImGui
    // 拦在 AppHost 之前（GLFW 回调链顺序）
    if (!ImGui_ImplGlfw_InitForVulkan(glfwWindow, true)) {
        std::fprintf(stderr, "[OrangeEditor] ImGui_ImplGlfw_InitForVulkan failed\n");
        return 1;
    }

    // ImGui Vulkan backend —— ImGui 在 NO_PROTOTYPES 下需要先用 LoadFunctions
    // 把内部 ~30 个 vkXxx 指针逐个 resolve；user_data 必须同时携带 loader fn
    // 与一个真 VkInstance，否则 instance/device 级函数无法解析。LoadFunctions
    // 仅在调用期间读 user_data，本地栈对象生命周期足够。
    // 预解析 vkGetDeviceProcAddr 作为 loader 的二级回退（仅 KHR→core alias
    // 路径用得到）。device proc addr 比 instance proc addr 对 device 级
    // 命令的解析更可靠 —— 后者在某些 loader 实现里对 core 1.3 device
    // 命令有 quirk。
    auto pfnGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        pfnGetInstanceProcAddr(vkInstance, "vkGetDeviceProcAddr"));

    ImguiVulkanLoaderCtx loaderCtx{pfnGetInstanceProcAddr, pfnGetDeviceProcAddr, vkInstance, vkDevice};
    if (!ImGui_ImplVulkan_LoadFunctions(&ImguiVulkanLoader, &loaderCtx)) {
        std::fprintf(stderr, "[OrangeEditor] ImGui_ImplVulkan_LoadFunctions failed\n");
        return 1;
    }

    VkDescriptorPool imguiDescPool =
        MakeImguiDescriptorPool(pfnGetInstanceProcAddr, vkInstance, vkDevice);
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
        DestroyImguiDescriptorPool(pfnGetInstanceProcAddr, vkInstance, vkDevice, imguiDescPool);
        return 1;
    }

    // ---- Layer 注入 -----------------------------------------------------
    host->PushLayer(std::make_unique<EditorRenderLayer>(*host, *pRenderer,
                                                        imguiDescPool, vkDevice));

    std::fprintf(stdout, "[OrangeEditor] ImGui dock + multi-viewport ready. Esc 退出。\n");

    const int rc = host->Run();

    // ---- 关停 ---------------------------------------------------------
    // 关键约束：ImGui_ImplGlfw_Shutdown 会销毁 multi-viewport 期间 ImGui
    // 自己开的额外 GLFWwindow + 标准 cursor，必须发生在 AppHost dtor
    // （主 GLFWwindow 销毁 + glfwTerminate）**之前**，否则 GLFW 已经
    // 被 terminate，所有 glfwDestroy* 调用会丢 17 行
    // GLFW_NOT_INITIALIZED。
    //
    // 同时还要先 EditorRenderLayer 析构（dtor 清 overlay callback），
    // 避免 renderer Shutdown 时调到捕获 ImGui 已 dead 状态的 callback。
    // LayerStack 由 host 拥有，要逼析构必须先 host.reset() —— 这跟上一
    // 段冲突：layer 想先 reset，window 想后 reset。出路是手工先把
    // overlay callback 清空，再做 ImGui shutdown 与 host.reset。
    pRenderDevice->WaitIdle();
    pRenderer->SetSwapchainOverlayCallback({});  // layer dtor 之外手工提前清
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    host.reset();   // → AppHost dtor → LayerStack dtor → EditorRenderLayer dtor
                    //   （overlay callback 已提前清，dtor 再清一次是幂等的）
    DestroyImguiDescriptorPool(pfnGetInstanceProcAddr, vkInstance, vkDevice, imguiDescPool);
    pRenderer->Shutdown();
    pRenderer.reset();
    pRenderDevice.reset();

    std::fprintf(stdout, "[OrangeEditor] clean shutdown.\n");
    return rc;
}
