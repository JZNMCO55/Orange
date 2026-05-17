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
#include <orange/engine/scene/LayerComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/scene/WorldPartition.h>

// 编辑器内部模块（拆分后的本地 header；不进 include/ 公共面）
#include "DemoWorld.h"
#include "EditorHost.h"
#include "EditorRenderLayer.h"
#include "VulkanLoaderShim.h"
#include "branding/EditorWindowIcon.h"
#include "demo_game/HealthComponent.h"
#include "plugin/AnimatorMiniPreviewPlugin.h"
#include "plugin/CameraFrustumGizmoPlugin.h"
#include "plugin/DirectionalLightGizmoPlugin.h"
#include "plugin/ParticleEmitterGizmoPlugin.h"
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
        std::fprintf(stderr, "[OrangeEditor] AppHost::Create failed (code=%u)\n",
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
    {
        const ImWchar* cjkRanges = io.Fonts->GetGlyphRangesChineseFull();
        ImFont* fontMain = io.Fonts->AddFontFromFileTTF(
            "C:\\Windows\\Fonts\\msyh.ttc", fontPx, nullptr, cjkRanges);
        if (fontMain == nullptr) {
            std::fprintf(stdout,
                         "[OrangeEditor] msyh.ttc 加载失败，回退 segoeui.ttf "
                         "(ASCII only, 中文会显示成 '?')\n");
            fontMain = io.Fonts->AddFontFromFileTTF(
                "C:\\Windows\\Fonts\\segoeui.ttf", fontPx);
        }
        if (fontMain == nullptr) {
            ImFontConfig fontCfg;
            fontCfg.SizePixels = fontPx;
            io.Fonts->AddFontDefault(&fontCfg);
            std::fprintf(stdout,
                         "[OrangeEditor] 系统字体全部加载失败，回退 ImGui 默认 "
                         "@%.0fpx (dpiScale=%.2f)\n",
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
            std::fprintf(stdout,
                         "[OrangeEditor] codicon.ttf 加载失败 —— Codicons icon "
                         "将显示为 '?' 占位（不致命）\n");
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

    // 注册第一个 IEditorInspectorPlugin —— v0.3 deliverable 5 落地。
    // 多 plugin 按 push_back 顺序检查 CanHandle，第一条命中接管段；目前
    // 仅一条，未来 v0.5 Material 缩略图 / v0.7 Animator 时间轴 plugin 同款
    // push_back 在此排队即可。
    editorHost.inspectorPlugins.push_back(
        std::make_unique<Orange::Editor::Plugin::AnimatorMiniPreviewPlugin>());

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

    // v0.5 c1：namedMaterialInstances 从原本的"块内局部 const auto"提升到
    // main 整个生命周期——schema AssetRef get/set 在 Inspector 渲染 / DnD
    // 写入路径上需要持续访问该 map，绝不能让它在块退出后悬挂。
    // SetNamedMaterialInstancesForSchema 把指针注入 schema 模块文件作用域
    // 静态变量；编辑器关闭时 main 栈帧析构同时 map 析构，schema 不会再
    // 访问（layer 已先 shutdown）。
    auto namedMat = BuildNamedMaterialInstances(editorHost.assets);
    Orange::Editor::Schema::SetAssetRegistryForSchema(
        editorHost.assets.pAssets.get());
    Orange::Editor::Schema::SetNamedMaterialInstancesForSchema(&namedMat);
    {
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
    }

    // ---- Layer 注入 -----------------------------------------------------
    host->PushLayer(std::make_unique<EditorRenderLayer>(*host, *pRenderDevice,
                                                        *pRenderer,
                                                        imguiDescPool, vkDevice,
                                                        editorHost));

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
    glfwSetWindowUserPointer(glfwWindow, &editorHost);
    glfwSetWindowCloseCallback(glfwWindow, [](GLFWwindow* w)
    {
        auto* pHost = static_cast<EditorHost*>(glfwGetWindowUserPointer(w));
        if (pHost == nullptr) { return; }
        if (pHost->scene.dirty)
        {
            glfwSetWindowShouldClose(w, GLFW_FALSE);
            if (pHost->scene.pendingCloseAction == PendingCloseAction::None)
            {
                pHost->scene.pendingCloseAction = PendingCloseAction::Exit;
            }
        }
    });

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
