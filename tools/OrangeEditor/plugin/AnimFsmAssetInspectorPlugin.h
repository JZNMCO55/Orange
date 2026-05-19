#ifndef ORANGE_EDITOR_PLUGIN_ANIM_FSM_ASSET_INSPECTOR_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_ANIM_FSM_ASSET_INSPECTOR_PLUGIN_H

// AnimFsmAssetInspectorPlugin —— v0.7 c2 落地：IEditorAssetInspector
// Plugin 的第二个真实 case（对偶 MaterialAssetInspectorPlugin）。
//
// 职责：当 Asset 浏览器选中 .anim_fsm 文件时接管整个 Inspector 区域。
//
// 落地范围（c2 sub-commit 拆分）：
//   * c2-3：plugin 链路 + .anim_fsm round-trip（States / Transitions 表格）
//   * c2-4（本 commit）：节点图 ImGui 自绘（节点矩形 + label + 选中 +
//     拖动）。仿 Lumix `imgui_user.inl` 的 ImDrawList::AddRectFilled +
//     IsMouseDragging(0) + GetIO().MouseDelta 模式；零 third-party 依
//     赖。拖动只改 in-memory editing 副本，**不**落盘 / **不**走 Undo
//     —— Save + 命令栈在 c2-5 落地
//   * c2-5：节点交互 + 命令栈（Add / Delete / Rename / Move + Save）
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

    // 接管 Inspector 整段：path 头部 + 统计行 + 节点图 canvas + 折叠
    // States/Transitions 表格 + 底部 scope 提示。
    void Draw(EditorHost& host, const std::string& assetPath) override;

private:
    // editing 副本与 assetPath 不一致时从盘 reload；不一致时 mSelected
    // StateName 同步清空。reload 失败 → mEditingValid = false。
    void EnsureEditingCache(const std::string& assetPath);

    // 在 ImGui 当前布局位置开 child window 画节点 canvas。节点位置走
    // mEditingFsm.states[i].layoutX/Y（in-memory，拖动直接改副本，不落盘）。
    void DrawCanvas();

    // 折叠区：States / Transitions 表格（c2-3 落地的展示路径保留，作为
    // 节点图之外的"数据视角"辅助检查）。
    void DrawTables() const;

    // editing 副本 —— 切 .anim_fsm 文件时 reload，拖动 layout 时就地改
    std::string                                     mEditingPath;
    ::Orange::Editor::AnimFsm::EditableStateMachine mEditingFsm;
    bool                                            mEditingValid{false};

    // 选中的 state name；空 string = 未选中。多选留 c2-5（与项目"先单
    // 选再扩多选"惯例一致；v0.8 多选实体也走同款节奏）。
    std::string mSelectedStateName;
};

}  // namespace Orange::Editor::Plugin

#endif  // ORANGE_EDITOR_PLUGIN_ANIM_FSM_ASSET_INSPECTOR_PLUGIN_H
