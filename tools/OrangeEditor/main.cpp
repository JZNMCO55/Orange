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
#include <orange/engine/core/Log.h>
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
#include <orange/engine/scene/LayerComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/asset/TextureAsset.h>
#include <orange/engine/render/EnvironmentComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/scene/WorldPartition.h>

// 编辑器内部模块（拆分后的本地 header；不进 include/ 公共面）
#include "BuiltinAssets.h"
#include "DemoWorld.h"
#include "EditorHost.h"
#include "EditorRenderLayer.h"
#include "VulkanLoaderShim.h"
#include "branding/EditorWindowIcon.h"
#include "demo_game/HealthComponent.h"
#include "plugin/AnimFsmAssetInspectorPlugin.h"
#include "plugin/AnimatorMiniPreviewPlugin.h"
#include "plugin/AudioAssetInspectorPlugin.h"
#include "plugin/AudioSourceInspectorPlugin.h"
#include "plugin/CameraFrustumGizmoPlugin.h"
#include "plugin/ColliderEditInspectorPlugin.h"
#include "plugin/DirectionalLightGizmoPlugin.h"
#include "plugin/DragonBonesAssetInspectorPlugin.h"
#include "plugin/ImportMetaAssetInspectorPlugin.h"
#include "plugin/MaterialAssetInspectorPlugin.h"
#include "plugin/ParticleEmitterGizmoPlugin.h"
#include "plugin/PointLightGizmoPlugin.h"
#include "plugin/PostProcessVolumeGizmoPlugin.h"
#include "plugin/SpotLightGizmoPlugin.h"
#include "schema/RegisterBuiltinSchemas.h"
#include "theme/EditorTheme.h"

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
#include <filesystem>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace
{

// 编辑器默认 UI 字体 size 的**设计基准**（@ 100% DPI / content scale 1.0）。
// 实际加载到 atlas 的字号是 kDesignFontSizePx * contentScale（见 ImGui Init
// 段，通过 glfwGetWindowContentScale 拿主显示器缩放）。同步 style.ScaleAllSizes
// 让 padding / spacing 一起按 DPI 缩放，避免在低物理像素 / 低缩放屏上 panel
// 内 widget 撑爆 dock cell。注意：本路径仅做全局 DPI 兜底；Inspector / Panel
// 内部 widget 列宽 / minSize 的比例化属架构整骨范畴，由对应 milestone 根治。
constexpr float kDesignFontSizePx = 18.0f;

// 启动期自定位仓库根（与 Lumix / Godot 同款方案）：用户从 build/bin/Debug
// 双击 .exe / IDE F5 / 任意 cwd 启动时，把 cwd 切回包含真实 assets/ 的仓
// 库根。否则 fs::current_path() = 启动者所在目录，所有 "assets/..." 相
// 对路径都找错位置（v0.5 B1 验收 retro：用户 cwd=build/bin/Debug 时只能
// 看到 build 产物自创建的 assets/ 子集 meshes + materials/builtin，而看不
// 到仓库根真实 assets/ 里的 scenes / configs）。
//
// 标记选用 "assets/scenes/demo.scene.json" 而非 "assets/" 本身 —— 后者
// 在 build/bin/Debug 下也会存在（DemoWorld lazy-bake fallback 写出来），
// 无法区分仓库根与 build 产物。.scene.json 只在仓库根有。
void ChdirToRepoRoot()
{
    namespace fs = std::filesystem;
    wchar_t exePathW[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
    if (len == 0 || len == MAX_PATH) { return; }

    fs::path dir = fs::path(exePathW).parent_path();
    constexpr int kMaxWalkUp = 8;
    for (int i = 0; i < kMaxWalkUp; ++i)
    {
        std::error_code ec;
        if (fs::exists(dir / "assets" / "scenes" / "demo.scene.json", ec))
        {
            fs::current_path(dir, ec);
            return;
        }
        const fs::path parent = dir.parent_path();
        if (parent == dir) { return; }
        dir = parent;
    }
}

// main.cpp 现在仅承担引擎 / Vulkan / ImGui 启动 + push layer + 关停序列。
// 业务逻辑已按 commit 1 / 2 / 3 + v0.2.5 整骨拆出：
//   EditorHost            → EditorHost.h（顶层 hub，聚合 4 sub-context + cmdStack）
//   context/Editor*       → 4 个 sub-context（selection / scene / assets / camera）
//   EditorHierarchy       → EditorHierarchy.{h,cpp}
//   VulkanLoaderShim      → VulkanLoaderShim.{h,cpp}（含 ImguiVulkanLoader /
//                           Make/DestroyImguiDescriptorPool / ShowSceneFileDialog）
//   BuiltinAssets         → BuiltinAssets.{h,cpp}（mesh 工厂 / InitializeEditorAssets
//                           / BuildNamedMaterialInstances —— v1.0.1 c11 从
//                           DemoWorld 拆出，与 demo 内容职责分离）
//   DemoWorld             → DemoWorld.{h,cpp}（SeedDemoWorld /
//                           SeedPbrShowcaseWorld 仅 demo 场景填充）
//   EditorCameraControl   → EditorCameraControl.{h,cpp}
//   EditorWidgets         → EditorWidgets.{h,cpp}（DragVec3Colored）
//   EditorRenderLayer     → EditorRenderLayer.{h,cpp} + panels/*.cpp（按面板
//                           切到独立 TU）

}  // namespace

int main()
{
    using namespace Orange::Engine;

    // 控制台用 UTF-8 码页解码日志输出 —— 源文件 / 日志字符串都是 UTF-8,中文
    // 系统控制台默认 GBK(936) 会把 UTF-8 中文显示成乱码。设成 CP_UTF8 让
    // ORANGE_LOG 的中文正常显示（GAP-2026-05-25 用户反馈控制台乱码）。
#if defined(_WIN32)
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);
#endif

    // 必须在任何相对路径 IO（Scene::Load / asset lazy-bake / shader 编译
    // 缓存等）之前完成 chdir，否则 build/bin/Debug 启动场景会产生 stale
    // build 产物 assets/ 子树污染。
    ChdirToRepoRoot();

    // ---- AppHost（窗口 + 主循环）---------------------------------------
    AppConfig cfg{};
    cfg.window.title  = "OrangeEditor v0.0.3";
    cfg.window.width  = 1600;
    cfg.window.height = 900;
    auto hostRes = AppHost::Create(cfg);
    if (hostRes.IsErr()) {
        ORANGE_LOG_ERROR("[OrangeEditor] AppHost::Create failed (code={})",
                         static_cast<unsigned>(hostRes.Error()));
        return 1;
    }
    auto host = std::move(hostRes).Value();
    auto* glfwWindow = static_cast<GLFWwindow*>(host->GetWindow().GetGlfwWindowHandle());

    // 运行期窗口 icon —— 与 OrangeEditor.rc 嵌进 exe 的 ICO 资源双重保险。
    // .rc 覆盖 Explorer / Alt-Tab / taskbar inactive；本调用覆盖窗口装饰
    // 区 / 多视口子窗口（GLFW 不会让子窗口继承主窗口 icon，必须每个
    // GLFWwindow 单独 set；当前仅主窗口，多视口扩展见 BRANDING.md）。
    OrangeEditorBranding::ApplyEditorWindowIcons(glfwWindow);

    // 启动即 maximize —— v0.4.5 后用户在低 DPI / 窄屏机器报 "初始打开 OK，
    // 用户拉伸 / 最大化后 Inspector 永久消失"。诊断初步排除 multi-viewport
    // detach 误判（关 ViewportsEnable 仍复现）；嫌疑点剩 GLFW WindowSize
    // callback chain（AppHost OnSize + ImGui ImplGlfw 1.91+ WindowSize
    // chained handler）在 resize 风暴下的事件分发顺序、或 OrangeRender
    // swap-chain rebuild 与 ImGui DisplaySize sync 的时序竞争。
    //
    // 当前 commit 是 workaround：在 ImGui / Renderer init 之前立刻 maximize，
    // 让 GLFW window settle 到 maximized 物理尺寸；之后 Renderer 创建
    // swap-chain + ImGui DisplaySize 都直接以 maximized 尺寸为基准，跳过
    // "1600×900 → maximized" 的 resize transition 路径。如果验证通过，
    // 说明 bug 在 resize transition 链上，留独立 session 挖 root cause；
    // 验证不通过则 dock layout 本身在 maximize 状态下就算错。
    glfwMaximizeWindow(glfwWindow);

    // GAP-2026-05-17-editor-first-frame-flash 修复：glfwMaximizeWindow 在
    // Windows 上是异步消息——maximize 后 GLFW 内部 framebuffer size 不会立刻
    // 更新；之后 RenderDevice / Renderer 创建 swap-chain 拿的是旧 1600×900 尺
    // 寸，第一帧 present 时 surface 已经变 1920×N，swap-chain 与 surface 错配
    // 表现为"左上 1600×900 区域 = 渲染内容、其余 = 未覆盖白色框 buffer"的
    // 一闪而过白条。
    //
    // 修法：连续 PollEvents + 比对 framebuffer size，让 GLFW 把 maximize 后
    // 的 WM_SIZE 消息处理完再继续。最多等 32 轮（~ 几十毫秒，Windows 通常
    // 1-2 轮就回，安全余量）；超时仍走原路径不阻断启动。
    {
        int prevW = 0, prevH = 0;
        glfwGetFramebufferSize(glfwWindow, &prevW, &prevH);
        for (int attempt = 0; attempt < 32; ++attempt)
        {
            glfwPollEvents();
            int newW = 0, newH = 0;
            glfwGetFramebufferSize(glfwWindow, &newW, &newH);
            if (newW != prevW || newH != prevH)
            {
                // size 变了，再 poll 一轮直到稳态（防 macOS / Linux DWM 多
                // 段 resize），否则跳出
                prevW = newW;
                prevH = newH;
            }
            else if (attempt > 0)
            {
                // 连续两轮 size 未变 = settled
                break;
            }
        }
    }

    // ---- 编辑器自管 RenderDevice + IRenderer ---------------------------
    Orange::Renderer::RenderDeviceDesc rdDesc{};
    rdDesc.mBackend          = Orange::Renderer::BackendType::Default;
    rdDesc.mEnableValidation = true;
    auto pRenderDevice = Orange::Renderer::RenderDevice::Create(rdDesc);
    if (pRenderDevice == nullptr) {
        ORANGE_LOG_ERROR("[OrangeEditor] RenderDevice::Create failed");
        return 1;
    }

    auto pRenderer = Orange::Renderer::CreateRenderer();
    Orange::Renderer::RendererDesc rendererDesc{};
    rendererDesc.mpDevice             = &pRenderDevice->GetRhiDevice();
    rendererDesc.mpNativeWindowHandle = glfwWindow;
    rendererDesc.mFramesInFlight      = 2;
    if (Orange::Failed(pRenderer->Initialize(rendererDesc))) {
        ORANGE_LOG_ERROR("[OrangeEditor] Renderer::Initialize failed");
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
        ORANGE_LOG_ERROR("[OrangeEditor] Interop::GetVulkanGetInstanceProcAddr 返回 null —— "
                         "非 Vulkan 后端或 RenderDevice 尚未 Initialize");
        return 1;
    }
    // 跑一帧让 swap-chain ready，再取 swap-chain info（min/count + format）
    {
        Orange::Renderer::FrameTimeInfo dummy{};
        dummy.mTotalTimeSeconds = 0.0;
        dummy.mDeltaTimeSeconds = 0.0f;
        if (Orange::Failed(pRenderer->BeginFrame(dummy))) {
            ORANGE_LOG_ERROR("[OrangeEditor] dummy BeginFrame failed");
            return 1;
        }
        if (Orange::Failed(pRenderer->EndFrame())) {
            ORANGE_LOG_ERROR("[OrangeEditor] dummy EndFrame failed");
            return 1;
        }
    }
    const auto sci = Orange::Renderer::Interop::GetVulkanSwapchainInfo(*pRenderer);
    ORANGE_LOG_INFO("[OrangeEditor] Vulkan handles: instance={} device={} qFamily={}",
                    handles.vkInstance, handles.vkDevice, handles.graphicsQueueFamilyIndex);
    ORANGE_LOG_INFO("[OrangeEditor] swap-chain: min={} count={} format={} {}x{}",
                    sci.minImageCount, sci.imageCount,
                    static_cast<int>(sci.colorFormat), sci.imageWidth, sci.imageHeight);

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
    // v0.6.5 c2：编辑器主题从 ImGui StyleColorsDark 切到 Cocos 炭灰
    // 路线。ApplyToImGui 内部以 StyleColorsDark 为 baseline（保证未覆盖
    // 的 ImGuiCol_* 仍有合法值），然后按 EditorTheme token 覆盖关键项
    // （背景 / 控件三态 / 字色 / 分隔 / 圆角 / 间距 / 边框）。
    Orange::Editor::Theme::ApplyToImGui();
    // 多视口模式下让"detached" window 看起来跟主窗口风格一致：
    // WindowRounding 已被 ApplyToImGui 设为 0，本段仅强制
    // ImGuiCol_WindowBg.w = 1.0f 防止多视口透明导致 detached window 看穿
    // 桌面（ImGui 多视口默认偏好半透）。
    if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // DPI / content-scale 自适应：ImGui 内部所有 widget 尺寸都是绝对像素，
    // 字号 + style padding 不随屏幕 DPI 自动放缩。这里拿 GLFW 主窗口的
    // content scale（Windows 上等于"设置 → 系统 → 显示 → 缩放与布局"那个
    // 百分比，如 1.0 / 1.25 / 1.5 / 1.75 / 2.0），用同一个系数同时缩放：
    //   * io.Fonts 加载字号：kDesignFontSizePx * scale
    //   * ImGui style（FramePadding / ItemSpacing / IndentSpacing 等）整体
    //     ScaleAllSizes(scale)
    // 不读 io.FontGlobalScale —— 那是位图重采样路径，TrueType 字体直接用目标
    // 像素加载更清晰。注意 ScaleAllSizes 必须只调一次，重复调会指数级放大。
    float dpiScaleX = 1.0f;
    float dpiScaleY = 1.0f;
    glfwGetWindowContentScale(glfwWindow, &dpiScaleX, &dpiScaleY);
    const float dpiScale = dpiScaleX > 0.0f ? dpiScaleX : 1.0f;
    const float fontPx   = kDesignFontSizePx * dpiScale;
    ImGui::GetStyle().ScaleAllSizes(dpiScale);

    // 默认 UI 字体：Microsoft YaHei UI（msyh.ttc）—— 同时含 ASCII + 简体中文
    // glyph，覆盖编辑器 UI 内所有中英混排 tooltip / 提示。
    //
    // 之前用 Segoe UI（segoeui.ttf）字体不含中文 glyph，所有中文字符显示为
    // "?" 占位（Animation panel placeholder / Inspector AssetRef tooltip 等
    // 中文文案全部乱码）。msyh.ttc 是 Windows 10/11 默认安装字体，ASCII +
    // 简体中文都覆盖；体积比 simsun.ttc 稍大但清晰度好。
    //
    // ImGui 默认 glyph range 是 ASCII (0x20-0xFF)，必须**显式**传入 CJK 范围
    // 才会把中文字符烘焙到 font atlas。
    //
    // 用 `GetGlyphRangesChineseFull` (21000+ 字，覆盖完整 CJK Unified
    // Ideographs Basic 区 U+4E00-U+9FAF + 标点 + 假名等) 而非
    // `GetGlyphRangesChineseSimplifiedCommon` 的 ~2500 常用字——后者漏
    // 罕用字 / 部分 CJK 标点（em-dash U+2014 等）导致零星 "?" 乱码。
    // 代价：font atlas 体积增大约 600KB，启动时烘焙 + 上传时间增 ~100ms 量级，
    // editor 场景可接受。后续若 atlas 体积压力大，可切按需 glyph 加载方案。
    //
    // **必须**在 ImGui_ImplVulkan_Init 之前完成 —— Vulkan backend 在 Init 阶段
    // 从 io.Fonts atlas 创建 font texture，后改动 atlas 需要重建 + 重上传。
    //
    // 失败 fallback 链：msyh.ttc → segoeui.ttf (ASCII only) → ImGui 内置
    // ProggyClean（位图，仅 ASCII；中文仍乱码但保底能跑）。
    //
    // GetGlyphRangesChineseFull 不含希腊字母 / 箭头 / 部分数学符号，导致
    // 项目里的 "Δ"（时间增量）/ "→"（教程指引）/ "×" 等显示为 "?"。用
    // ImFontGlyphRangesBuilder 在 ChineseFull 之上额外加进项目实际用到的
    // 一小撮非 CJK Unicode 符号（msyh.ttc 覆盖这些 codepoint）。builder
    // 输出的 ranges 数组必须 stay alive 到 ImGui_ImplVulkan_Init 完成
    // font texture 上传，所以用 static 持有。
    static ImVector<ImWchar> sCustomGlyphRanges;
    {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
        // 项目实际用到的非 CJK 字符：希腊大写 Δ、箭头 → ← ↑ ↓、乘号 ×、
        // 度数 °、约等 ≈、不等 ≠ ≤ ≥、无穷 ∞、希腊小写 μ π σ ω。
        // 源文件 UTF-8 编码（cmake /utf-8），这里用普通字符串字面量即可
        // （不要用 u8"..."，C++20 下是 const char8_t* 与 AddText 签名不兼容）。
        builder.AddText("Δ→←↑↓×°≈≠≤≥∞μπσω");
        builder.BuildRanges(&sCustomGlyphRanges);
    }
    {
        const ImWchar* cjkRanges = sCustomGlyphRanges.Data;
        ImFont* fontMain = io.Fonts->AddFontFromFileTTF(
            "C:\\Windows\\Fonts\\msyh.ttc", fontPx, nullptr, cjkRanges);
        if (fontMain == nullptr) {
            ORANGE_LOG_WARN("[OrangeEditor] msyh.ttc 加载失败，回退 segoeui.ttf "
                            "(ASCII only, 中文会显示成 '?')");
            fontMain = io.Fonts->AddFontFromFileTTF(
                "C:\\Windows\\Fonts\\segoeui.ttf", fontPx);
        }
        if (fontMain == nullptr) {
            ImFontConfig fontCfg;
            fontCfg.SizePixels = fontPx;
            io.Fonts->AddFontDefault(&fontCfg);
            ORANGE_LOG_WARN("[OrangeEditor] 系统字体全部加载失败，回退 ImGui 默认 "
                            "@{:.0f}px (dpiScale={:.2f})",
                            fontPx, dpiScale);
        }

        // v0.6.5 c3：Codicons icon font merge —— 把 VS Code 同款 icon font
        // 合并到当前主字体（msyh / segoeui / ImGui default 任一），让按钮 /
        // tooltip 可以直接写 ICON_CI_* codepoint 与中英文混排。
        //
        // MergeMode = true → 同一 ImFont 多 source；GlyphMinAdvanceX = fontPx
        // 让 icon 至少占满字符宽（多数 Codicons 字形是 1em 宽，与文字基线一
        // 致即可，不需要额外行距）。PixelSnapH 让 icon 边像素对齐避免 subpixel
        // 渲染模糊。
        //
        // ranges 必须 static 寿命：AddFontFromFileTTF 不复制 range 数组，仅
        // 存指针，io.Fonts->Build()（ImGui_ImplVulkan_Init 内触发）期间需访问。
        //
        // 路径：相对工作目录（已被 ChdirToRepoRoot 切到仓库根）。加载失败仅
        // log，不致命——失败后按钮显示 codepoint 对应的 fallback glyph "?"
        // 占位，编辑器仍能用。
        // Codicons font merge 参数：
        //   * GlyphMinAdvanceX = fontPx —— 让 icon glyph 至少占 1em 宽
        //     （多数 Codicons 设计就是 1em，本参数确保偶尔的 < 1em glyph
        //     也对齐到 1em，避免横向密度参差）
        //   * **不**设 GlyphMaxAdvanceX —— 实测设了 = fontPx 反而让 glyph
        //     在 1em cell 内位置失控（用户反馈"icon 在按钮内偏右"），
        //     用 glyph 自身 advance 让 ImGui 默认居中算法自然工作更稳
        //   * GlyphOffset.y = floor(fontPx * 0.15f) —— **仅纵向**经验偏移。
        //     Codicons icon glyph 设计在 1em cell 顶部附近（无 descender），
        //     ImGui 按 ascent/descent 算 baseline 后 icon 视觉中心略偏 button
        //     上沿（实测用户反馈"偏上"），向下推 ~15% fontPx 把视觉中心拉
        //     到 button center。这个值只动纵向不动横向，与 auto-size 按钮
        //     正交不冲突。
        //   * 副作用：" + Add Component" 这种 icon+文字组合里 icon 比文字
        //     baseline 略低 2-3 px @ 18px font。toolbar icon-only 按钮是
        //     主战场，这点偏移视觉可接受。
        // 工程教训：调字体 metrics 的几何精度撞 ImGui 内部 layout 算法
        // 不会赢——按钮居中靠 auto-size 比靠 fixed-size + glyph offset 稳；
        // 但**仅纵向** GlyphOffset 仍是 ImGui icon font 集成的标准 hack。
        static const ImWchar kCodiconsRange[] = { 0xea60, 0xf102, 0 };
        ImFontConfig codiconsCfg;
        codiconsCfg.MergeMode        = true;
        codiconsCfg.PixelSnapH       = true;
        codiconsCfg.GlyphMinAdvanceX = fontPx;
        codiconsCfg.GlyphOffset.y    = std::floor(fontPx * 0.15f);
        ImFont* fontCodicons = io.Fonts->AddFontFromFileTTF(
            "tools/OrangeEditor/theme/codicons/codicon.ttf",
            fontPx, &codiconsCfg, kCodiconsRange);
        if (fontCodicons == nullptr) {
            ORANGE_LOG_WARN("[OrangeEditor] codicon.ttf 加载失败 —— Codicons icon "
                            "将显示为 '?' 占位（不致命）");
        }
    }

    // GLFW backend —— install_callbacks=true 让 ImGui 自动装 GLFW key /
    // mouse / focus 回调；与 AppHost 共享同一 window，事件分发上 ImGui
    // 拦在 AppHost 之前（GLFW 回调链顺序）
    if (!ImGui_ImplGlfw_InitForVulkan(glfwWindow, true)) {
        ORANGE_LOG_ERROR("[OrangeEditor] ImGui_ImplGlfw_InitForVulkan failed");
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
        ORANGE_LOG_ERROR("[OrangeEditor] ImGui_ImplVulkan_LoadFunctions failed");
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
        ORANGE_LOG_ERROR("[OrangeEditor] ImGui_ImplVulkan_Init failed");
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

    // v0.8 EditorSettings + EditorKeybindings 持久化：启动时尝试从 editor_
    // settings.json 加载（含 settings 段 + keybindings 段共享同文件），缺失
    // / 解析失败保留默认值。文件相对路径（ChdirToRepoRoot 之后）。
    constexpr const char* kEditorSettingsPath = "editor_settings.json";
    {
        auto readRes = Orange::Engine::JsonReader::FromFile(kEditorSettingsPath);
        if (readRes.IsOk())
        {
            ReadEditorSettings(readRes.Value(), editorHost.settings);
            ReadEditorKeybindings(readRes.Value(), editorHost.keybindings);
            ORANGE_LOG_INFO("[OrangeEditor] loaded {}", kEditorSettingsPath);
        }
    }

    // 注册第一个 IEditorInspectorPlugin —— v0.3 deliverable 5 落地。
    // 多 plugin 按 push_back 顺序检查 CanHandle，第一条命中接管段；目前
    // 仅一条，未来 v0.5 Material 缩略图 / v0.7 Animator 时间轴 plugin 同款
    // push_back 在此排队即可。
    editorHost.inspectorPlugins.push_back(
        std::make_unique<Orange::Editor::Plugin::AnimatorMiniPreviewPlugin>());
    // AudioSource 段末 Play / Stop 试播按钮（与 AnimatorMiniPreviewPlugin 同
    // 款"装饰式扩展"模式）。
    editorHost.inspectorPlugins.push_back(
        std::make_unique<Orange::Editor::Plugin::AudioSourceInspectorPlugin>());
    // Collider 段末 "Edit Vertices in Viewport" 按钮（GAP-2026-05-21）——
    // Polygon / Edge Chain shape 时进入 viewport 顶点编辑子模式。
    editorHost.inspectorPlugins.push_back(
        std::make_unique<Orange::Editor::Plugin::ColliderEditInspectorPlugin>());

    // 注册第一个 IEditorAssetInspectorPlugin —— v0.7 c0 落地（消除 L16）。
    // 当 Asset 浏览器选中 .material 文件时接管 Inspector 整段；与
    // inspectorPlugins 正交（按选中资源类型而非 component schema 分派）。
    editorHost.assetInspectorPlugins.push_back(
        std::make_unique<Orange::Editor::Plugin::MaterialAssetInspectorPlugin>());

    // v0.7 c2-2：第二条 IEditorAssetInspectorPlugin —— 选中 .anim_fsm
    // 文件时接管 Inspector 整段。当前 scope 仅展示 states / transitions /
    // initial state（round-trip 验证）；节点图编辑 UI / Save / Condition
    // DSL 在 c2-3 ~ c2-6 逐步展开。
    editorHost.assetInspectorPlugins.push_back(
        std::make_unique<Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin>());

    // v0.7 c3：第三条 IEditorAssetInspectorPlugin —— 选中 DragonBones
    // skeleton 资源（_ske.json / _ske.dbbin）时接管 Inspector，显示
    // dragonBonesName + 每个 armature 的 bone / animation metadata。
    editorHost.assetInspectorPlugins.push_back(
        std::make_unique<Orange::Editor::Plugin::DragonBonesAssetInspectorPlugin>());

    // 第四条 IEditorAssetInspectorPlugin —— 选中 .wav / .ogg / .mp3 / .flac
    // 时接管 Inspector 显示 Preview Play / Stop 按钮 + 资源元数据。
    editorHost.assetInspectorPlugins.push_back(
        std::make_unique<Orange::Editor::Plugin::AudioAssetInspectorPlugin>());

    // v1.1 T5：第五条 IEditorAssetInspectorPlugin —— 任何同目录存在 .meta
    // sidecar 的资产（importer 产物 .mesh / .png / .jpg / .tga / .hdr 等）
    // 接管 Inspector readonly 显示 source path / source hash / handle id /
    // import params + Reimport 按钮。注册顺序在 Material / AnimFsm /
    // DragonBones / Audio 之后 —— 这些 plugin 自家文件无 .meta，所以不会
    // 与本 plugin 同时命中；显式 IsExtSkipped 防御也守一道。
    editorHost.assetInspectorPlugins.push_back(
        std::make_unique<Orange::Editor::Plugin::ImportMetaAssetInspectorPlugin>());

    // 注册首批 IEditorGizmoPlugin —— v0.4 c4 落地（v0.2.5 c12 抽象首批
    // 真实消费）。Light 方向箭头 + ParticleEmitter spawn box / velocity
    // 向量两个纯装饰 overlay；与 c2 / c3 内置 Transform gizmo（直接子系
    // 统，不走 plugin）正交。多 plugin 按 push_back 顺序遍历，**所有**
    // CanHandle 返回 true 的 plugin 全画（不互斥）；ScenePanel.cpp 内
    // dispatch 路径同款。
    editorHost.gizmoPlugins.push_back(
        std::make_unique<Orange::Editor::Plugin::DirectionalLightGizmoPlugin>());
    editorHost.gizmoPlugins.push_back(
        std::make_unique<Orange::Editor::Plugin::ParticleEmitterGizmoPlugin>());
    // PointLight 中心黄色圆点 + XZ 平面 range 圆环 overlay。同
    // DirectionalLight / ParticleEmitter 模式纯装饰 overlay，不接管 LMB 拖动。
    editorHost.gizmoPlugins.push_back(
        std::make_unique<Orange::Editor::Plugin::PointLightGizmoPlugin>());
    // PostProcessComponent Mode=Local 时画 localExtent 半尺寸盒（紫色内层
    // 实色 = weight=1 全效区）+ (localExtent + blendDistance) 外层框（浅色
    // = smoothstep 淡入末端）。让用户在 viewport 直接看到 V2 volume 边界，
    // 不再盯 Inspector 数字想象。Mode=Global 时 plugin 跳过。
    editorHost.gizmoPlugins.push_back(
        std::make_unique<Orange::Editor::Plugin::PostProcessVolumeGizmoPlugin>());
    // SpotLight 锥体 wireframe overlay（apex + base ring + 4 条侧棱）。同款纯
    // 装饰 overlay，方向 / 位置随 entity Transform 即时跟随。
    editorHost.gizmoPlugins.push_back(
        std::make_unique<Orange::Editor::Plugin::SpotLightGizmoPlugin>());
    // v0.4 c5：Camera frustum gizmo（plugin 当前用 hardcode 默认 fov/aspect/
    // near/far + entity transform 推 view；待 GAP-2026-05-15-camera-editor-
    // vs-runtime-separation 落地后切真实 component 数据）
    editorHost.gizmoPlugins.push_back(
        std::make_unique<Orange::Editor::Plugin::CameraFrustumGizmoPlugin>());

    // 把 CommandStack 的"栈有效变更"钩子绑到 scene.dirty——任何 Push / Undo /
    // Redo / EndGroup 后 File>Save 菜单立刻亮起。pHost 捕获本地 editorHost 地
    // 址；editorHost 与 cmdStack 同生命周期（main 栈帧），lambda 不会悬挂。
    editorHost.cmdStack.SetOnChanged(
        [pHost = &editorHost]{ pHost->scene.dirty = true; });

    // namedMaterialInstances 集中到 EditorAssetContext 自身（v0.8 整骨消除
    // L15）。BuildNamedMaterialInstances 直接写到 context 字段；schema AssetRef
    // get/set lambda 在 v0.9.5 c3 后通过 SchemaInspector 显式传入的
    // `const EditorAssetContext&` 参数访问 pAssets / namedMaterialInstances，
    // 不再需要启动期注入 file-scope 静态指针。
    editorHost.assets.namedMaterialInstances =
        BuildNamedMaterialInstances(editorHost.assets);
    {
        Scene::LoadOptions demoLoadOpts{};
        demoLoadOpts.assetRegistry          = editorHost.assets.pAssets.get();
        demoLoadOpts.animatorRegistry       = editorHost.assets.pAnimators.get();
        demoLoadOpts.namedMaterialInstances = &editorHost.assets.namedMaterialInstances;
        // 启动期 namedMaterialInstances 是 one-shot snapshot，不含 DCC 导入的
        // assets/<Type>/*.material；接 resolver 让查表失败时按磁盘 lazy-create
        // 兜底，修复"导入模型保存后重启编辑器，material 显示 None"。
        demoLoadOpts.materialResolver       =
            [&editorHost](const std::string& id) { return ::EnsureMaterialInstance(editorHost, id); };
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
            // v0.6 c4：单文件 Load 不读 manifest，扫一遍 World 把出现过的
            // LayerComponent.layerId 自动 AddLayer，让用户重启后仍能在
            // Layer Panel 看到完整列表（visible 默认 true，dirty 不变）。
            auto& reg = editorHost.scene.pWorld->Registry();
            using LC  = ::Orange::Engine::Scene::LayerComponent;
            for (auto e : reg.view<LC>()) {
                const auto& lc = reg.get<LC>(e);
                if (lc.layerId.empty()) { continue; }
                if (editorHost.scene.partition.HasLayer(lc.layerId)) { continue; }
                ::Orange::Engine::Scene::LayerInfo info;
                info.id          = lc.layerId;
                info.displayName = lc.layerId;
                info.visible     = true;
                editorHost.scene.partition.AddLayer(std::move(info));
            }
        }

        // 不在启动期自动挂 EnvironmentComponent —— 与 Cocos Creator 默认
        // 一致："viewport 干净中性灰 + IBL 全黑"的 baseline 状态。用户想
        // 启用 PBR + IBL 真实反射时，按 Add Component → Environment + 拖
        // .hdr 到 cubemap 字段触发 BakeIblFromWorld（auto-rebake 路径已通），
        // 同时 sky-dome pass 也会随之激活。这条路径与 main.cpp 别处的
        // "Inspector 加 Environment 就立刻生效" 流程对位，无额外 magic。
    }

    // PBR showcase scene 一次性 lazy 生成：检测 pbr_showcase.scene.json 不
    // 存在时种好 + Save 落盘，让用户 File → Open Scene 立即可加载。临时
    // World 内构造完后存盘即丢，不影响当前已加载的 demo.scene。
    //
    // 实现：临时把 editorHost.scene.pWorld 指向 tempWorld，调 SeedPbrShowcaseWorld
    // 复用 editorHost.assets 的 sphereMeshHandle + pbrShowcaseMaterials；
    // Save 完成后把 pWorld 指针恢复到原 demo world。tempWorld 析构时
    // RenderableComponent (POD) 不会 delete materialInstance 裸指针，无 dangling。
    {
        namespace fs = std::filesystem;
        const char* kShowcasePath = "assets/scenes/pbr_showcase.scene.json";
        if (!fs::exists(kShowcasePath))
        {
            World tempWorld;
            SeedPbrShowcaseWorld(tempWorld, editorHost.assets);

            auto namedMap = BuildNamedMaterialInstances(editorHost.assets);
            Scene::SaveOptions saveOpts{};
            saveOpts.assetRegistry          = editorHost.assets.pAssets.get();
            saveOpts.namedMaterialInstances = &namedMap;
            saveOpts.extraSerializers       = editorHost.extraSerializers;
            if (auto sv = Scene::Save(tempWorld, kShowcasePath, saveOpts);
                sv.IsErr())
            {
                ORANGE_LOG_ERROR("[OrangeEditor] Scene::Save '{}' 失败 (code={})；"
                                 "File → Open Scene 仍可手动写入",
                                 kShowcasePath, static_cast<unsigned>(sv.Error()));
            }
            else
            {
                ORANGE_LOG_INFO("[OrangeEditor] 生成 {}（18 球 PBR showcase）",
                                kShowcasePath);
            }
        }
    }

    // ---- Layer 注入 -----------------------------------------------------
    auto* pEditorLayerRaw = host->PushLayer(std::make_unique<EditorRenderLayer>(
        *host, *pRenderDevice, *pRenderer, imguiDescPool, vkDevice, editorHost));
    auto* pEditorLayer = static_cast<EditorRenderLayer*>(pEditorLayerRaw);

    // v0.6 c2：窗口 × 拦截。AppHost::Run 的循环结构是
    // `while (!ShouldClose && !exitRequested)`，先 check 后 OnUpdate ——
    // 意味用户点 × 后下一次 iteration 起头 ShouldClose=true 立即 break，
    // **不进** OnUpdate，编辑器无机会拦截 dirty。
    // 解：glfwSetWindowCloseCallback 在 GLFW 处理 × 事件时即触发，
    // dirty=true 时 set ShouldClose=false 阻止关闭 + 标记 pendingCloseAction
    // 让 EditorRenderLayer 帧末弹 popup。dirty=false 时不拦，让 ShouldClose
    // 保持 true，AppHost::Run 下次 iteration 正常退出。
    // ImGui_ImplGlfw_InitForVulkan(install_callbacks=true) 不安装
    // WindowCloseCallback（仅 Key/Char/MouseButton/Scroll/Cursor*/Focus
    // /Monitor），所以本 callback 独占该 hook。
    //
    // ⚠️ 不能用 glfwSetWindowUserPointer 存 EditorHost —— 该 user pointer 归引擎
    // Window 所有（Window::Create 设为 Window::Impl*，引擎所有 GLFW 回调经 ImplFrom
    // 当 Window::Impl* 读取）。覆盖它会让引擎 OnChar/OnKey/OnSize 等把 EditorHost
    // 误读成 Window::Impl —— OnChar 路径 impl->callback 调到垃圾 std::function 直接
    // 崩溃，OnSize 还会往 EditorHost 写 width/height 静默腐蚀内存。改用文件级静态
    // 指针：non-capturing lambda 可按名引用静态变量、仍能转成 GLFW C 回调函数指针。
    static EditorHost* spEditorHost = &editorHost;
    glfwSetWindowCloseCallback(glfwWindow, [](GLFWwindow* w)
    {
        EditorHost* pHost = spEditorHost;
        if (pHost == nullptr) { return; }
        // 未保存确认拦截 = 场景 dirty 或材质有未写盘改动（facet 1：与 EditorRenderLayer
        // ::HasUnsavedMaterial 同义，回调里直接查 host 字段，避免依赖 layer 实例）。
        const bool unsavedMaterial = !pHost->assets.editingMaterialPath.empty()
                                  && pHost->assets.editingMaterialDirty;
        if (pHost->scene.dirty || unsavedMaterial)
        {
            glfwSetWindowShouldClose(w, GLFW_FALSE);
            if (pHost->scene.pendingCloseAction == PendingCloseAction::None)
            {
                pHost->scene.pendingCloseAction = PendingCloseAction::Exit;
            }
        }
    });

    // v1.1 T2：OS 文件 drag-drop 路由。GLFW drop callback 在 glfwPollEvents
    // 主线程同步触发；ImGui_ImplGlfw_InitForVulkan(install_callbacks=true)
    // 只 chain Key/Char/MouseButton/Scroll/Cursor*/Focus/Monitor，**不**
    // 安装 DropCallback —— 本回调独占该 hook。callback 内仅 push 路径到
    // EditorHost.pendingImports；真正的 Dispatch 在 EditorRenderLayer::
    // ApplyPendingImports 帧末 drain（与 dialog 模态阻塞节奏一致）。
    glfwSetDropCallback(glfwWindow, [](GLFWwindow*, int count, const char** paths)
    {
        EditorHost* pHost = spEditorHost;
        if (pHost == nullptr || paths == nullptr) { return; }
        for (int i = 0; i < count; ++i)
        {
            if (paths[i] != nullptr) { pHost->pendingImports.emplace_back(paths[i]); }
        }
    });

    // v0.8 Console 接 Core::Log：PushLayer 之后注册 sink，让 Core::Log
    // 写入路径并行 push 到 Editor Console ring buffer。layer 的析构（host
    // .reset 触发）会先于 Core::Log 全局静态析构发生，所以这里在退出前
    // 必须显式 ClearLogSink 避免 dangling。
    if (pEditorLayer != nullptr)
    {
        Orange::Engine::Log::SetLogSink(&EditorRenderLayer::LogSinkCallback, pEditorLayer);
    }

    ORANGE_LOG_INFO("[OrangeEditor] ImGui dock + multi-viewport ready. "
                    "world entities={}. Esc 退出。",
                    editorHost.scene.pWorld->Size());

    const int rc = host->Run();

    // Console 面板 sink 解绑：layer 即将析构，避免后续日志在 dangling 指
    // 针上 push。
    Orange::Engine::Log::ClearLogSink();

    // v0.8 EditorSettings + EditorKeybindings 持久化：进程退出前写盘。失败
    // 仅 log，不阻断 shutdown。
    {
        Orange::Engine::JsonWriter w;
        WriteEditorSettings(w, editorHost.settings);
        WriteEditorKeybindings(w, editorHost.keybindings);
        auto saveRes = w.SaveToFile(kEditorSettingsPath);
        if (saveRes.IsErr())
        {
            ORANGE_LOG_WARN("[OrangeEditor] save {} failed (code={})",
                            kEditorSettingsPath,
                            static_cast<unsigned>(saveRes.Error()));
        }
    }

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

    ORANGE_LOG_INFO("[OrangeEditor] clean shutdown.");
    return rc;
}
