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
// Inspector / Assets / Console。前四个仅 TextDisabled 占位，后续
// 逐个填实；Console 当前放帧统计 + Esc 退出按钮，等接 Core::Log 时换
// 成日志流。默认 dock 布局首帧通过 DockBuilder* 编程式建立，之后用户调
// 整由 imgui.ini 持久化。

// NOMINMAX / WIN32_LEAN_AND_MEAN 必须在**任何**可能传染 windows.h 的头之
// 前 define —— GLFW_EXPOSE_NATIVE_WIN32 + GLFW/glfw3native.h 会拉 windows.h，
// 后续 std::min / std::numeric_limits::max 会被 min/max 宏污染（实测 build
// 报 C2589 / C2737）。
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <orange/engine/app/AppConfig.h>
#include <orange/engine/app/AppHost.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/app/Layer.h>
#include <orange/engine/platform/Window.h>
#include <orange/engine/platform/WindowEvent.h>
#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/ColliderDesc.h>
#include <orange/engine/physics/RigidBodyComponent.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/render/ParticleEmitterComponent.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

// 编辑器内部模块（拆分后的本地 header；不进 include/ 公共面）
#include "DemoWorld.h"
#include "EditorHost.h"
#include "EditorRenderLayer.h"
#include "VulkanLoaderShim.h"
#include "demo_game/HealthComponent.h"
#include "schema/RegisterBuiltinSchemas.h"

#include <glm/gtc/matrix_transform.hpp>  // glm::lookAt（编辑器相机用）
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <orange/renderer/RenderDevice.h>
#include <orange/renderer/Renderer.h>
#include <orange/renderer/VulkanInterop.h>
#include <orange/rhi/RHICommandList.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <vulkan/vulkan.h>

// windows.h —— main 仅用 HWND (glfwGetWin32Window 返回类型) + 关停期
// COM 上下文；IFileDialog / IShellItem 已迁到 VulkanLoaderShim.cpp。
#include <windows.h>

#include <imgui.h>
#include <imgui_internal.h>  // DockBuilder* API（仅在编辑器侧首帧建默认布局用）
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace
{

// 编辑器默认 UI 字体 size（像素）。ImGui 内嵌 ProggyClean 默认 13px，在
// 1080p+ 屏上对编辑器使用偏小；这里拉到 28px。后续要做的扩展点：把这个
// 数值抽到一个"编辑器 Settings"面板里让用户运行时调整 —— 改后重建
// io.Fonts atlas 并触发 ImGui_ImplVulkan_CreateFontsTexture 重传到 GPU。
constexpr float kDefaultFontSizePx = 36.0f;

// main.cpp 现在仅承担引擎 / Vulkan / ImGui 启动 + push layer + 关停序列。
// 业务逻辑已按 commit 1 / 2 / 3 + v0.2.5 整骨拆出：
//   EditorHost            → EditorHost.h（顶层 hub，聚合 4 sub-context + cmdStack）
//   context/Editor*       → 4 个 sub-context（selection / scene / assets / camera）
//   EditorHierarchy       → EditorHierarchy.{h,cpp}
//   VulkanLoaderShim      → VulkanLoaderShim.{h,cpp}（含 ImguiVulkanLoader /
//                           Make/DestroyImguiDescriptorPool / ShowSceneFileDialog）
//   DemoWorld             → DemoWorld.{h,cpp}（mesh 工厂 / InitializeEditorAssets
//                           / SeedDemoWorld）
//   EditorCameraControl   → EditorCameraControl.{h,cpp}
//   EditorWidgets         → EditorWidgets.{h,cpp}（DragVec3Colored）
//   EditorRenderLayer     → EditorRenderLayer.{h,cpp} + panels/*.cpp（按面板
//                           切到独立 TU）

}  // namespace

int main()
{
    using namespace Orange::Engine;

    // ---- AppHost（窗口 + 主循环）---------------------------------------
    AppConfig cfg{};
    cfg.window.title  = "OrangeEditor v0.0.3";
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

    // ---- 编辑器顶层 EditorHost + 启动期场景 ---------------------------------
    //
    // EditorHost 是编辑器中央 hub（Lumix StudioApp / Godot EditorNode 同位）：
    // 聚合 4 个 sub-context（selection / scene / assets / camera）+ CommandStack。
    // scene context 拥有 World 所有权；场景 Open / New 在 OnUpdate 内整体
    // swap world，所有权放 host.scene 内最自然。生命周期：editorHost 与
    // AppHost 在同一 scope；AppHost.reset() 在关停段手工提前调，保证 layer
    // 析构时 host（含 world）仍存活。
    //
    // 资产初始化必须先于场景加载，否则 RenderableComponent.mesh 会拿到
    // Invalid handle，Scene 视口画不出几何。
    //
    // 启动优先级：检测 assets/scenes/demo.scene.json（相对 .exe 工作目录），
    // 存在则 Load；文件不存在（IoError）或加载失败则回退 SeedDemoWorld。
    // File > New Scene 走 ApplyPendingSceneOp，与回退路径保持一致。
    // 启动期一次性注册所有内置 component schema —— 必须在创建 EditorRenderLayer
    // 之前完成，让 Inspector 首帧绘制时 registry 已 ready。后续 v0.3 游戏侧
    // 自定义 component 通过 OrangeEditor::RegisterComponentSchema 扩展点
    // 继续追加。
    Orange::Editor::Schema::RegisterBuiltinSchemas();
    DemoGame::RegisterHealthComponentSchema();

    EditorHost editorHost;
    editorHost.scene.pWorld = std::make_unique<Orange::Engine::World>();
    InitializeEditorAssets(editorHost);
    editorHost.extraSerializers.push_back(DemoGame::GetHealthSerializerEntry());

    // 把 CommandStack 的"栈有效变更"钩子绑到 scene.dirty——任何 Push / Undo /
    // Redo / EndGroup 后 File>Save 菜单立刻亮起。pHost 捕获本地 editorHost 地
    // 址；editorHost 与 cmdStack 同生命周期（main 栈帧），lambda 不会悬挂。
    editorHost.cmdStack.SetOnChanged(
        [pHost = &editorHost]{ pHost->scene.dirty = true; });
    {
        const auto namedMat = BuildNamedMaterialInstances(editorHost.assets);
        Scene::LoadOptions demoLoadOpts{};
        demoLoadOpts.assetRegistry          = editorHost.assets.pAssets.get();
        demoLoadOpts.animatorRegistry       = editorHost.assets.pAnimators.get();
        demoLoadOpts.namedMaterialInstances = &namedMat;
        demoLoadOpts.extraSerializers       = editorHost.extraSerializers;
        if (auto res = Scene::Load("assets/scenes/demo.scene.json",
                                   *editorHost.scene.pWorld, demoLoadOpts);
            res.IsErr())
        {
            SeedDemoWorld(editorHost);
        }
        else
        {
            editorHost.scene.currentScenePath = "assets/scenes/demo.scene.json";
        }
    }

    // ---- Layer 注入 -----------------------------------------------------
    host->PushLayer(std::make_unique<EditorRenderLayer>(*host, *pRenderDevice,
                                                        *pRenderer,
                                                        imguiDescPool, vkDevice,
                                                        editorHost));

    std::fprintf(stdout,
                 "[OrangeEditor] ImGui dock + multi-viewport ready. world entities=%zu. Esc 退出。\n",
                 editorHost.scene.pWorld->Size());

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
