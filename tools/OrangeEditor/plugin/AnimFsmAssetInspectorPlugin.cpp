#include "AnimFsmAssetInspectorPlugin.h"

#include "../AnimFsmFileIO.h"
#include "../EditorHost.h"
#include "../command/AnimFsmCommands.h"
#include "../theme/EditorTheme.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <memory>
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

// popup id 常量
constexpr const char* kAddStatePopupId = "##anim_fsm_add_state_popup";
constexpr const char* kNodePopupId     = "##anim_fsm_node_context_popup";

// 派生：popup 内 InputText 控件宽度。"reference string" 12 字符宽 +
// 两侧 FramePadding —— 与 LayersPanel.cpp 的"按字符宽派生 inputWidth"
// 同款工业模式，避开字面量像素。
float PopupInputWidth()
{
    return ImGui::CalcTextSize("XXXXXXXXXXXX").x
           + ImGui::GetStyle().FramePadding.x * 2.0f;
}

// 把 C 字符串 buffer 安全 copy 到一个 char[N]（不溢出 + 末尾置 \0）。
void SafeCopyToBuffer(char* dst, std::size_t cap, const std::string& src)
{
    if (cap == 0) { return; }
    const std::size_t n = std::min(cap - 1, src.size());
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

}  // anonymous namespace

bool AnimFsmAssetInspectorPlugin::CanHandle(const std::string& assetPath) const
{
    if (assetPath.size() < 9) { return false; }
    return assetPath.compare(assetPath.size() - 9, 9, ".anim_fsm") == 0;
}

void AnimFsmAssetInspectorPlugin::EnsureEditingCache(EditorHost& host,
                                                    const std::string& assetPath)
{
    if (mEditingPath == assetPath) { return; }

    // 切文件 = 旧 .anim_fsm 的命令在新 fsm 上 Undo 会按 state name 乱跑，
    // 必须 Clear 命令栈（与切 Scene 同款纪律）。命令栈是全局的（CommandStack
    // 是 EditorHost 值成员，单实例），所以 Clear 会清掉其他 panel 已 push
    // 的命令——这是 c2-5 接受的工程妥协，与 EditorSceneContext::pWorld
    // swap 时调 host.cmdStack.Clear() 的语义对齐。
    host.cmdStack.Clear();

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
    mDraggingStateName.clear();
    mDirty = false;
}

void AnimFsmAssetInspectorPlugin::SaveToDisk()
{
    if (mEditingPath.empty() || !mEditingValid) { return; }
    if (::Orange::Editor::AnimFsm::WriteAnimFsmFile(mEditingPath, mEditingFsm))
    {
        mDirty = false;
    }
}

void AnimFsmAssetInspectorPlugin::DrawCanvas(EditorHost& host)
{
    ImGui::BeginChild("##anim_fsm_canvas", ImVec2(0.0f, kCanvasHeight), true);

    ImDrawList*  dl           = ImGui::GetWindowDrawList();
    const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize   = ImGui::GetContentRegionAvail();

    // background hit-target：覆盖整个 canvas 区域。先注册（先画）→ ImGui
    // hit-test 反序判定 → 节点 InvisibleButton 后注册优先命中。背景仅在
    // 真空白区命中。
    ImGui::SetCursorScreenPos(canvasOrigin);
    ImGui::InvisibleButton("##canvas_bg", canvasSize);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        // 左键空白 = 取消选中
        mSelectedStateName.clear();
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        // 右键空白 = 触发 Add State popup（鼠标位置 → 新 state 默认 layout）
        const ImVec2 mouse = ImGui::GetMousePos();
        // 把屏幕坐标换回 canvas 内坐标（减 canvasOrigin）；居中节点到鼠标
        const ImVec2 newNodePos = ImVec2(
            mouse.x - canvasOrigin.x - kNodeWidth * 0.5f,
            mouse.y - canvasOrigin.y - kNodeHeight * 0.5f);
        // 记录到独立 popup 字段中（不复用 mDragInitial，避免与节点拖动
        // 跟踪互相覆盖）
        mAddStateBuffer[0] = '\0';
        mPopupSpawnX       = std::max(0.0f, newNodePos.x);
        mPopupSpawnY       = std::max(0.0f, newNodePos.y);
        ImGui::OpenPopup(kAddStatePopupId);
    }

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

        ImGui::SetCursorScreenPos(nodeMin);
        ImGui::PushID(state.name.c_str());
        ImGui::InvisibleButton("##node", ImVec2(kNodeWidth, kNodeHeight));

        const bool hovered      = ImGui::IsItemHovered();
        const bool active       = ImGui::IsItemActive();
        const bool activated    = ImGui::IsItemActivated();
        const bool deactivated  = ImGui::IsItemDeactivated();

        // 拖动跟踪：activated（鼠标按下瞬间）记录初始位置；active 期间
        // 累加 delta；deactivated（鼠标松开）push MoveStateCommand
        if (activated)
        {
            mDraggingStateName = state.name;
            mDragInitialX      = state.layoutX;
            mDragInitialY      = state.layoutY;
        }
        if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            state.layoutX += delta.x;
            state.layoutY += delta.y;
        }
        if (deactivated && mDraggingStateName == state.name)
        {
            // 只有实际位置变了才 push（避免单击不拖也 push 空命令）
            if (state.layoutX != mDragInitialX || state.layoutY != mDragInitialY)
            {
                host.cmdStack.Push(std::make_unique<AnimFsmMoveStateCommand>(
                    this, state.name,
                    mDragInitialX, mDragInitialY,
                    state.layoutX, state.layoutY));
            }
            mDraggingStateName.clear();
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            mSelectedStateName = state.name;
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            mSelectedStateName  = state.name;
            mRenameTargetState  = state.name;
            SafeCopyToBuffer(mRenameBuffer, sizeof(mRenameBuffer), state.name);
            ImGui::OpenPopup(kNodePopupId);
        }

        const bool selected = (state.name == mSelectedStateName);
        const ImU32 borderColor =
            selected ? borderSelectColor
                     : hovered ? borderHoverColor : borderDefaultColor;
        const float borderThickness = selected ? 2.5f : 1.0f;

        dl->AddRectFilled(nodeMin, nodeMax, fillColor, kNodeRounding);
        dl->AddRect(nodeMin, nodeMax, borderColor, kNodeRounding,
                    ImDrawFlags_None, borderThickness);

        const ImVec2 textSize = ImGui::CalcTextSize(state.name.c_str());
        const ImVec2 textPos  = ImVec2(
            nodeMin.x + (kNodeWidth - textSize.x) * 0.5f,
            nodeMin.y + (kNodeHeight - textSize.y) * 0.5f);
        dl->AddText(textPos, textColor, state.name.c_str());

        ImGui::PopID();
    }

    if (mEditingFsm.states.empty())
    {
        const ImVec2 hintPos = ImVec2(canvasOrigin.x + 8.0f,
                                      canvasOrigin.y + 8.0f);
        dl->AddText(hintPos, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    "(empty FSM —— 右键空白区 Add State)");
    }

    // popup 必须在同一 ImGui ID stack scope 内调 BeginPopup；放 canvas
    // child 内即可。popup 内容由专门的方法绘制。
    DrawAddStatePopup(host);
    DrawNodeContextPopup(host);

    ImGui::EndChild();
}

void AnimFsmAssetInspectorPlugin::DrawAddStatePopup(EditorHost& host)
{
    if (!ImGui::BeginPopup(kAddStatePopupId)) { return; }

    ImGui::TextUnformatted("Add State");
    ImGui::Separator();
    ImGui::SetNextItemWidth(PopupInputWidth());
    const bool enterPressed = ImGui::InputText("##add_state_name",
                                               mAddStateBuffer,
                                               sizeof(mAddStateBuffer),
                                               ImGuiInputTextFlags_EnterReturnsTrue);

    const std::string trimmedName = mAddStateBuffer;
    const bool        nameValid   = !trimmedName.empty();
    const bool        nameClash   =
        nameValid
        && std::find_if(mEditingFsm.states.begin(), mEditingFsm.states.end(),
            [&trimmedName](const ::Orange::Editor::AnimFsm::EditableState& s)
            { return s.name == trimmedName; })
            != mEditingFsm.states.end();

    if (nameClash)
    {
        ImGui::TextColored(Orange::Editor::Theme::Color::GetAlertError(),
                           "name '%s' 已存在", trimmedName.c_str());
    }

    ImGui::BeginDisabled(!nameValid || nameClash);
    const bool addClicked = ImGui::Button("Add") || enterPressed;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        ImGui::CloseCurrentPopup();
    }

    if (addClicked && nameValid && !nameClash)
    {
        host.cmdStack.Push(std::make_unique<AnimFsmAddStateCommand>(
            this, trimmedName, mPopupSpawnX, mPopupSpawnY));
        mSelectedStateName = trimmedName;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void AnimFsmAssetInspectorPlugin::DrawNodeContextPopup(EditorHost& host)
{
    if (!ImGui::BeginPopup(kNodePopupId)) { return; }

    ImGui::Text("State: %s", mRenameTargetState.c_str());
    ImGui::Separator();

    ImGui::TextDisabled("Rename:");
    ImGui::SetNextItemWidth(PopupInputWidth());
    const bool enterPressed = ImGui::InputText("##rename_state",
                                               mRenameBuffer,
                                               sizeof(mRenameBuffer),
                                               ImGuiInputTextFlags_EnterReturnsTrue);
    const std::string newName  = mRenameBuffer;
    const bool        newValid = !newName.empty();
    const bool        sameName = (newName == mRenameTargetState);
    const bool        nameClash =
        newValid && !sameName
        && std::find_if(mEditingFsm.states.begin(), mEditingFsm.states.end(),
            [&newName](const ::Orange::Editor::AnimFsm::EditableState& s)
            { return s.name == newName; })
            != mEditingFsm.states.end();

    if (nameClash)
    {
        ImGui::TextColored(Orange::Editor::Theme::Color::GetAlertError(),
                           "name '%s' 已存在", newName.c_str());
    }

    ImGui::BeginDisabled(!newValid || sameName || nameClash);
    const bool renameClicked = ImGui::Button("Rename") || enterPressed;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Delete State"))
    {
        host.cmdStack.Push(std::make_unique<AnimFsmDeleteStateCommand>(
            this, mRenameTargetState));
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        ImGui::CloseCurrentPopup();
    }

    if (renameClicked && newValid && !sameName && !nameClash)
    {
        host.cmdStack.Push(std::make_unique<AnimFsmRenameStateCommand>(
            this, mRenameTargetState, newName));
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
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

void AnimFsmAssetInspectorPlugin::Draw(EditorHost& host,
                                       const std::string& assetPath)
{
    EnsureEditingCache(host, assetPath);

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
        ImGui::TextColored(Orange::Editor::Theme::Color::GetAlertError(),
                           "无法读取 .anim_fsm 文件（详见 stderr）");
        return;
    }

    // 顶部 toolbar：Save 按钮 + dirty 指示
    ImGui::BeginDisabled(!mDirty);
    if (ImGui::Button("Save"))
    {
        SaveToDisk();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (mDirty)
    {
        ImGui::TextColored(Orange::Editor::Theme::Color::GetAlertWarn(),
                           "* unsaved changes");
    }
    else
    {
        ImGui::TextDisabled("(in sync with disk)");
    }
    ImGui::Separator();

    ImGui::Text("States:        %zu", mEditingFsm.states.size());
    ImGui::Text("Transitions:   %zu", mEditingFsm.transitions.size());
    ImGui::Text("Initial state: %s",
                mEditingFsm.initialState.empty()
                    ? "(unset)"
                    : mEditingFsm.initialState.c_str());
    ImGui::Text("Selected:      %s",
                mSelectedStateName.empty() ? "(none)" : mSelectedStateName.c_str());
    ImGui::Separator();

    DrawCanvas(host);

    ImGui::Separator();
    DrawTables();

    ImGui::Separator();
    ImGui::TextDisabled("c2-5 scope：节点增删改 + 拖动 + Save + Undo/Redo");
    ImGui::TextDisabled("右键空白 = Add State；右键节点 = Delete / Rename；Ctrl+Z/Y 走全局命令栈");
    ImGui::TextDisabled("边绘制 / transition 增删在 c2-6 落地；Condition DSL 在 c2-7");
}

}  // namespace Orange::Editor::Plugin
