#include "AnimFsmAssetInspectorPlugin.h"

#include "../AnimFsmFileIO.h"
#include "../AnimFsmModel.h"
#include "../EditorHost.h"

#include <imgui.h>

#include <string>

namespace Orange::Editor::Plugin
{

namespace
{

// Anim FSM 子模式主体 —— c2-2 范围：纯展示，无编辑 UI、无 Save 按钮。
// 节点图绘制 + 编辑 + 命令栈 + Condition DSL 在 c2-3 ~ c2-7 逐步展开。
void DrawAnimFsmSubMode(EditorHost& /*host*/, const std::string& animFsmPath)
{
    ImGui::TextDisabled("Anim FSM:");
    ImGui::SameLine();
    ImGui::TextUnformatted(animFsmPath.c_str());
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", animFsmPath.c_str());
    }
    ImGui::Separator();

    auto dataOpt = ::Orange::Editor::AnimFsm::ReadAnimFsmFile(animFsmPath);
    if (!dataOpt.has_value())
    {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
                           "无法读取 .anim_fsm 文件（详见 stderr）");
        return;
    }
    const auto& fsm = *dataOpt;

    // 统计行
    ImGui::Text("States:        %zu", fsm.states.size());
    ImGui::Text("Transitions:   %zu", fsm.transitions.size());
    ImGui::Text("Initial state: %s",
                fsm.initialState.empty() ? "(unset)" : fsm.initialState.c_str());
    ImGui::Separator();

    // States 列表
    if (ImGui::CollapsingHeader("States", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::BeginTable("##anim_fsm_states", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Clip");
            ImGui::TableSetupColumn("LayoutX");
            ImGui::TableSetupColumn("LayoutY");
            ImGui::TableHeadersRow();
            for (const auto& s : fsm.states)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(s.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(s.clipName.empty() ? "(unset)" : s.clipName.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.1f", static_cast<double>(s.layoutX));
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.1f", static_cast<double>(s.layoutY));
            }
            ImGui::EndTable();
        }
    }

    // Transitions 列表
    if (ImGui::CollapsingHeader("Transitions", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::BeginTable("##anim_fsm_transitions", 2,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("From");
            ImGui::TableSetupColumn("To");
            ImGui::TableHeadersRow();
            for (const auto& t : fsm.transitions)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(t.fromState.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(t.toState.c_str());
            }
            ImGui::EndTable();
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("c2-2 scope：仅展示 + round-trip 验证");
    ImGui::TextDisabled("节点图编辑 UI / Save 按钮 / Condition DSL 在 c2-3 ~ c2-6 落地");
}

}  // anonymous namespace

bool AnimFsmAssetInspectorPlugin::CanHandle(const std::string& assetPath) const
{
    // ".anim_fsm" = 9 chars；短 path / 空 path 不命中。
    if (assetPath.size() < 9) { return false; }
    return assetPath.compare(assetPath.size() - 9, 9, ".anim_fsm") == 0;
}

void AnimFsmAssetInspectorPlugin::Draw(EditorHost& host, const std::string& assetPath)
{
    DrawAnimFsmSubMode(host, assetPath);
}

}  // namespace Orange::Editor::Plugin
