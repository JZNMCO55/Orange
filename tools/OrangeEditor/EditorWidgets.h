#ifndef ORANGE_EDITOR_EDITOR_WIDGETS_H
#define ORANGE_EDITOR_EDITOR_WIDGETS_H

// 编辑器复用 ImGui 控件 —— Inspector 各组件区块用、未来 Settings / 其它
// 面板也可能用。本头不依赖任何引擎类型，只对 ImGui 调用，纯 UI 工具。

// ---- Inspector property 两列表格布局帮手 (v0.4.5) ---------------------
//
// 解决 v0.4 收尾撞上的 Inspector 字段名截断（"Sta..." / "Par..."）问题：
// ImGui 默认 `DragFloat("Label", v)` 把控件 + label 横向并排，控件吃掉
// CalcItemWidth() 默认 65% 宽度，剩余 35% 给 label —— 窄屏 (1680×1120 ×
// 150% scale) 不够。本帮手把每个 property 段统一改成 ImGui::Table 2 列：
//   * 左列 ImGuiTableColumnFlags_WidthFixed 固定宽，宽 = caller 在
//     BeginPropertyTable 时传入的"本 schema 段最长 label 文本宽"
//   * 右列 ImGuiTableColumnFlags_WidthStretch 占满剩余空间
// 控件本身在右列用 SetNextItemWidth(-FLT_MIN) 占满整列；label 由
// PropertyLabel 在左列单独 TextUnformatted。窄屏下控件自动缩窄，label
// 永不被截断（前提：cell 自身有足够宽度容纳 longest label —— DPI scale
// 由 main.cpp ImGui::GetStyle().ScaleAllSizes(dpiScale) 兜底）。
//
// 调用模式：
//
//   const float maxLabelW = ...;  // CalcTextSize 最长 prop.label
//   if (Orange::Editor::Widgets::BeginPropertyTable("##props.Transform", maxLabelW)) {
//       Orange::Editor::Widgets::PropertyLabel("Position");
//       ImGui::DragFloat3("##Position", &pos.x);     // ## hidden label
//       ...
//       Orange::Editor::Widgets::EndPropertyTable();
//   }
//
// "##idSuffix" 形式的 hidden label 确保 ImGui 控件 ID 唯一（每行 cell 内
// 一个 control，PushID 不必）。
//
// **不要**在 PropertyLabel 与控件之间插入 ImGui::SameLine / Separator——
// PropertyLabel 内部已经 TableSetColumnIndex(1) 让光标在右列起点。

namespace Orange::Editor::Widgets
{

// id：ImGui::BeginTable 的 strID（必须 caller 提供唯一 id，否则同帧多个
// table 会合并）。typical "##props.<typeName>"。
// labelColTextWidth：本段所有 prop.label 中最长那个的 CalcTextSize.x。
// 函数内部加上 FramePadding * 2 + ItemSpacing 作为列实际宽。
// 返回 false 时 caller 必须**不**调 EndPropertyTable（与 ImGui::BeginTable
// 同语义）。
bool BeginPropertyTable(const char* id, float labelColTextWidth);

// 推进到下一行，写左列 label（AlignTextToFramePadding 与右列控件中线对
// 齐），然后跳到右列 + SetNextItemWidth(-FLT_MIN)。下一行 caller 紧跟
// 一个 ImGui 控件即可。
//
// tooltip 非空时 hover 左列 label 触发 SetTooltip；用户对 "鼠标移到字段
// 名上看说明" 的预期比 "鼠标停在控件上看说明" 更稳定（控件可能在拖动 /
// 编辑状态，hover 行为被打断）。
void PropertyLabel(const char* label, const char* tooltip = nullptr);

void EndPropertyTable();

}  // namespace Orange::Editor::Widgets

// 三色 X/Y/Z 标签 + 3 个 DragFloat 的组合控件，对齐 Unity Transform 的
// 配色（X 红 / Y 绿 / Z 蓝）。比裸 DragFloat3 多视觉占用：每分量前一
// 个有色 Button 当 label —— Button 是装饰，点击吃掉但无副作用（不进
// 入键盘焦点队列）。
//
// v0.4.5 起：本控件不再自带右侧文本 label —— 调用方必须在 PropertyTable
// 左列已经写过 label（用 Widgets::PropertyLabel）。idSuffix 仅用于
// PushID 区分多个相邻 DragVec3Colored 的 ImGui ID（典型 "Position" /
// "Rotation" / "Scale"），不再显示出来。控件横向宽 = 当前 cell
// GetContentRegionAvail().x —— Table 右列 stretch 后给到的 cell 宽。
//
// 用 PushID(idSuffix) 隔离三个内部 DragFloat 的 ImGui ID；外层调用方按需
// 再包 PushID（Inspector 同一 Window 内同名字段不出现，目前不必）。
//
// 返回值：任一分量被改 → true，调用方一般写回 component 字段即可。
bool DragVec3Colored(const char* idSuffix, float v[3],
                     float speed = 0.1f,
                     float vMin  = 0.0f,
                     float vMax  = 0.0f,
                     const char* fmt = "%.3f");

#endif  // ORANGE_EDITOR_EDITOR_WIDGETS_H
