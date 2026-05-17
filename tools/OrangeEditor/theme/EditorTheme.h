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
// c2 落 Cocos 3.8.8 采色（背景 / 控件 / header / menu / 字色 / 分隔）；
// c4 追加 accent 橙 + 半透 selection；c6 追加 alert 系（warn / error /
// success）。
namespace Color
{

// 主 dock space 背景（最深一档炭灰）。c2 = #242424 (~14% 亮度)，与
// §D5.1 决策 #2A2A2A ~ #2C2C2C 范围一致（略偏深以让 panel 与 dock 有
// 视觉分层）。
const ImVec4& GetBackgroundPrimary();

// panel 内容背景（中等炭灰）。c2 = #2E2E2E (~18% 亮度)，比主背景亮
// 一档让 panel 边界明确，对应 Cocos 各 panel 内容区域。
const ImVec4& GetBackgroundSecondary();

// popup / tooltip 背景（最亮一档炭灰）。c2 = #363636 (~21%)，让 popup
// 比主 UI 浮出来。
const ImVec4& GetBackgroundTertiary();

// 输入框 / Combo / DragFloat 控件 idle 态背景。c2 = #1E1E1E (~12%)，
// 比主背景更深让输入框轮廓自然显现，对应 Cocos Inspector 字段输入框
// 的"凹陷"视觉。
const ImVec4& GetControlBg();

// 控件 hover 态。c2 = #3C3C3C (~24%)。
const ImVec4& GetControlBgHovered();

// 控件 active 态（按下 / 点击瞬间）。c2 = #464646 (~28%)。
const ImVec4& GetControlBgActive();

// component header / 折叠区块 / TreeNode / Selectable idle 态。c2 =
// #363636 (~21%)，与 popup 背景同档，让 header 在 panel 内"突起"。
const ImVec4& GetHeaderBg();

// header hover 态。c2 = #464646 (~28%)。
const ImVec4& GetHeaderBgHovered();

// header active 态（选中 / 展开瞬间）。c2 = #555555 (~33%)。
const ImVec4& GetHeaderBgActive();

// menu bar / toolbar 背景。c2 = #282828 (~16%)，介于主背景与 panel 之间。
const ImVec4& GetMenuBarBg();

// 主字色（普通文本）。c2 = #DCDCDC (~86%)，浅灰非纯白，长时间盯不疲劳。
const ImVec4& GetTextPrimary();

// 次字色（label / hint）。c2 = #A0A0A0 (~63%)。
const ImVec4& GetTextSecondary();

// 禁用字色（disabled 控件 label）。c2 = #6E6E6E (~43%)。
const ImVec4& GetTextDisabled();

// 分隔线 / border（深到几乎隐形，仅作为视觉断点）。c2 = #141414 (~8%)。
const ImVec4& GetSeparator();
const ImVec4& GetBorder();

// brand accent 色（橙）#FF8A3D，alpha 1.0。c4 起用于 focus outline /
// active tab 下划线 / Save dirty 高亮等"小面积高对比"位置。**绝不用于
// 大面积实色填充**——大色块走 `GetAccentSelection()` 半透。
const ImVec4& GetAccentPrimary();

// accent hover：橙 50% alpha。c4 起用于 ImGuiCol_HeaderHovered /
// ButtonHovered（dirty Save）/ TabHovered 等"鼠标悬停高亮"。
const ImVec4& GetAccentHovered();

// accent active：橙 60% alpha。c4 起用于 ImGuiCol_HeaderActive /
// ButtonActive（dirty Save 按下瞬间）/ TabActive 当前激活态。
const ImVec4& GetAccentActive();

// accent selection：橙 30% alpha 半透叠加。c4 起用于 ImGuiCol_Header
// （TreeNode / Selectable 选中行整行染色），避免大面积饱和橙刺眼。
// §D5.1 决策点："selection 用半透叠加而非整行实色填充"的落地。
const ImVec4& GetAccentSelection();

}  // namespace Color

// ---- Spacing --------------------------------------------------------
// 间距 / padding。c2 时基于 ImGui 默认值（经 main.cpp ScaleAllSizes 已
// 按 DPI 缩放过）作为基线，仅在 Cocos 风格需要时微调；本期不动 DPI
// scale 路径。
namespace Spacing
{

// 窗口内边距。c2 = ImVec2(8, 8)（ImGui 默认 + ScaleAllSizes 后基线）。
const ImVec2& GetWindowPadding();

// 控件内边距（frame 内文本与边的间距）。c2 = ImVec2(6, 4)。
const ImVec2& GetFramePadding();

// item 间距（同行 widget 间 / 行间）。c2 = ImVec2(8, 4)，比 ImGui 默认
// 略紧（4 → 4 不变 / 8 → 8 不变，实际不动）。
const ImVec2& GetItemSpacing();

// item 内部子元素间距（如 checkbox label 与方框之间）。c2 = ImVec2(4, 4)。
const ImVec2& GetItemInnerSpacing();

// TreeNode 缩进单位。c2 = 18.0f（与字号 baseline 一致）。
float GetIndentSpacing();

}  // namespace Spacing

// ---- Rounding -------------------------------------------------------
// 圆角半径。Cocos 工具感方向 → 全部走 0px（方正风格）。整套 Rounding
// token 全部返回 0.0f；如 Cocos 截图细看出某处微圆角，c6 polish 时
// 再微调。
namespace Rounding
{

// 窗口圆角。Cocos = 0px（与多视口 detached window 走 0 同款一致性）。
float GetWindow();

// 控件圆角（Button / Frame / Combo）。c2 = 0.0f。
float GetFrame();

// popup / tooltip 圆角。c2 = 0.0f。
float GetPopup();

// scrollbar 圆角。c2 = 0.0f。
float GetScrollbar();

// slider grab 圆角。c2 = 0.0f。
float GetGrab();

// tab 圆角。c2 = 0.0f。
float GetTab();

// child window 圆角。c2 = 0.0f。
float GetChild();

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

// 顶部 toolbar Save 按钮 icon。c3 = ICON_CI_SAVE。
const char* GetSave();

// 顶部 toolbar Play 按钮 icon。c3 = ICON_CI_DEBUG_START（VS Code
// transport-control 同款三件套之一）。
const char* GetPlay();

// c4 扩：toolbar / panel 通用按钮 icon。
const char* GetPause();    // ICON_CI_DEBUG_PAUSE
const char* GetStop();     // ICON_CI_DEBUG_STOP
const char* GetClose();    // ICON_CI_CLOSE / CHROME_CLOSE，用于 "×" 取消按钮
const char* GetSearch();   // ICON_CI_SEARCH，用于 "Pick" asset 选择
const char* GetAdd();      // ICON_CI_ADD，用于 "+" 新增按钮
const char* GetArrowUp();  // ICON_CI_ARROW_UP，用于 ".." 上级目录
const char* GetFolder();   // ICON_CI_FOLDER
const char* GetGear();     // ICON_CI_GEAR，用于 Settings 入口

}  // namespace Icon

// ---- ComponentTypeBand ----------------------------------------------
// Inspector component header 左侧 4px 色带（方向 C "识别度补丁"）。
// per-component-type 一个低饱和色作为视觉分类标签，与 schema typeName
// 字符串映射；c5 接入 SchemaInspector ComponentHeaderLocal 消费。
//
// 配色原则（低饱和度避免与橙 accent 冲突）：
//   * Transform     —— 绿（位置 / 朝向，对应 Y 轴 gizmo 同色系）
//   * Renderable    —— 蓝（mesh / 几何）
//   * DirectionalLight —— 黄（光源）
//   * RigidBody     —— 红（物理 / 碰撞）
//   * Collider      —— 紫（碰撞形状）
//   * ParticleEmitter —— 青（粒子）
//   * Animator      —— 粉（动画 / 骨骼）
//   * Name          —— 灰（基础元数据）
//   * Default       —— 浅灰 fallback（未注册的 component type）
namespace ComponentTypeBand
{

// 色带宽度（px）。c5 = 4.0f（与 §D5.1 决策一致）。
float GetBandWidthPx();

// 8 个内置 component 色带色。
const ImVec4& GetTransform();
const ImVec4& GetRenderable();
const ImVec4& GetDirectionalLight();
const ImVec4& GetRigidBody();
const ImVec4& GetCollider();
const ImVec4& GetParticleEmitter();
const ImVec4& GetAnimator();
const ImVec4& GetName();

// fallback：未注册的 component type / 游戏侧自定义 component。
const ImVec4& GetDefault();

// 字符串查表：按 schema.typeName 返回对应色带色；不命中返回 GetDefault()。
// 调用方传 schema 顶层 typeName（如 "Transform" / "Renderable" /
// "DirectionalLight"）。
const ImVec4& LookupByTypeName(const char* typeName);

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
