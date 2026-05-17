#ifndef ORANGE_EDITOR_THEME_EDITOR_THEME_H
#define ORANGE_EDITOR_THEME_EDITOR_THEME_H

// EditorTheme —— OrangeEditor v0.6.5 视觉体系 token 中心化点。
//
// 决策来源：`docs/editor-roadmap.md` §D5.1（2026-05-17 落锚）。完整背景
// + Cocos / Godot 对比 + 方向 C 论证见该节；本文件仅是落地实现。
//
// 与参考引擎对照（v0.6.5 启动 ritual Step 4 结论）：
//   * Lumix `src/editor/settings.h` —— 用户可配 + Workspace/User 双层
//     storage + GUI 调整 + 持久化。**那是 v0.8 EditorSettings 整骨范围**
//     （消除 L13），v0.6.5 不引入注册系统
//   * Godot `editor/themes/editor_theme_manager.h` —— 761 行 manager +
//     类 Theme 资源 + 多 palette。过重，OrangeEditor 不参照
//   * OrangeEditor v0.6.5 —— **轻量路径**：const + namespace 分组、零
//     注册系统、零用户可配；v0.8 把 token 迁入 EditorSettings 时改 getter
//     实现即可，调用方零改动
//
// 命名约定：
//   * **getter 风格** `GetXxx()` 返回 `const ImVec4&` / `const ImVec2&` /
//     `float` —— 让 v0.8 切 Settings 时改实现而不动调用方
//   * 函数名 = 项目语义（`GetCompTransform`），不绑定具体值——v0.8
//     用户改色后 getter 内部读 Settings 即可
//   * 6 个 sub-namespace 按视觉维度分组：Color / Spacing / Rounding /
//     Font / Icon / ComponentTypeBand
//
// c1 范围（本 commit）：
//   * 6 sub-namespace 骨架建立，每个声明 1–2 个示意性 getter 作为
//     "结构 sample"；其余 getter 由 c2 / c3 / c4 / c5 / c6 增量添加
//   * `ApplyToImGui()` 顶层 API **空函数实现**——视觉零变化，main.cpp
//     仍走 `ImGui::StyleColorsDark()`，c2 才让 main.cpp 切到这里
//
// 后续 commit 增量路径：
//   * c2 —— Color / Spacing / Rounding 全 token + Cocos 3.8.8 采色值 +
//     ApplyToImGui 实装，main.cpp 切到 EditorTheme
//   * c3 —— Icon namespace 接入 Codicons codepoint
//   * c4 —— Color::AccentPrimary 切到橙 #FF8A3D + 半透 selection token
//   * c5 —— ComponentTypeBand 全 8 内置 component 色 + SchemaInspector
//     消费
//   * c6 —— alert 色系（warn / error / success）+ toolbar polish 消费

#include <imgui.h>

namespace Orange::Editor::Theme
{

// ---- Color ----------------------------------------------------------
// RGBA token（背景 / panel / 控件三态 / accent / alert / selection）。
// c1 仅 2 个示意性 getter；完整集合由 c2 / c4 增量添加。
namespace Color
{

// 主 dock space 背景。c1 占位 = ImGui StyleColorsDark 等价深色；c2 采色
// 替换为 Cocos #2A2A2A 区间。
const ImVec4& GetBackgroundPrimary();

// brand accent 色（橙）。c1 占位 = OrangeEngine 主色 #FF8A3D（已知最终
// 值，无需 c2 调整）；c4 起用于 focus outline / active tab 下划线 / Save
// dirty 高亮等"小面积高对比"位置。**绝不用于大面积实色填充**——半透
// selection 由 `GetAccentSelection()`（c4 添加）承担。
const ImVec4& GetAccentPrimary();

}  // namespace Color

// ---- Spacing --------------------------------------------------------
// 间距 / padding。c2 时把当前 ImGui 默认值（经 main.cpp ScaleAllSizes
// 缩放）固化到 token。
namespace Spacing
{

// 窗口内边距。c1 占位 = ImGui 默认 ImVec2(8, 8)；c2 校准。
const ImVec2& GetWindowPadding();

}  // namespace Spacing

// ---- Rounding -------------------------------------------------------
// 圆角半径。Cocos 工具感方向 → 倾向 0 ~ 1px。
namespace Rounding
{

// 控件圆角（Button / Frame / Combo）。c1 占位 = 0.0f（Cocos 方正风格）；
// c2 微调（可能保留 0 或上调到 1px）。
float GetFrame();

}  // namespace Rounding

// ---- Font -----------------------------------------------------------
// 字号档位基于 v0.4.5 DPI scale 18px baseline 派生。具体加载在
// main.cpp ImGui Init 阶段做；本 namespace 仅暴露"想要的尺寸"由调用方
// 查询（如 H1 标题 / Caption 提示文字）。
namespace Font
{

// 正文字号。c1 占位 = 18.0f（与 main.cpp::kDesignFontSizePx 一致）；
// c2 时如有需要派生 H1 / H2 / Caption 三档。
float GetSizeBody();

}  // namespace Font

// ---- Icon -----------------------------------------------------------
// Codicons codepoint 命名映射。**c1 占位返回裸文字字面量**（"Save" /
// "Play" 等），c3 接入 Codicons 后切到对应 codepoint（如 ICON_CI_SAVE
// 宏）。这样 c2 期间编辑器仍能正常显示按钮 label，不依赖 Codicons 字
// 体可用。
namespace Icon
{

// 顶部 toolbar Save 按钮 icon。c1 = "Save"；c3 = ICON_CI_SAVE。
const char* GetSave();

// 顶部 toolbar Play 按钮 icon。c1 = "Play"；c3 = ICON_CI_PLAY。
const char* GetPlay();

}  // namespace Icon

// ---- ComponentTypeBand ----------------------------------------------
// Inspector component header 左侧 4px 色带（方向 C "识别度补丁"）。
// per-component-type 一个低饱和色，c5 接入 SchemaInspector 消费。
namespace ComponentTypeBand
{

// 色带宽度（px）。c1 = 4.0f（与 §D5.1 决策一致）。
float GetBandWidthPx();

// Transform component 色带色。c1 占位 = 低饱和绿（与 Y 轴 gizmo 同色系，
// 用户可联想"位置/朝向"）；c5 校色。
const ImVec4& GetTransform();

}  // namespace ComponentTypeBand

// ---- 顶层 API -------------------------------------------------------

// 一次性把 EditorTheme 所有 token 灌进 `ImGui::GetStyle()`。
//
// c1 实现 = **空函数**（视觉零变化，main.cpp 仍走 ImGui::StyleColorsDark）。
// c2 起填实：push Color / Spacing / Rounding 全 token 进 ImGui style；
// 同 commit 让 main.cpp ImGui Init 段把 StyleColorsDark 调用替换为
// ApplyToImGui。
//
// 调用时机：ImGui::CreateContext 之后、第一帧 BeginFrame 之前调一次。
// per-frame 临时覆盖（如 Save dirty 高亮的 PushStyleColor）仍由调用点
// 自行处理，本 API 不接管。
void ApplyToImGui();

}  // namespace Orange::Editor::Theme

#endif  // ORANGE_EDITOR_THEME_EDITOR_THEME_H
