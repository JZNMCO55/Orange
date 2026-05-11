// EditorWidgets 实现 —— 见 EditorWidgets.h 的注释。

#include "EditorWidgets.h"

#include <imgui.h>

#include <cstdio>

bool DragVec3Colored(const char* label, float v[3],
                     float speed,
                     float vMin,
                     float vMax,
                     const char* fmt)
{
    constexpr ImVec4 kRedX  {0.70f, 0.18f, 0.18f, 1.0f};
    constexpr ImVec4 kGrnY  {0.27f, 0.55f, 0.27f, 1.0f};
    constexpr ImVec4 kBluZ  {0.18f, 0.36f, 0.70f, 1.0f};

    bool changed = false;
    ImGui::PushID(label);

    const ImGuiStyle& s = ImGui::GetStyle();
    const float btnH    = ImGui::GetFrameHeight();
    // CalcItemWidth：当前 column 下默认 item 宽度（ImGui 自适应窗口宽）
    const float total   = ImGui::CalcItemWidth();
    const float dragW   = (total - 3.0f * (btnH + s.ItemInnerSpacing.x)) / 3.0f;

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
    ImGui::SameLine(0.0f, s.ItemInnerSpacing.x);
    axis(1, "Y", kGrnY);
    ImGui::SameLine(0.0f, s.ItemInnerSpacing.x);
    axis(2, "Z", kBluZ);
    ImGui::SameLine(0.0f, s.ItemInnerSpacing.x);
    ImGui::TextUnformatted(label);

    ImGui::PopID();
    return changed;
}
