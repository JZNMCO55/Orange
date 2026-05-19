#include "AnimFsmAssetInspectorPlugin.h"

#include "../AnimFsmFileIO.h"
#include "../EditorHost.h"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace Orange::Editor::Plugin
{

namespace
{

// 节点矩形固定尺寸 —— c2-4 简化阶段不按 label 长度伸缩；c2-5 接 Rename
// 落地后如果长名字撑爆再扩。
constexpr float kNodeWidth     = 140.0f;
constexpr float kNodeHeight    = 44.0f;
constexpr float kNodeRounding  = 4.0f;
constexpr float kCanvasHeight  = 360.0f;

// canvas 内坐标 = mEditingFsm.states[i].layoutX/Y；屏幕坐标 = canvas
// 起点 + canvas 内坐标。当前不实现 pan / zoom（c2 期不在 critical path
// 上；Lumix 自己也只做 pan 不做 zoom，留 c3+ 按需补）。

}  // anonymous namespace

bool AnimFsmAssetInspectorPlugin::CanHandle(const std::string& assetPath) const
{
    // ".anim_fsm" = 9 chars；短 / 空 path 不命中。
    if (assetPath.size() < 9) { return false; }
    return assetPath.compare(assetPath.size() - 9, 9, ".anim_fsm") == 0;
}

void AnimFsmAssetInspectorPlugin::EnsureEditingCache(const std::string& assetPath)
{
    if (mEditingPath == assetPath) { return; }

    auto opt = ::Orange::Editor::AnimFsm::ReadAnimFsmFile(assetPath);
    if (opt.has_value())
    {
        mEditingFsm   = std::move(*opt);
        mEditingValid = true;
    }
    else
    {
        mEditingFsm   = {};
        mEditingValid = false;
    }
    mEditingPath = assetPath;
    mSelectedStateName.clear();
}

void AnimFsmAssetInspectorPlugin::DrawCanvas()
{
    // canvas child window：固定高度，自适应宽度；border = true 给一个视觉
    // 边界让 canvas 区域与外层 Inspector 区分。child 内 ImDrawList 绘制
    // + InvisibleButton 命中检测同一坐标系（screen-space）。
    ImGui::BeginChild("##anim_fsm_canvas", ImVec2(0.0f, kCanvasHeight), true);

    ImDrawList*  dl           = ImGui::GetWindowDrawList();
    const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();

    // 拿 ImGui 内置 color token —— 与 EditorTheme.cpp 的 ImGui style 覆盖
    // 路径联动（避免新增"节点图专用"硬编码 RGBA；v0.6.5 视觉体系基准）。
    const ImU32 fillColor          = ImGui::GetColorU32(ImGuiCol_FrameBg);
    const ImU32 borderDefaultColor = ImGui::GetColorU32(ImGuiCol_Border);
    const ImU32 borderHoverColor   = ImGui::GetColorU32(ImGuiCol_HeaderHovered);
    const ImU32 borderSelectColor  = ImGui::GetColorU32(ImGuiCol_HeaderActive);
    const ImU32 textColor          = ImGui::GetColorU32(ImGuiCol_Text);

    for (auto& state : mEditingFsm.states)
    {
        const ImVec2 nodeMin = ImVec2(canvasOrigin.x + state.layoutX,
                                      canvasOrigin.y + state.layoutY);
        const ImVec2 nodeMax = ImVec2(nodeMin.x + kNodeWidth,
                                      nodeMin.y + kNodeHeight);

        // hit-test：把 cursor 设到节点位置 + InvisibleButton 覆盖节点矩形。
        // PushID 用 state.name 保证多节点 id 唯一；c2-5 Rename 命令落地
        // 时需要 state name 不重名（reader 端 c2-2 已 enforce），同一帧
        // 内不可能有同名节点。
        ImGui::SetCursorScreenPos(nodeMin);
        ImGui::PushID(state.name.c_str());
        ImGui::InvisibleButton("##node", ImVec2(kNodeWidth, kNodeHeight));

        const bool hovered = ImGui::IsItemHovered();
        const bool active  = ImGui::IsItemActive();

        // 拖动：active（鼠标按下）+ 实际在拖（避免点击瞬间触发）→ 累加
        // delta 到 layout。这条改动**只**改 in-memory 副本，不写盘 /
        // 不入命令栈；Save + Undo 在 c2-5 落地。
        if (active && ImGui::IsMouseDragging(0))
        {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            state.layoutX += delta.x;
            state.layoutY += delta.y;
        }

        // 选中：单击命中即设选中（重复点击同节点不变化）。多选 c2-5。
        if (ImGui::IsItemClicked(0))
        {
            mSelectedStateName = state.name;
        }

        const bool selected = (state.name == mSelectedStateName);

        const ImU32 borderColor =
            selected ? borderSelectColor
                     : hovered ? borderHoverColor : borderDefaultColor;
        const float borderThickness = selected ? 2.5f : 1.0f;

        dl->AddRectFilled(nodeMin, nodeMax, fillColor, kNodeRounding);
        dl->AddRect(nodeMin, nodeMax, borderColor, kNodeRounding,
                    ImDrawFlags_None, borderThickness);

        // label 居中。state name 过长时 ImGui::CalcTextSize 会反映真实
        // 宽度；目前不做截断（c2-5 Rename 时按需补 ellipsis）。
        const ImVec2 textSize = ImGui::CalcTextSize(state.name.c_str());
        const ImVec2 textPos  = ImVec2(
            nodeMin.x + (kNodeWidth - textSize.x) * 0.5f,
            nodeMin.y + (kNodeHeight - textSize.y) * 0.5f);
        dl->AddText(textPos, textColor, state.name.c_str());

        ImGui::PopID();
    }

    // 空 canvas 时给一行提示，避免用户以为加载失败
    if (mEditingFsm.states.empty())
    {
        const ImVec2 hintPos = ImVec2(canvasOrigin.x + 8.0f,
                                      canvasOrigin.y + 8.0f);
        dl->AddText(hintPos, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    "(empty FSM —— 状态机为空；c2-5 接 Add State 后可加节点)");
    }

    ImGui::EndChild();
}

void AnimFsmAssetInspectorPlugin::DrawTables() const
{
    if (ImGui::CollapsingHeader("States table"))
    {
        if (ImGui::BeginTable("##anim_fsm_states", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Clip");
            ImGui::TableSetupColumn("LayoutX");
            ImGui::TableSetupColumn("LayoutY");
            ImGui::TableHeadersRow();
            for (const auto& s : mEditingFsm.states)
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

    if (ImGui::CollapsingHeader("Transitions table"))
    {
        if (ImGui::BeginTable("##anim_fsm_transitions", 2,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("From");
            ImGui::TableSetupColumn("To");
            ImGui::TableHeadersRow();
            for (const auto& t : mEditingFsm.transitions)
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
}

void AnimFsmAssetInspectorPlugin::Draw(EditorHost& /*host*/,
                                       const std::string& assetPath)
{
    EnsureEditingCache(assetPath);

    ImGui::TextDisabled("Anim FSM:");
    ImGui::SameLine();
    ImGui::TextUnformatted(assetPath.c_str());
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", assetPath.c_str());
    }
    ImGui::Separator();

    if (!mEditingValid)
    {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
                           "无法读取 .anim_fsm 文件（详见 stderr）");
        return;
    }

    // 统计行 + Selected
    ImGui::Text("States:        %zu", mEditingFsm.states.size());
    ImGui::Text("Transitions:   %zu", mEditingFsm.transitions.size());
    ImGui::Text("Initial state: %s",
                mEditingFsm.initialState.empty()
                    ? "(unset)"
                    : mEditingFsm.initialState.c_str());
    ImGui::Text("Selected:      %s",
                mSelectedStateName.empty() ? "(none)" : mSelectedStateName.c_str());
    ImGui::Separator();

    DrawCanvas();

    ImGui::Separator();
    DrawTables();

    ImGui::Separator();
    ImGui::TextDisabled("c2-4 scope：节点矩形 + 拖动 + 选中（in-memory）");
    ImGui::TextDisabled("Save / Undo / Add / Delete / Rename 在 c2-5 落地；边在 c2-6 落地");
}

}  // namespace Orange::Editor::Plugin
