// EditorWidgets 实现 —— 见 EditorWidgets.h 的注释。

#include "EditorWidgets.h"

#include <imgui.h>

#include <cfloat>
#include <cstdio>

namespace Orange::Editor::Widgets
{

bool BeginPropertyTable(const char* id, float labelColTextWidth)
{
    const ImGuiStyle& s = ImGui::GetStyle();
    // SizingFixedFit 让左列吃 WidthFixed，右列吃 WidthStretch；NoPadOuterX
    // 让 table 紧贴 CollapsingHeader 缩进位置，不引入额外左缩进。
    constexpr ImGuiTableFlags kFlags =
          ImGuiTableFlags_SizingFixedFit
        | ImGuiTableFlags_NoPadOuterX
        | ImGuiTableFlags_NoSavedSettings;
    if (!ImGui::BeginTable(id, 2, kFlags)) { return false; }

    // 左列宽 = max(label text width) + FramePadding * 2 + ItemSpacing。
    // FramePadding 是 caller 在 PropertyLabel 内可能 AlignTextToFramePadding
    // 后的左右内边距；ItemSpacing 给 label 与右列控件之间留呼吸。
    const float labelColW =
        labelColTextWidth + s.FramePadding.x * 2.0f + s.ItemSpacing.x;
    ImGui::TableSetupColumn("##label",   ImGuiTableColumnFlags_WidthFixed,   labelColW);
    ImGui::TableSetupColumn("##control", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    return true;
}

void PropertyLabel(const char* label, const char* tooltip)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    // 让 label 与右列控件的 FramePadding 中线对齐 —— 控件高 FrameHeight，
    // label 是纯文本，不 align 的话 label 会贴 cell 顶（视觉错位）。
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label != nullptr ? label : "?");
    if (tooltip != nullptr && ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", tooltip);
    }

    ImGui::TableSetColumnIndex(1);
    // 让紧跟的下一个 ImGui 控件占满右列。-FLT_MIN 是 ImGui 习语 = "占满
    // 当前 ContentRegion 减去右 padding"；用 -1.0f 在某些 cell 边界会被
    // 当成无效值。
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void EndPropertyTable()
{
    ImGui::EndTable();
}

}  // namespace Orange::Editor::Widgets

bool DragVec3Colored(const char* idSuffix, float v[3],
                     float speed,
                     float vMin,
                     float vMax,
                     const char* fmt)
{
    constexpr ImVec4 kRedX  {0.70f, 0.18f, 0.18f, 1.0f};
    constexpr ImVec4 kGrnY  {0.27f, 0.55f, 0.27f, 1.0f};
    constexpr ImVec4 kBluZ  {0.18f, 0.36f, 0.70f, 1.0f};

    bool changed = false;
    ImGui::PushID(idSuffix);

    const ImGuiStyle& s = ImGui::GetStyle();
    const float btnH    = ImGui::GetFrameHeight();

    // GetContentRegionAvail —— 当前 cell（或 ContentRegion）剩余可用宽。
    // 在 PropertyTable 右列里 = 右列 stretch 后的 cell 宽；在裸 Window 里
    // = 行剩余宽。比 CalcItemWidth() 更直观，不依赖 PushItemWidth 状态。
    const float total = ImGui::GetContentRegionAvail().x;
    // 三轴布局：[btnX][drag][space][btnY][drag][space][btnZ][drag]
    //   button 宽   = btnH（正方形）
    //   inner space = ItemInnerSpacing.x（button 与 drag 间）
    //   axis space  = ItemInnerSpacing.x（axis 之间，比 ItemSpacing 紧凑）
    // 3 个 axis = 3*(btn + inner + drag) + 2*axisSpace
    const float axisSpace = s.ItemInnerSpacing.x;
    const float dragW =
        (total - 3.0f * (btnH + s.ItemInnerSpacing.x) - 2.0f * axisSpace) / 3.0f;

    auto axis = [&](int idx, const char* name, ImVec4 color) {
        ImGui::PushStyleColor(ImGuiCol_Button,        color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  color);
        ImGui::Button(name, ImVec2(btnH, btnH));
        ImGui::PopStyleColor(3);
        ImGui::SameLine(0.0f, s.ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(dragW);
        char id[8];
        std::snprintf(id, sizeof(id), "##%s", name);
        if (ImGui::DragFloat(id, &v[idx], speed, vMin, vMax, fmt)) {
            changed = true;
        }
    };

    axis(0, "X", kRedX);
    ImGui::SameLine(0.0f, axisSpace);
    axis(1, "Y", kGrnY);
    ImGui::SameLine(0.0f, axisSpace);
    axis(2, "Z", kBluZ);

    ImGui::PopID();
    return changed;
}
