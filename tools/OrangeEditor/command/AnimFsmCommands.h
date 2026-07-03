#ifndef ORANGE_EDITOR_COMMAND_ANIM_FSM_COMMANDS_H
#define ORANGE_EDITOR_COMMAND_ANIM_FSM_COMMANDS_H

// AnimFsm 子模式命令集（v0.7 c2-5 落地）。
//
// 与 EntityCommands 的纪律差异：本组命令操作 plugin 内部的 in-memory
// EditableStateMachine 副本，不操作 World / Entity。命令存 plugin 弱
// 引用 —— plugin 由 EditorHost.assetInspectorPlugins 持有，与 CommandStack
// 同生命周期；析构顺序 plugin 先 / CommandStack 后，但 CommandStack 析
// 构不调 Undo，dangling pointer 永不被访问，安全。
//
// 切 .anim_fsm 文件时（plugin::EnsureEditingCache reload）需要 Clear
// 全局命令栈 —— 旧文件的命令在新 fsm 上 Undo 会乱跑（state name 命中
// 错位）。这与切场景同款节奏：与 EditorSceneContext::pWorld swap 时
// CommandStack::Clear() 的设计意图一致。
//
// 命令清单：
//   * AnimFsmAddStateCommand    —— 追加 state；空 fsm 时自动设为
//                                  initialState
//   * AnimFsmDeleteStateCommand —— 删 state + 级联删所有相关 transitions
//                                  + 清 initialState（如有）
//   * AnimFsmRenameStateCommand —— 改 state.name + 联动 transitions /
//                                  initialState；连续 rename 同一 state
//                                  合并为单条 Undo（Merge 路径）
//   * AnimFsmMoveStateCommand   —— 改 state.layoutX/Y；同 state 连续拖
//                                  动合并为单条 Undo（Merge 路径）
//   * AnimFsmAddTransitionCommand    —— 在 transitions[] 末尾 push_back
//                                       一条新 transition
//   * AnimFsmDeleteTransitionCommand —— 按 index 删除 transition；Undo
//                                       按原 index 插回

#include "../AnimFsmModel.h"
#include "ICommand.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Orange::Editor::Plugin
{
    class AnimFsmAssetInspectorPlugin;
}

class AnimFsmAddStateCommand : public ICommand
{
public:
    AnimFsmAddStateCommand(Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin* pPlugin,
                           std::string                                          stateName,
                           float                                                layoutX,
                           float                                                layoutY);

    void        Execute() override;
    void        Undo() override;
    const char* GetType() const override { return "anim_fsm_add_state"; }

private:
    Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin* mpPlugin;
    std::string                                          mStateName;
    float                                                mLayoutX;
    float                                                mLayoutY;
    // Execute 时如果 fsm 之前为空 → 顺手把 initialState 设为本 state；
    // Undo 时按本字段恢复（true 才清空，避免误清其它命令设的 initial）
    bool mDidSetInitialState{false};
};

class AnimFsmDeleteStateCommand : public ICommand
{
public:
    AnimFsmDeleteStateCommand(Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin* pPlugin,
                              std::string                                          stateName);

    void        Execute() override;
    void        Undo() override;
    const char* GetType() const override { return "anim_fsm_delete_state"; }

private:
    Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin* mpPlugin;
    std::string                                          mStateName;

    // Undo 用快照：删除前的 state 副本 + 与本 state 相关的所有 transitions
    // + 在原 transitions[] 内的 index（恢复时按原序插回）。initialState
    // 是否被本命令清空也记录。
    ::Orange::Editor::AnimFsm::EditableState                   mSavedState;
    std::vector<::Orange::Editor::AnimFsm::EditableTransition> mSavedTransitions;
    std::vector<std::size_t>                                   mSavedTransitionIndices;
    std::size_t                                                mSavedStateIndex{0};
    bool                                                       mWasInitialState{false};
};

class AnimFsmRenameStateCommand : public ICommand
{
public:
    AnimFsmRenameStateCommand(Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin* pPlugin,
                              std::string                                          oldName,
                              std::string                                          newName);

    void        Execute() override;
    void        Undo() override;
    const char* GetType() const override { return "anim_fsm_rename_state"; }
    bool        Merge(ICommand& newer) override;

private:
    Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin* mpPlugin;
    std::string                                          mOldName;
    std::string                                          mNewName;
};

class AnimFsmMoveStateCommand : public ICommand
{
public:
    AnimFsmMoveStateCommand(Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin* pPlugin,
                            std::string                                          stateName,
                            float                                                oldX,
                            float                                                oldY,
                            float                                                newX,
                            float                                                newY);

    void        Execute() override;
    void        Undo() override;
    const char* GetType() const override { return "anim_fsm_move_state"; }
    bool        Merge(ICommand& newer) override;

private:
    Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin* mpPlugin;
    std::string                                          mStateName;
    float                                                mOldX;
    float                                                mOldY;
    float                                                mNewX;
    float                                                mNewY;
};

class AnimFsmAddTransitionCommand : public ICommand
{
public:
    AnimFsmAddTransitionCommand(Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin* pPlugin,
                                std::string                                          fromState,
                                std::string                                          toState);

    void        Execute() override;
    void        Undo() override;
    const char* GetType() const override { return "anim_fsm_add_transition"; }

private:
    Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin* mpPlugin;
    std::string                                          mFromState;
    std::string                                          mToState;
};

class AnimFsmDeleteTransitionCommand : public ICommand
{
public:
    AnimFsmDeleteTransitionCommand(Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin* pPlugin,
                                   std::size_t                                          transitionIndex);

    void        Execute() override;
    void        Undo() override;
    const char* GetType() const override { return "anim_fsm_delete_transition"; }

private:
    Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin* mpPlugin;
    std::size_t                                          mIndex;
    // Execute 时保存被删 transition 的副本，Undo 按原 index 插回
    ::Orange::Editor::AnimFsm::EditableTransition mSavedTransition;
    bool                                          mWasValid{false};
};

// ----- v0.7 c2-7-B: parameter + condition 命令 -----

class AnimFsmAddParameterCommand : public ICommand
{
public:
    AnimFsmAddParameterCommand(Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin* pPlugin,
                               ::Orange::Editor::AnimFsm::EditableParameter         parameter);

    void        Execute() override;
    void        Undo() override;
    const char* GetType() const override { return "anim_fsm_add_parameter"; }

private:
    Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin* mpPlugin;
    ::Orange::Editor::AnimFsm::EditableParameter         mParameter;
};

class AnimFsmDeleteParameterCommand : public ICommand
{
public:
    AnimFsmDeleteParameterCommand(Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin* pPlugin,
                                  std::size_t                                          parameterIndex);

    void        Execute() override;
    void        Undo() override;
    const char* GetType() const override { return "anim_fsm_delete_parameter"; }

private:
    Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin* mpPlugin;
    std::size_t                                          mIndex;
    ::Orange::Editor::AnimFsm::EditableParameter         mSaved;
    bool                                                 mWasValid{false};
    // 删 parameter **不**级联引用的 condition —— 引用 dangling parameter
    // 的 condition 运行时 evaluate 始终 false（no-fire），编辑器仍展示
    // 让用户决定是否手动删
};

// SetInitialStateCommand（v0.7 c2-8） —— 改 fsm.initialState 字符串
// + 联动运行时启动 state（实际写入磁盘 + 运行时翻译时按本字段 SetInitial
// State）。空字符串视为"清空 initial state"合法操作。
class AnimFsmSetInitialStateCommand : public ICommand
{
public:
    AnimFsmSetInitialStateCommand(Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin* pPlugin,
                                  std::string                                          oldInitial,
                                  std::string                                          newInitial);

    void        Execute() override;
    void        Undo() override;
    const char* GetType() const override { return "anim_fsm_set_initial_state"; }

private:
    Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin* mpPlugin;
    std::string                                          mOldInitial;
    std::string                                          mNewInitial;
};

// 整 vector<EditableCondition> 覆盖式命令 —— Add / Delete / Edit 单条
// condition 都通过构造 newConds 推送本命令实现。merge 暂不实现（Undo
// 粒度按 transition 覆盖，与 Lumix 模式一致）。
class AnimFsmSetTransitionConditionsCommand : public ICommand
{
public:
    AnimFsmSetTransitionConditionsCommand(
        Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin*      pPlugin,
        std::size_t                                               transitionIndex,
        std::vector<::Orange::Editor::AnimFsm::EditableCondition> oldConditions,
        std::vector<::Orange::Editor::AnimFsm::EditableCondition> newConditions);

    void        Execute() override;
    void        Undo() override;
    const char* GetType() const override { return "anim_fsm_set_transition_conditions"; }

private:
    Orange::Editor::Plugin::AnimFsmAssetInspectorPlugin*      mpPlugin;
    std::size_t                                               mIndex;
    std::vector<::Orange::Editor::AnimFsm::EditableCondition> mOldConditions;
    std::vector<::Orange::Editor::AnimFsm::EditableCondition> mNewConditions;
};

#endif // ORANGE_EDITOR_COMMAND_ANIM_FSM_COMMANDS_H
