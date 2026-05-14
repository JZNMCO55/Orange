#ifndef ORANGE_EDITOR_PLUGIN_ANIMATOR_MINI_PREVIEW_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_ANIMATOR_MINI_PREVIEW_PLUGIN_H

// AnimatorMiniPreviewPlugin —— v0.3 milestone 第一个真实
// IEditorInspectorPlugin case。
//
// 职责：在 AnimatorComponent 段末（ParseEnd 钩子）追加一条 mini-preview
// 横幅，显示 backend name + 运行状态（Running / Finished）+ 状态颜色。
//
// 设计意图：验证 IEditorInspectorPlugin 抽象的"装饰式扩展"路径——plugin
// 只在 schema 默认渲染**之后**追加 UI，不替换字段控件、不接管整段、不向
// command stack 推入命令。这是 IEditorInspectorPlugin 头注释中"在段首 /
// 段尾追加 UI"用例的最小落地。
//
// 选型理由：见 editor-roadmap.md v0.3 deliverable 5；候选 Animator
// mini-preview / Light 色温色环 / Material 缩略图 中，Animator 工程量
// 最小（纯只读 IAnimator API：BackendName + IsFinished），适合作 plugin
// 抽象验证型第一案。

#include "IEditorInspectorPlugin.h"

namespace Orange::Editor::Plugin
{

class AnimatorMiniPreviewPlugin : public IEditorInspectorPlugin
{
public:
    // 按 schema.typeName == "Animator" 字符串比较匹配（与 RegisterAnimator
    // ComponentSchema 内的 typeName 字面量保持一致）。
    bool CanHandle(const Orange::Editor::Schema::ComponentSchema& schema) const override;

    // ParseEnd 在默认字段渲染之后追加一行分隔符 + "Mini-Preview" 标签 +
    // 状态文本（Running / Finished，配色绿 / 灰）。完全只读，不入命令栈。
    void ParseEnd(EditorHost&                                            host,
                  Orange::Engine::Entity                                 entity,
                  const Orange::Editor::Schema::ComponentSchema&         schema,
                  void*                                                  component) override;
};

}  // namespace Orange::Editor::Plugin

#endif  // ORANGE_EDITOR_PLUGIN_ANIMATOR_MINI_PREVIEW_PLUGIN_H
