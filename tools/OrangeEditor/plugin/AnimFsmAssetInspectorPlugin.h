#ifndef ORANGE_EDITOR_PLUGIN_ANIM_FSM_ASSET_INSPECTOR_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_ANIM_FSM_ASSET_INSPECTOR_PLUGIN_H

// AnimFsmAssetInspectorPlugin —— v0.7 c2 落地：IEditorAssetInspector
// Plugin 的第二个真实 case（对偶 MaterialAssetInspectorPlugin）。
//
// 职责：当 Asset 浏览器选中 .anim_fsm 文件时接管整个 Inspector 区域。
//
// 落地范围（c2 sub-commit 拆分）：
//   * c2-3：plugin 链路 + .anim_fsm round-trip（States / Transitions 表格）
//   * c2-4：节点图 ImGui 自绘（节点矩形 + 选中 + 拖动）
//   * c2-5（本 commit）：节点交互 + 命令栈 + Save
//     - 命令类型：AnimFsm{Add,Delete,Rename,Move}StateCommand
//     - 右键空白 popup：Add State
//     - 右键节点 popup：Delete / Rename
//     - 拖动结束时 push MoveStateCommand（拖动期间走 in-memory delta 不入栈）
//     - Save 按钮：dirty 时启用，WriteAnimFsmFile 写回 + 清 dirty
//     - 切 .anim_fsm 文件时 host.cmdStack.Clear()（与切 Scene 同款纪律）
//   * c2-6：边绘制 + transition 创建删除
//   * c2-7：Condition DSL 编辑（依赖 ADR-005 决策）
//   * c2-8：Initial state 标记
//
// 设计意图：本 plugin 验证 v0.7 c0 抽象的可扩展性 —— "新增按选中资源
// 类型切 Inspector 内容只需注册 plugin，不再改 InspectorPanel" 这一
// 约定。

#include "../AnimFsmModel.h"
#include "IEditorAssetInspectorPlugin.h"

namespace Orange::Editor::Plugin
{

class AnimFsmAssetInspectorPlugin : public IEditorAssetInspectorPlugin
{
public:
    // 按 path 末尾 ".anim_fsm" 后缀比较匹配。空 / 短 path 返回 false。
    bool CanHandle(const std::string& assetPath) const override;

    // 接管 Inspector 整段。详见 .cpp 内 Draw 实现。
    void Draw(EditorHost& host, const std::string& assetPath) override;

    // ---- 命令访问入口（c2-5 起，AnimFsmCommands.cpp 调用） ----
    //
    // 命令类不是 plugin friend；通过暴露 mutation 入口接口直接操作 editing
    // 副本。dirty 字段由命令 Execute / Undo 显式 MarkDirty 标，Save 路径
    // ClearDirty。
    ::Orange::Editor::AnimFsm::EditableStateMachine&       GetEditingFsm() noexcept       { return mEditingFsm; }
    const ::Orange::Editor::AnimFsm::EditableStateMachine& GetEditingFsm() const noexcept { return mEditingFsm; }

    void MarkDirty() noexcept   { mDirty = true; }
    bool IsDirty() const noexcept { return mDirty; }

    // 命令操作如果同时改变了选中状态（Rename / Delete 后），plugin 选中
    // 字段必须联动 —— 命令调用以保持 UI 一致。
    const std::string& GetSelectedStateName() const noexcept { return mSelectedStateName; }
    void               SetSelectedStateName(std::string name) { mSelectedStateName = std::move(name); }

private:
    // editing 副本与 assetPath 不一致时从盘 reload + Clear 命令栈；reload
    // 失败 → mEditingValid = false。
    void EnsureEditingCache(EditorHost& host, const std::string& assetPath);

    // 把 mEditingFsm 写回 mEditingPath 指向的文件；成功后 mDirty = false。
    void SaveToDisk();

    // canvas 节点图绘制 + 鼠标交互（拖动 / 选中 / 右键 popup 触发）。
    // 命令 push 走 host.cmdStack。
    void DrawCanvas(EditorHost& host);

    // 折叠区：States / Transitions 表格。
    void DrawTables() const;

    // 右键 popup 路径（c2-5）：
    // - Add State popup 由 canvas 空白处右键触发
    // - Rename / Delete popup 由节点右键触发
    void DrawAddStatePopup(EditorHost& host);
    void DrawNodeContextPopup(EditorHost& host);

    // c2-7-B 新增：Parameters 折叠段 + 选中 transition condition 编辑段
    void DrawParametersSection(EditorHost& host);
    void DrawSelectedTransitionSection(EditorHost& host);
    void DrawAddParameterPopup(EditorHost& host);

    // ----- editing 状态 -----
    std::string                                     mEditingPath;
    ::Orange::Editor::AnimFsm::EditableStateMachine mEditingFsm;
    bool                                            mEditingValid{false};
    bool                                            mDirty{false};

    // ----- 选中 -----
    std::string mSelectedStateName;

    // ----- 拖动跟踪（per-frame）-----
    // 鼠标按下瞬间记录 mDraggingStateName + initialX/Y；松手（IsItemDeactivated）
    // 时 push MoveStateCommand(initialX/Y, currentX/Y) + 清空 mDraggingStateName。
    // 拖动**期间**已直接修改 state.layoutX/Y（in-memory delta），不入命令栈。
    std::string mDraggingStateName;
    float       mDragInitialX{0.0f};
    float       mDragInitialY{0.0f};

    // ----- popup buffers -----
    // Add State popup：用户输入的 state name 编辑 buffer + 触发时鼠标位
    // 置（canvas 内坐标，作为新节点的默认 layout）。**独立**于 mDragInitial
    // 字段——后者只服务节点拖动跟踪，混用会在 popup 打开期间被节点 Activated
    // 路径覆盖。
    char  mAddStateBuffer[64]{};
    float mPopupSpawnX{0.0f};
    float mPopupSpawnY{0.0f};
    // Rename popup：目标 state（右键触发时记录）+ 新名 buffer
    std::string mRenameTargetState;
    char        mRenameBuffer[64]{};
    // 节点右键 popup 触发是 deferred —— 必须在节点循环 PopID 之外调
    // OpenPopup，否则与 BeginPopup 的 ID stack scope 不匹配导致 popup
    // 弹不出来。flag 仅活一帧。
    bool        mPendingOpenNodePopup{false};

    // ----- v0.7 c2-7-B: condition / parameter UI 状态 -----
    // 用户当前正在编辑的 transition 在 mEditingFsm.transitions[] 内的 index；
    // 未选时为 size_t(-1)。切换 .anim_fsm 文件 / 删除 transition 时重置。
    std::size_t mSelectedTransitionIndex{static_cast<std::size_t>(-1)};
    // Add Parameter popup 的输入 buffer
    char mAddParameterBuffer[64]{};
    int  mAddParameterTypeIdx{0};  // 0=Bool, 1=Int, 2=Float, 3=Trigger（与 ParameterType enum 对齐）
};

}  // namespace Orange::Editor::Plugin

#endif  // ORANGE_EDITOR_PLUGIN_ANIM_FSM_ASSET_INSPECTOR_PLUGIN_H
