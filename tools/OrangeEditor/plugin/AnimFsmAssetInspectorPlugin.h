#ifndef ORANGE_EDITOR_PLUGIN_ANIM_FSM_ASSET_INSPECTOR_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_ANIM_FSM_ASSET_INSPECTOR_PLUGIN_H

// AnimFsmAssetInspectorPlugin —— v0.7 c2 落地：IEditorAssetInspector
// Plugin 的第二个真实 case（对偶 MaterialAssetInspectorPlugin）。
//
// 职责：当 Asset 浏览器选中 .anim_fsm 文件时接管整个 Inspector 区域。
//
// 落地范围（c2 sub-commit 拆分）：
//   * c2-2（本 commit）：plugin 链路 + .anim_fsm round-trip 验证。
//     Draw 钩子读 .anim_fsm 文件（AnimFsmFileIO），显示 states /
//     transitions / initial state 三项计数 + 列表。**无**节点图绘制、
//     **无**编辑 UI、**无**Save 按钮（dirty 永远 false）。
//   * c2-3：节点图 ImGui 自绘（节点矩形 + 选中 + 拖动）
//   * c2-4：节点/边交互 + 命令栈（Add / Delete / Rename / Move）
//   * c2-5：边绘制 + transition 创建删除
//   * c2-6：Condition DSL 编辑（依赖 ADR-005 决策）
//   * c2-7：Initial state 标记
//
// 设计意图（与 IEditorAssetInspectorPlugin 抽象的对偶 case）：本 plugin
// 验证 v0.7 c0 抽象的可扩展性 —— "新增按选中资源类型切 Inspector 内容
// 的子模式只需注册 plugin，不再改 InspectorPanel" 这一约定。

#include "IEditorAssetInspectorPlugin.h"

namespace Orange::Editor::Plugin
{

class AnimFsmAssetInspectorPlugin : public IEditorAssetInspectorPlugin
{
public:
    // 按 path 末尾 ".anim_fsm" 后缀比较匹配。空 path / 短 path 返回 false。
    bool CanHandle(const std::string& assetPath) const override;

    // 接管 Inspector 整段，调用 DrawAnimFsmSubMode 路径（c2-2 仅展示，
    // 无编辑 UI；后续 c2-3 ~ c2-7 在本路径内逐步展开）。
    void Draw(EditorHost& host, const std::string& assetPath) override;
};

}  // namespace Orange::Editor::Plugin

#endif  // ORANGE_EDITOR_PLUGIN_ANIM_FSM_ASSET_INSPECTOR_PLUGIN_H
