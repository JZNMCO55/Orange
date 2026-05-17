// EditorTheme c1：骨架实现。每个 getter 返回一个 file-scope const 占位
// 值；`ApplyToImGui()` 空函数。视觉零变化（main.cpp 仍走
// ImGui::StyleColorsDark）。完整论证见 EditorTheme.h 顶部注释 + editor-
// roadmap §D5.1。
//
// 实现选择：file-scope `const ImVec4 kXxx = ...` + getter 返回 `const&`。
//
// 为什么 const 而非 constexpr：ImVec4 / ImVec2 在 ImGui 1.91 的构造函数
// 不是 constexpr，无法做 constexpr 全局。运行时初始化（静态存储区一次性
// 构造）足够，编辑器场景对启动期 ~µs 量级开销不敏感。
//
// 为什么 file-scope const 而非 inline static：减少符号污染 + 便于 v0.8
// EditorSettings 整骨时把这些 const 替换为对 Settings 的查询调用（getter
// 内部行从 `return kXxx;` 变成 `return Settings::Get<ImVec4>("...");`）。

#include "EditorTheme.h"

namespace Orange::Editor::Theme
{

// ---- Color ----------------------------------------------------------

namespace Color
{

namespace
{

// c1 占位：与 ImGui StyleColorsDark 的 ImGuiCol_WindowBg 等价值（约 #15151E）。
// c2 采色替换为 Cocos #2A2A2A 区间。
const ImVec4 kBackgroundPrimary{0.10f, 0.10f, 0.12f, 1.00f};

// brand accent：OrangeEngine 橙 #FF8A3D。这是已知最终值（§D5.1 决策已
// 拍板），c4 前不会被消费但常量定义已就位。
const ImVec4 kAccentPrimary{1.000f, 0.541f, 0.239f, 1.000f};

}  // namespace

const ImVec4& GetBackgroundPrimary() { return kBackgroundPrimary; }
const ImVec4& GetAccentPrimary()     { return kAccentPrimary;     }

}  // namespace Color

// ---- Spacing --------------------------------------------------------

namespace Spacing
{

namespace
{

// c1 占位 = ImGui 默认 ImVec2(8, 8)。c2 校准（可能保留默认或微调）。
const ImVec2 kWindowPadding{8.0f, 8.0f};

}  // namespace

const ImVec2& GetWindowPadding() { return kWindowPadding; }

}  // namespace Spacing

// ---- Rounding -------------------------------------------------------

namespace Rounding
{

// c1 占位 = 0.0f（Cocos 方正风格）。c2 可能保留 0 或上调到 1px。
float GetFrame() { return 0.0f; }

}  // namespace Rounding

// ---- Font -----------------------------------------------------------

namespace Font
{

// c1 占位 = 18.0f（与 main.cpp::kDesignFontSizePx 一致）。注意：本值是
// **设计基准**像素，实际加载到 atlas 的字号是 18 * DPI scale。返回值
// 不含 scale—调用方按 ImGui 默认 widget 渲染规则使用即可（FramePadding
// 已 ScaleAllSizes，行高自动正确）。
float GetSizeBody() { return 18.0f; }

}  // namespace Font

// ---- Icon -----------------------------------------------------------

namespace Icon
{

// c1 占位 = 裸文字 label。c3 接入 Codicons 后切到对应 codepoint string
// （IconsCodicons.h 提供的 ICON_CI_* 宏展开成 UTF-8 字节序列）。
const char* GetSave() { return "Save"; }
const char* GetPlay() { return "Play"; }

}  // namespace Icon

// ---- ComponentTypeBand ----------------------------------------------

namespace ComponentTypeBand
{

namespace
{

// c1 占位：Transform 色带 = 低饱和绿（与 Y 轴 gizmo 同色系联想"位置 /
// 朝向"）。c5 校色定终值。
const ImVec4 kTransform{0.45f, 0.65f, 0.40f, 1.00f};

}  // namespace

float          GetBandWidthPx() { return 4.0f; }
const ImVec4&  GetTransform()   { return kTransform; }

}  // namespace ComponentTypeBand

// ---- 顶层 API -------------------------------------------------------

void ApplyToImGui()
{
    // c1 占位：空函数。视觉零变化，main.cpp 仍走 ImGui::StyleColorsDark。
    //
    // c2 起灌入：
    //   ImGuiStyle& style = ImGui::GetStyle();
    //   style.Colors[ImGuiCol_WindowBg] = Color::GetBackgroundPrimary();
    //   style.WindowPadding             = Spacing::GetWindowPadding();
    //   style.FrameRounding             = Rounding::GetFrame();
    //   ... (完整 ImGuiCol_ 枚举映射 + style 字段)
    //
    // c4 追加 accent 橙 + 半透 selection 系列。
}

}  // namespace Orange::Editor::Theme
