// AnimFsm 子模式命令集实现。详见 .h 头注释（命令清单 + 与
// EntityCommands 的纪律差异 + 切文件 Clear 命令栈的设计意图）。

#include "AnimFsmCommands.h"

#include "../plugin/AnimFsmAssetInspectorPlugin.h"

#include <orange/engine/core/Log.h>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace
{

using ::Orange::Editor::AnimFsm::EditableState;
using ::Orange::Editor::AnimFsm::EditableStateMachine;
using ::Orange::Editor::AnimFsm::EditableTransition;
using ::Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin;

// 找指定 state 在 states[] 内的位置。未命中返回 states.end()。
auto FindStateIt(EditableStateMachine& fsm, const std::string& name)
{
    return std::find_if(fsm.states.begin(), fsm.states.end(),
        [&name](const EditableState& s) { return s.name == name; });
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// AnimFsmAddStateCommand
// ---------------------------------------------------------------------------

AnimFsmAddStateCommand::AnimFsmAddStateCommand(
    AnimFsmAssetInspectorPlugin* pPlugin,
    std::string                  stateName,
    float                        layoutX,
    float                        layoutY)
    : mpPlugin(pPlugin)
    , mStateName(std::move(stateName))
    , mLayoutX(layoutX)
    , mLayoutY(layoutY)
{}

void AnimFsmAddStateCommand::Execute()
{
    if (mpPlugin == nullptr) { return; }
    EditableStateMachine& fsm = mpPlugin->GetEditingFsm();

    // 重名防御（UI 应已 prevent；命令侧再防御一次避免 redo 路径意外）
    if (FindStateIt(fsm, mStateName) != fsm.states.end())
    {
        ORANGE_LOG_WARN("[AnimFsmAddState] state '{}' 已存在，跳过",
                        mStateName);
        return;
    }

    EditableState s;
    s.name    = mStateName;
    s.layoutX = mLayoutX;
    s.layoutY = mLayoutY;
    fsm.states.push_back(std::move(s));

    // 空 fsm 时自动设为 initialState；Undo 时按本字段决定是否清空
    mDidSetInitialState = false;
    if (fsm.initialState.empty())
    {
        fsm.initialState    = mStateName;
        mDidSetInitialState = true;
    }

    mpPlugin->MarkDirty();
}

void AnimFsmAddStateCommand::Undo()
{
    if (mpPlugin == nullptr) { return; }
    EditableStateMachine& fsm = mpPlugin->GetEditingFsm();

    auto it = FindStateIt(fsm, mStateName);
    if (it != fsm.states.end()) { fsm.states.erase(it); }

    if (mDidSetInitialState && fsm.initialState == mStateName)
    {
        fsm.initialState.clear();
    }

    // 联动选中：删的就是当前选中 → 清空
    if (mpPlugin->GetSelectedStateName() == mStateName)
    {
        mpPlugin->SetSelectedStateName(std::string{});
    }

    mpPlugin->MarkDirty();
}

// ---------------------------------------------------------------------------
// AnimFsmDeleteStateCommand
// ---------------------------------------------------------------------------

AnimFsmDeleteStateCommand::AnimFsmDeleteStateCommand(
    AnimFsmAssetInspectorPlugin* pPlugin,
    std::string                  stateName)
    : mpPlugin(pPlugin)
    , mStateName(std::move(stateName))
{}

void AnimFsmDeleteStateCommand::Execute()
{
    if (mpPlugin == nullptr) { return; }
    EditableStateMachine& fsm = mpPlugin->GetEditingFsm();

    auto it = FindStateIt(fsm, mStateName);
    if (it == fsm.states.end())
    {
        ORANGE_LOG_WARN("[AnimFsmDeleteState] state '{}' 不存在，跳过",
                        mStateName);
        return;
    }

    // 快照：state 本体 + 与本 state 相关的所有 transitions
    mSavedState      = *it;
    mSavedStateIndex = static_cast<std::size_t>(it - fsm.states.begin());
    mSavedTransitions.clear();
    mSavedTransitionIndices.clear();
    for (std::size_t i = 0; i < fsm.transitions.size(); ++i)
    {
        const auto& t = fsm.transitions[i];
        if (t.fromState == mStateName || t.toState == mStateName)
        {
            mSavedTransitions.push_back(t);
            mSavedTransitionIndices.push_back(i);
        }
    }

    // 删 state + 级联 transitions
    fsm.states.erase(it);
    fsm.transitions.erase(
        std::remove_if(fsm.transitions.begin(), fsm.transitions.end(),
            [this](const EditableTransition& t) {
                return t.fromState == mStateName || t.toState == mStateName;
            }),
        fsm.transitions.end());

    // 如果删的是 initialState，清空 initialState（Undo 时恢复）
    mWasInitialState = (fsm.initialState == mStateName);
    if (mWasInitialState) { fsm.initialState.clear(); }

    // 联动选中
    if (mpPlugin->GetSelectedStateName() == mStateName)
    {
        mpPlugin->SetSelectedStateName(std::string{});
    }

    mpPlugin->MarkDirty();
}

void AnimFsmDeleteStateCommand::Undo()
{
    if (mpPlugin == nullptr) { return; }
    EditableStateMachine& fsm = mpPlugin->GetEditingFsm();

    // 把 state 插回原 index（如越界则 push_back 兜底）
    const std::size_t insertAt = std::min(mSavedStateIndex, fsm.states.size());
    fsm.states.insert(fsm.states.begin() + static_cast<std::ptrdiff_t>(insertAt),
                      mSavedState);

    // 把 transitions 按原 index 顺序插回。注意 mSavedTransitionIndices 是删
    // 之前的 index，插入时按升序逐条插（每次插入后 index 自动右移；预先升
    // 序保证插入位置依然合法）
    for (std::size_t i = 0; i < mSavedTransitions.size(); ++i)
    {
        const std::size_t idx = std::min(mSavedTransitionIndices[i], fsm.transitions.size());
        fsm.transitions.insert(
            fsm.transitions.begin() + static_cast<std::ptrdiff_t>(idx),
            mSavedTransitions[i]);
    }

    if (mWasInitialState) { fsm.initialState = mStateName; }

    mpPlugin->MarkDirty();
}

// ---------------------------------------------------------------------------
// AnimFsmRenameStateCommand
// ---------------------------------------------------------------------------

AnimFsmRenameStateCommand::AnimFsmRenameStateCommand(
    AnimFsmAssetInspectorPlugin* pPlugin,
    std::string                  oldName,
    std::string                  newName)
    : mpPlugin(pPlugin)
    , mOldName(std::move(oldName))
    , mNewName(std::move(newName))
{}

namespace
{

// 把 fsm 内所有引用 srcName 的字段改为 dstName。Return：实际改动数量
// （诊断用；命令本身不消费）。
std::size_t RenameStateInFsm(EditableStateMachine& fsm,
                             const std::string&    srcName,
                             const std::string&    dstName)
{
    std::size_t changed = 0;
    for (auto& s : fsm.states)
    {
        if (s.name == srcName)
        {
            s.name = dstName;
            ++changed;
        }
    }
    for (auto& t : fsm.transitions)
    {
        if (t.fromState == srcName)
        {
            t.fromState = dstName;
            ++changed;
        }
        if (t.toState == srcName)
        {
            t.toState = dstName;
            ++changed;
        }
    }
    if (fsm.initialState == srcName)
    {
        fsm.initialState = dstName;
        ++changed;
    }
    return changed;
}

}  // anonymous namespace

void AnimFsmRenameStateCommand::Execute()
{
    if (mpPlugin == nullptr) { return; }
    EditableStateMachine& fsm = mpPlugin->GetEditingFsm();

    // 重名防御：new 已存在（且不是 self）→ skip
    if (mNewName != mOldName
        && FindStateIt(fsm, mNewName) != fsm.states.end())
    {
        ORANGE_LOG_WARN("[AnimFsmRenameState] new name '{}' 已存在，跳过",
                        mNewName);
        return;
    }

    RenameStateInFsm(fsm, mOldName, mNewName);

    if (mpPlugin->GetSelectedStateName() == mOldName)
    {
        mpPlugin->SetSelectedStateName(mNewName);
    }
    mpPlugin->MarkDirty();
}

void AnimFsmRenameStateCommand::Undo()
{
    if (mpPlugin == nullptr) { return; }
    EditableStateMachine& fsm = mpPlugin->GetEditingFsm();

    RenameStateInFsm(fsm, mNewName, mOldName);

    if (mpPlugin->GetSelectedStateName() == mNewName)
    {
        mpPlugin->SetSelectedStateName(mOldName);
    }
    mpPlugin->MarkDirty();
}

bool AnimFsmRenameStateCommand::Merge(ICommand& newer)
{
    auto* p = dynamic_cast<AnimFsmRenameStateCommand*>(&newer);
    if (p == nullptr || p->mpPlugin != mpPlugin) { return false; }
    // 仅链式 rename 合并：newer.mOldName 必须等于本命令的 mNewName。
    // 这样 stack 内最终保留 (mOldName -> p->mNewName) 的单条 Undo。
    if (p->mOldName != mNewName) { return false; }
    mNewName = p->mNewName;
    return true;
}

// ---------------------------------------------------------------------------
// AnimFsmMoveStateCommand
// ---------------------------------------------------------------------------

AnimFsmMoveStateCommand::AnimFsmMoveStateCommand(
    AnimFsmAssetInspectorPlugin* pPlugin,
    std::string                  stateName,
    float                        oldX,
    float                        oldY,
    float                        newX,
    float                        newY)
    : mpPlugin(pPlugin)
    , mStateName(std::move(stateName))
    , mOldX(oldX)
    , mOldY(oldY)
    , mNewX(newX)
    , mNewY(newY)
{}

void AnimFsmMoveStateCommand::Execute()
{
    if (mpPlugin == nullptr) { return; }
    EditableStateMachine& fsm = mpPlugin->GetEditingFsm();
    auto it = FindStateIt(fsm, mStateName);
    if (it == fsm.states.end()) { return; }
    it->layoutX = mNewX;
    it->layoutY = mNewY;
    mpPlugin->MarkDirty();
}

void AnimFsmMoveStateCommand::Undo()
{
    if (mpPlugin == nullptr) { return; }
    EditableStateMachine& fsm = mpPlugin->GetEditingFsm();
    auto it = FindStateIt(fsm, mStateName);
    if (it == fsm.states.end()) { return; }
    it->layoutX = mOldX;
    it->layoutY = mOldY;
    mpPlugin->MarkDirty();
}

bool AnimFsmMoveStateCommand::Merge(ICommand& newer)
{
    auto* p = dynamic_cast<AnimFsmMoveStateCommand*>(&newer);
    if (p == nullptr
        || p->mpPlugin    != mpPlugin
        || p->mStateName  != mStateName)
    {
        return false;
    }
    // 同 state 连续拖动合并：mOldX/Y 保留首次拖前的值；mNewX/Y 更新到
    // 最新（最终松手位置）。
    mNewX = p->mNewX;
    mNewY = p->mNewY;
    return true;
}

// ---------------------------------------------------------------------------
// AnimFsmAddTransitionCommand
// ---------------------------------------------------------------------------

AnimFsmAddTransitionCommand::AnimFsmAddTransitionCommand(
    AnimFsmAssetInspectorPlugin* pPlugin,
    std::string                  fromState,
    std::string                  toState)
    : mpPlugin(pPlugin)
    , mFromState(std::move(fromState))
    , mToState(std::move(toState))
{}

void AnimFsmAddTransitionCommand::Execute()
{
    if (mpPlugin == nullptr) { return; }
    EditableStateMachine& fsm = mpPlugin->GetEditingFsm();

    // 端点防御：必须存在于 states[]（UI 应已 prevent，但命令侧再防御）
    if (FindStateIt(fsm, mFromState) == fsm.states.end()
        || FindStateIt(fsm, mToState)   == fsm.states.end())
    {
        ORANGE_LOG_WARN("[AnimFsmAddTransition] 端点 '{}' / '{}' 不存在，跳过",
                        mFromState, mToState);
        return;
    }

    EditableTransition t;
    t.fromState = mFromState;
    t.toState   = mToState;
    fsm.transitions.push_back(std::move(t));

    mpPlugin->MarkDirty();
}

void AnimFsmAddTransitionCommand::Undo()
{
    if (mpPlugin == nullptr) { return; }
    EditableStateMachine& fsm = mpPlugin->GetEditingFsm();

    // Execute push_back 到末尾；Undo 从末尾找最后一条匹配 from->to 移除
    // （rbegin 反向扫描首个匹配，匹配的就是 Execute 当时 push 的那条）
    for (auto it = fsm.transitions.rbegin(); it != fsm.transitions.rend(); ++it)
    {
        if (it->fromState == mFromState && it->toState == mToState)
        {
            // reverse_iterator → 对应的 forward iterator = std::next(it).base()
            fsm.transitions.erase(std::next(it).base());
            break;
        }
    }
    mpPlugin->MarkDirty();
}

// ---------------------------------------------------------------------------
// AnimFsmDeleteTransitionCommand
// ---------------------------------------------------------------------------

AnimFsmDeleteTransitionCommand::AnimFsmDeleteTransitionCommand(
    AnimFsmAssetInspectorPlugin* pPlugin,
    std::size_t                  transitionIndex)
    : mpPlugin(pPlugin)
    , mIndex(transitionIndex)
{}

void AnimFsmDeleteTransitionCommand::Execute()
{
    if (mpPlugin == nullptr) { return; }
    EditableStateMachine& fsm = mpPlugin->GetEditingFsm();
    if (mIndex >= fsm.transitions.size())
    {
        ORANGE_LOG_WARN("[AnimFsmDeleteTransition] index {} 越界（size={}），跳过",
                        mIndex, fsm.transitions.size());
        mWasValid = false;
        return;
    }
    mSavedTransition = fsm.transitions[mIndex];
    fsm.transitions.erase(fsm.transitions.begin()
                          + static_cast<std::ptrdiff_t>(mIndex));
    mWasValid = true;
    mpPlugin->MarkDirty();
}

void AnimFsmDeleteTransitionCommand::Undo()
{
    if (mpPlugin == nullptr || !mWasValid) { return; }
    EditableStateMachine& fsm = mpPlugin->GetEditingFsm();
    const std::size_t insertAt = std::min(mIndex, fsm.transitions.size());
    fsm.transitions.insert(
        fsm.transitions.begin() + static_cast<std::ptrdiff_t>(insertAt),
        mSavedTransition);
    mpPlugin->MarkDirty();
}

// ---------------------------------------------------------------------------
// AnimFsmAddParameterCommand
// ---------------------------------------------------------------------------

using ::Orange::Editor::AnimFsm::EditableParameter;
using ::Orange::Editor::AnimFsm::EditableCondition;

AnimFsmAddParameterCommand::AnimFsmAddParameterCommand(
    AnimFsmAssetInspectorPlugin* pPlugin,
    EditableParameter            parameter)
    : mpPlugin(pPlugin)
    , mParameter(std::move(parameter))
{}

void AnimFsmAddParameterCommand::Execute()
{
    if (mpPlugin == nullptr) { return; }
    EditableStateMachine& fsm = mpPlugin->GetEditingFsm();
    // 重名防御
    for (const auto& p : fsm.parameters)
    {
        if (p.name == mParameter.name)
        {
            ORANGE_LOG_WARN("[AnimFsmAddParameter] '{}' 已存在，跳过",
                            mParameter.name);
            return;
        }
    }
    fsm.parameters.push_back(mParameter);
    mpPlugin->MarkDirty();
}

void AnimFsmAddParameterCommand::Undo()
{
    if (mpPlugin == nullptr) { return; }
    EditableStateMachine& fsm = mpPlugin->GetEditingFsm();
    for (auto it = fsm.parameters.begin(); it != fsm.parameters.end(); ++it)
    {
        if (it->name == mParameter.name)
        {
            fsm.parameters.erase(it);
            break;
        }
    }
    mpPlugin->MarkDirty();
}

// ---------------------------------------------------------------------------
// AnimFsmDeleteParameterCommand
// ---------------------------------------------------------------------------

AnimFsmDeleteParameterCommand::AnimFsmDeleteParameterCommand(
    AnimFsmAssetInspectorPlugin* pPlugin,
    std::size_t                  parameterIndex)
    : mpPlugin(pPlugin)
    , mIndex(parameterIndex)
{}

void AnimFsmDeleteParameterCommand::Execute()
{
    if (mpPlugin == nullptr) { return; }
    EditableStateMachine& fsm = mpPlugin->GetEditingFsm();
    if (mIndex >= fsm.parameters.size())
    {
        ORANGE_LOG_WARN("[AnimFsmDeleteParameter] index {} 越界（size={}），跳过",
                        mIndex, fsm.parameters.size());
        mWasValid = false;
        return;
    }
    mSaved = fsm.parameters[mIndex];
    fsm.parameters.erase(fsm.parameters.begin() + static_cast<std::ptrdiff_t>(mIndex));
    mWasValid = true;
    mpPlugin->MarkDirty();
}

void AnimFsmDeleteParameterCommand::Undo()
{
    if (mpPlugin == nullptr || !mWasValid) { return; }
    EditableStateMachine& fsm = mpPlugin->GetEditingFsm();
    const std::size_t insertAt = std::min(mIndex, fsm.parameters.size());
    fsm.parameters.insert(fsm.parameters.begin() + static_cast<std::ptrdiff_t>(insertAt),
                          mSaved);
    mpPlugin->MarkDirty();
}

// ---------------------------------------------------------------------------
// AnimFsmSetInitialStateCommand
// ---------------------------------------------------------------------------

AnimFsmSetInitialStateCommand::AnimFsmSetInitialStateCommand(
    AnimFsmAssetInspectorPlugin* pPlugin,
    std::string                  oldInitial,
    std::string                  newInitial)
    : mpPlugin(pPlugin)
    , mOldInitial(std::move(oldInitial))
    , mNewInitial(std::move(newInitial))
{}

void AnimFsmSetInitialStateCommand::Execute()
{
    if (mpPlugin == nullptr) { return; }
    mpPlugin->GetEditingFsm().initialState = mNewInitial;
    mpPlugin->MarkDirty();
}

void AnimFsmSetInitialStateCommand::Undo()
{
    if (mpPlugin == nullptr) { return; }
    mpPlugin->GetEditingFsm().initialState = mOldInitial;
    mpPlugin->MarkDirty();
}

// ---------------------------------------------------------------------------
// AnimFsmSetTransitionConditionsCommand
// ---------------------------------------------------------------------------

AnimFsmSetTransitionConditionsCommand::AnimFsmSetTransitionConditionsCommand(
    AnimFsmAssetInspectorPlugin*           pPlugin,
    std::size_t                            transitionIndex,
    std::vector<EditableCondition>         oldConditions,
    std::vector<EditableCondition>         newConditions)
    : mpPlugin(pPlugin)
    , mIndex(transitionIndex)
    , mOldConditions(std::move(oldConditions))
    , mNewConditions(std::move(newConditions))
{}

void AnimFsmSetTransitionConditionsCommand::Execute()
{
    if (mpPlugin == nullptr) { return; }
    EditableStateMachine& fsm = mpPlugin->GetEditingFsm();
    if (mIndex >= fsm.transitions.size())
    {
        ORANGE_LOG_WARN("[AnimFsmSetTransitionConditions] index {} 越界，跳过",
                        mIndex);
        return;
    }
    fsm.transitions[mIndex].conditions = mNewConditions;
    mpPlugin->MarkDirty();
}

void AnimFsmSetTransitionConditionsCommand::Undo()
{
    if (mpPlugin == nullptr) { return; }
    EditableStateMachine& fsm = mpPlugin->GetEditingFsm();
    if (mIndex >= fsm.transitions.size())
    {
        return;
    }
    fsm.transitions[mIndex].conditions = mOldConditions;
    mpPlugin->MarkDirty();
}
