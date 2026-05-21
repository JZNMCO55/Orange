#include "AnimFsmAssetInspectorPlugin.h"

#include "../AnimFsmFileIO.h"
#include "../EditorHost.h"
#include "../command/AnimFsmCommands.h"
#include "../theme/EditorTheme.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
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
    mSelectedTransitionIndex = static_cast<std::size_t>(-1);
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

    // background hit-target：覆盖整个 canvas 区域。先注册（先画）；ImGui
    // 1.91.x 默认先注册的 item 会"锁定" hover/active/clicked，导致后续节点
    // InvisibleButton 抢不到 click + drag —— 必须在 bg 上调
    // SetNextItemAllowOverlap()，显式允许后续 item 在同区域命中。
    ImGui::SetCursorScreenPos(canvasOrigin);
    ImGui::SetNextItemAllowOverlap();
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
    const ImU32 edgeColor          = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const float edgeThickness      = 2.0f;
    const float arrowSize          = ImGui::GetFontSize() * 0.6f;

    // 在节点之前画边（绘制顺序 = 边在节点之下，避免覆盖节点边缘）。
    // 简化策略：直线 + 中点箭头；自循环画节点右侧圆环。Bezier 曲线 +
    // 端点 clip 到节点矩形边缘的 polish 留 c2-7+。
    for (const auto& t : mEditingFsm.transitions)
    {
        auto fromIt = std::find_if(
            mEditingFsm.states.begin(), mEditingFsm.states.end(),
            [&t](const ::Orange::Editor::AnimFsm::EditableState& s)
            { return s.name == t.fromState; });
        auto toIt = std::find_if(
            mEditingFsm.states.begin(), mEditingFsm.states.end(),
            [&t](const ::Orange::Editor::AnimFsm::EditableState& s)
            { return s.name == t.toState; });
        if (fromIt == mEditingFsm.states.end()
            || toIt == mEditingFsm.states.end())
        {
            continue;  // dangling 引用（reader 已 reject + 命令路径理论
                       //   不产生，仍做防御性 skip）
        }

        const ImVec2 fromCenter = ImVec2(
            canvasOrigin.x + fromIt->layoutX + kNodeWidth * 0.5f,
            canvasOrigin.y + fromIt->layoutY + kNodeHeight * 0.5f);
        const ImVec2 toCenter = ImVec2(
            canvasOrigin.x + toIt->layoutX + kNodeWidth * 0.5f,
            canvasOrigin.y + toIt->layoutY + kNodeHeight * 0.5f);

        if (t.fromState == t.toState)
        {
            // 自循环：节点右侧圆环（无箭头 —— self-loop 方向自明）
            const float loopRadius = ImGui::GetFontSize() * 1.5f;
            const ImVec2 loopCenter = ImVec2(
                fromCenter.x + kNodeWidth * 0.5f + loopRadius,
                fromCenter.y);
            dl->AddCircle(loopCenter, loopRadius, edgeColor, 16, edgeThickness);
            continue;
        }

        dl->AddLine(fromCenter, toCenter, edgeColor, edgeThickness);

        // 箭头：在线中点处画一个朝向 toCenter 方向的三角形
        const float dx  = toCenter.x - fromCenter.x;
        const float dy  = toCenter.y - fromCenter.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0.001f) { continue; }
        const float  invLen = 1.0f / len;
        const ImVec2 dir    = ImVec2(dx * invLen, dy * invLen);
        const ImVec2 perp   = ImVec2(-dir.y, dir.x);
        const ImVec2 midPos = ImVec2(
            (fromCenter.x + toCenter.x) * 0.5f,
            (fromCenter.y + toCenter.y) * 0.5f);
        const ImVec2 tipPos = ImVec2(
            midPos.x + dir.x * arrowSize,
            midPos.y + dir.y * arrowSize);
        const ImVec2 baseCenter = ImVec2(
            midPos.x - dir.x * arrowSize,
            midPos.y - dir.y * arrowSize);
        const ImVec2 a1 = ImVec2(
            baseCenter.x + perp.x * arrowSize * 0.5f,
            baseCenter.y + perp.y * arrowSize * 0.5f);
        const ImVec2 a2 = ImVec2(
            baseCenter.x - perp.x * arrowSize * 0.5f,
            baseCenter.y - perp.y * arrowSize * 0.5f);
        dl->AddTriangleFilled(tipPos, a1, a2, edgeColor);
    }

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
            // PushID(state.name) scope 内直接 OpenPopup 会让 popup ID 包含
            // state.name 哈希，但 DrawNodeContextPopup 的 BeginPopup 在 PopID
            // 外侧调，两边 ID stack 不匹配 → popup 永远开不出来。defer 到
            // PopID 之后统一 OpenPopup。
            mPendingOpenNodePopup = true;
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

        // c2-8：initial state 节点左上角金色三角标识（Unity Animator
        // Controller 同款视觉惯例）
        if (state.name == mEditingFsm.initialState)
        {
            const float  triSize = ImGui::GetFontSize() * 0.7f;
            const ImVec2 t0 = ImVec2(nodeMin.x,           nodeMin.y);
            const ImVec2 t1 = ImVec2(nodeMin.x + triSize, nodeMin.y);
            const ImVec2 t2 = ImVec2(nodeMin.x,           nodeMin.y + triSize);
            // 金色 = ImGui 内置 PlotHistogram 色（橙黄），与 EditorTheme
            // accent 同色系；不写 IM_COL32 字面量以遵守 v0.6.5 视觉体系
            const ImU32 initialColor = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
            dl->AddTriangleFilled(t0, t1, t2, initialColor);
        }

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
    //
    // 节点右键 popup 的 OpenPopup 必须 defer 到这里（PopID 外），与下面
    // BeginPopup 处于相同 ID stack scope，否则 ID 哈希不一致 popup 弹不出。
    if (mPendingOpenNodePopup)
    {
        ImGui::OpenPopup(kNodePopupId);
        mPendingOpenNodePopup = false;
    }
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

    // c2-8：Set as initial 按钮（当前 state != initialState 时启用）
    const bool alreadyInitial = (mRenameTargetState == mEditingFsm.initialState);
    ImGui::BeginDisabled(alreadyInitial);
    if (ImGui::Button("Set as Initial"))
    {
        host.cmdStack.Push(std::make_unique<AnimFsmSetInitialStateCommand>(
            this, mEditingFsm.initialState, mRenameTargetState));
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    if (alreadyInitial)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(already initial)");
    }

    if (renameClicked && newValid && !sameName && !nameClash)
    {
        host.cmdStack.Push(std::make_unique<AnimFsmRenameStateCommand>(
            this, mRenameTargetState, newName));
        ImGui::CloseCurrentPopup();
    }

    // ---- c2-6：transitions 管理段 ----
    ImGui::Separator();
    ImGui::TextDisabled("Transitions from this state:");
    {
        // 收集本帧要删的 index，循环结束后 push 命令（避免迭代期间改容器）
        std::size_t indexToDelete = static_cast<std::size_t>(-1);
        bool        anyShown      = false;
        for (std::size_t i = 0; i < mEditingFsm.transitions.size(); ++i)
        {
            const auto& tr = mEditingFsm.transitions[i];
            if (tr.fromState != mRenameTargetState) { continue; }
            anyShown = true;
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("→ %s", tr.toState.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit##tr"))
            {
                // c2-7-B：选中本 transition 用于在 Inspector 主面板编辑
                // condition list，关闭 popup
                mSelectedTransitionIndex = i;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete##tr"))
            {
                indexToDelete = i;
            }
            ImGui::PopID();
        }
        if (!anyShown)
        {
            ImGui::TextDisabled("(no transitions from this state)");
        }
        if (indexToDelete != static_cast<std::size_t>(-1))
        {
            host.cmdStack.Push(std::make_unique<AnimFsmDeleteTransitionCommand>(
                this, indexToDelete));
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Create transition to:");
    {
        // 同帧只创建一条 transition；created flag 防御
        bool created = false;
        for (const auto& s : mEditingFsm.states)
        {
            if (created) { break; }
            ImGui::PushID(s.name.c_str());
            // Selectable 走 DontClosePopups —— 用户连续建多条 transition
            // 不必反复右键节点；点 Cancel / 点外面关闭 popup
            if (ImGui::Selectable(s.name.c_str(), false,
                                  ImGuiSelectableFlags_DontClosePopups))
            {
                host.cmdStack.Push(std::make_unique<AnimFsmAddTransitionCommand>(
                    this, mRenameTargetState, s.name));
                created = true;
            }
            ImGui::PopID();
        }
    }

    ImGui::EndPopup();
}

namespace
{

using ::Orange::Editor::AnimFsm::EditableCondition;
using ::Orange::Editor::AnimFsm::EditableParameter;
using ::Orange::Engine::Animation::ConditionOp;
using ::Orange::Engine::Animation::ParameterType;

const char* ParameterTypeShort(ParameterType t) noexcept
{
    switch (t)
    {
        case ParameterType::Bool:    return "B";
        case ParameterType::Int:     return "I";
        case ParameterType::Float:   return "F";
        case ParameterType::Trigger: return "T";
    }
    return "?";
}

// 找到指定 paramName 在 parameters[] 内的 type（找不到返回 Bool fallback）
ParameterType FindParameterType(
    const std::vector<EditableParameter>& parameters, const std::string& name)
{
    for (const auto& p : parameters)
    {
        if (p.name == name) { return p.type; }
    }
    return ParameterType::Bool;
}

// 按 ParameterType 把 threshold 初始化为对应 variant index 的零值
void InitThresholdForType(std::variant<bool, std::int32_t, float>& th, ParameterType t)
{
    switch (t)
    {
        case ParameterType::Bool:
        case ParameterType::Trigger: th = false;        break;
        case ParameterType::Int:     th = std::int32_t{0}; break;
        case ParameterType::Float:   th = 0.0f;        break;
    }
}

// Default value display string for parameters table row
std::string FormatVariantValue(const std::variant<bool, std::int32_t, float>& v)
{
    if (std::holds_alternative<bool>(v))
    {
        return std::get<bool>(v) ? "true" : "false";
    }
    if (std::holds_alternative<std::int32_t>(v))
    {
        return std::to_string(std::get<std::int32_t>(v));
    }
    return std::to_string(std::get<float>(v));
}

// 一条 ConditionExpr 的 row UI —— paramName combo + op combo + threshold
// input。返回 true 表示用户改了某字段（caller 决定是否 push 命令）。
bool DrawConditionRowUI(EditableCondition&                       condition,
                        const std::vector<EditableParameter>&    parameters,
                        const std::string&                       widgetIdPrefix)
{
    bool modified = false;

    // ----- paramName combo -----
    int currentParamIdx = -1;
    std::vector<const char*> paramNames;
    paramNames.reserve(parameters.size());
    for (std::size_t i = 0; i < parameters.size(); ++i)
    {
        paramNames.push_back(parameters[i].name.c_str());
        if (parameters[i].name == condition.paramName)
        {
            currentParamIdx = static_cast<int>(i);
        }
    }

    const float comboWidth = ImGui::CalcTextSize("XXXXXXXXXX").x
                             + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(comboWidth);
    if (ImGui::Combo((widgetIdPrefix + "param").c_str(),
                     &currentParamIdx,
                     paramNames.data(),
                     static_cast<int>(paramNames.size())))
    {
        if (currentParamIdx >= 0 && currentParamIdx < static_cast<int>(parameters.size()))
        {
            condition.paramName = parameters[currentParamIdx].name;
            // 切换 paramName 时按新 param type 重置 threshold（避免 variant
            // index 与 type 不一致）
            InitThresholdForType(condition.threshold,
                                 parameters[currentParamIdx].type);
            modified = true;
        }
    }

    ImGui::SameLine();

    // ----- op combo -----
    const char* opLabels[] = {
        "If", "IfNot", ">", "<", "==", "!=", ">=", "<="};
    int curOpIdx = static_cast<int>(condition.op);
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("XXXX").x
                            + ImGui::GetStyle().FramePadding.x * 2.0f);
    if (ImGui::Combo((widgetIdPrefix + "op").c_str(),
                     &curOpIdx, opLabels, IM_ARRAYSIZE(opLabels)))
    {
        condition.op = static_cast<ConditionOp>(curOpIdx);
        modified = true;
    }

    // ----- threshold input（按 condition.threshold variant index 渲染对应控件）
    // If / IfNot 时 threshold 不消费，但 UI 仍渲染（让用户随时切回比较 op）
    ImGui::SameLine();
    const float thWidth = ImGui::CalcTextSize("XXXXXXXX").x
                          + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(thWidth);

    if (std::holds_alternative<bool>(condition.threshold))
    {
        bool v = std::get<bool>(condition.threshold);
        if (ImGui::Checkbox((widgetIdPrefix + "th").c_str(), &v))
        {
            condition.threshold = v;
            modified = true;
        }
    }
    else if (std::holds_alternative<std::int32_t>(condition.threshold))
    {
        int v = static_cast<int>(std::get<std::int32_t>(condition.threshold));
        if (ImGui::InputInt((widgetIdPrefix + "th").c_str(), &v, 0))
        {
            condition.threshold = static_cast<std::int32_t>(v);
            modified = true;
        }
    }
    else  // float
    {
        float v = std::get<float>(condition.threshold);
        if (ImGui::InputFloat((widgetIdPrefix + "th").c_str(), &v, 0.0f, 0.0f, "%.3f"))
        {
            condition.threshold = v;
            modified = true;
        }
    }

    return modified;
}

}  // anonymous namespace

void AnimFsmAssetInspectorPlugin::DrawParametersSection(EditorHost& host)
{
    if (!ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }

    std::size_t indexToDelete = static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i < mEditingFsm.parameters.size(); ++i)
    {
        const auto& p = mEditingFsm.parameters[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("[%s] %s = %s",
                    ParameterTypeShort(p.type),
                    p.name.c_str(),
                    FormatVariantValue(p.defaultValue).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete##param"))
        {
            indexToDelete = i;
        }
        ImGui::PopID();
    }
    if (mEditingFsm.parameters.empty())
    {
        ImGui::TextDisabled("(no parameters; click '+ Add Parameter' below)");
    }
    if (indexToDelete != static_cast<std::size_t>(-1))
    {
        host.cmdStack.Push(std::make_unique<AnimFsmDeleteParameterCommand>(
            this, indexToDelete));
    }

    if (ImGui::SmallButton("+ Add Parameter"))
    {
        mAddParameterBuffer[0] = '\0';
        mAddParameterTypeIdx   = 0;
        ImGui::OpenPopup("##anim_fsm_add_param");
    }

    DrawAddParameterPopup(host);
}

void AnimFsmAssetInspectorPlugin::DrawAddParameterPopup(EditorHost& host)
{
    if (!ImGui::BeginPopup("##anim_fsm_add_param")) { return; }

    ImGui::TextUnformatted("Add Parameter");
    ImGui::Separator();

    const float popupInputW = ImGui::CalcTextSize("XXXXXXXXXXXX").x
                              + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(popupInputW);
    const bool enterPressed = ImGui::InputText("##param_name",
                                               mAddParameterBuffer,
                                               sizeof(mAddParameterBuffer),
                                               ImGuiInputTextFlags_EnterReturnsTrue);

    const char* typeLabels[] = {"Bool", "Int", "Float", "Trigger"};
    ImGui::SetNextItemWidth(popupInputW);
    ImGui::Combo("##param_type", &mAddParameterTypeIdx,
                 typeLabels, IM_ARRAYSIZE(typeLabels));

    const std::string newName = mAddParameterBuffer;
    const bool        valid   = !newName.empty();
    bool              clash   = false;
    if (valid)
    {
        for (const auto& p : mEditingFsm.parameters)
        {
            if (p.name == newName) { clash = true; break; }
        }
    }
    if (clash)
    {
        ImGui::TextColored(Orange::Editor::Theme::Color::GetAlertError(),
                           "name '%s' 已存在", newName.c_str());
    }

    ImGui::BeginDisabled(!valid || clash);
    const bool addClicked = ImGui::Button("Add") || enterPressed;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        ImGui::CloseCurrentPopup();
    }

    if (addClicked && valid && !clash)
    {
        EditableParameter p;
        p.name = newName;
        p.type = static_cast<ParameterType>(mAddParameterTypeIdx);
        InitThresholdForType(p.defaultValue, p.type);
        host.cmdStack.Push(std::make_unique<AnimFsmAddParameterCommand>(
            this, std::move(p)));
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void AnimFsmAssetInspectorPlugin::DrawSelectedTransitionSection(EditorHost& host)
{
    if (mSelectedTransitionIndex == static_cast<std::size_t>(-1))
    {
        return;
    }
    if (mSelectedTransitionIndex >= mEditingFsm.transitions.size())
    {
        // transition 被删 / 切文件后选中残留 → 自愈
        mSelectedTransitionIndex = static_cast<std::size_t>(-1);
        return;
    }

    auto&             t      = mEditingFsm.transitions[mSelectedTransitionIndex];
    const std::string header = "Selected transition: " + t.fromState
                               + " -> " + t.toState + "##sel_tr";
    if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Deselect##tr"))
    {
        mSelectedTransitionIndex = static_cast<std::size_t>(-1);
        return;
    }

    ImGui::TextDisabled("Conditions (AND combined; empty list = unconditional transition):");

    // 工作副本 + modified flag —— 任一字段编辑都触发整 vector 覆盖式命令
    std::vector<EditableCondition> newConds   = t.conditions;
    bool                           modified   = false;
    std::size_t                    indexToDel = static_cast<std::size_t>(-1);

    for (std::size_t i = 0; i < newConds.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        if (DrawConditionRowUI(newConds[i], mEditingFsm.parameters,
                               "##cond_"))
        {
            modified = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete##cond_row"))
        {
            indexToDel = i;
        }
        ImGui::PopID();
    }
    if (indexToDel != static_cast<std::size_t>(-1))
    {
        newConds.erase(newConds.begin() + static_cast<std::ptrdiff_t>(indexToDel));
        modified = true;
    }

    ImGui::BeginDisabled(mEditingFsm.parameters.empty());
    if (ImGui::SmallButton("+ Add Condition"))
    {
        if (!mEditingFsm.parameters.empty())
        {
            EditableCondition c;
            c.paramName = mEditingFsm.parameters.front().name;
            c.op        = ConditionOp::If;
            InitThresholdForType(c.threshold,
                                 mEditingFsm.parameters.front().type);
            newConds.push_back(std::move(c));
            modified = true;
        }
    }
    ImGui::EndDisabled();
    if (mEditingFsm.parameters.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(register a parameter first)");
    }

    if (modified)
    {
        host.cmdStack.Push(std::make_unique<AnimFsmSetTransitionConditionsCommand>(
            this, mSelectedTransitionIndex,
            t.conditions, std::move(newConds)));
    }
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
    DrawParametersSection(host);

    ImGui::Separator();
    DrawSelectedTransitionSection(host);

    ImGui::Separator();
    DrawTables();

    ImGui::Separator();
    ImGui::TextDisabled("v0.7 c2-7：Parameters + transition Conditions（AND）+ Undo/Redo");
    ImGui::TextDisabled("右键空白 = Add State；右键节点 = Delete / Rename / Add Transition / Edit Transition");
    ImGui::TextDisabled("选中 transition（节点 popup Edit）→ 上方 Selected transition 段编辑 conditions");
}

}  // namespace Orange::Editor::Plugin
