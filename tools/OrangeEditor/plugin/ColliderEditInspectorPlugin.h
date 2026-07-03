#ifndef ORANGE_EDITOR_PLUGIN_COLLIDER_EDIT_INSPECTOR_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_COLLIDER_EDIT_INSPECTOR_PLUGIN_H

// ColliderEditInspectorPlugin —— Collider 段末的 "Edit Vertices in Viewport"
// 入口（engine-known-gaps GAP-2026-05-21）。
//
// 职责：当选中 entity 的 Collider 是 Polygon / Edge Chain 时，在 Inspector
// 段末（ParseEnd 钩子）追加一个按钮，点击进入 / 退出 viewport 顶点编辑子模式
// （host.colliderEdit）。Circle / Box 无顶点表 → 显示 disabled 提示。
//
// 与 AudioSourceInspectorPlugin 同款 "schema 渲染字段 + plugin 在段末追加自定义
// 动作 UI" 模式：shape 顶点表的数字编辑仍由 Collider schema 字段负责（保留），
// 本 plugin 只提供 "切到视口直接拖" 的入口，不接管整段。
//
// 选型理由：进入 / 退出 edit mode 是带副作用的动作（改 host.colliderEdit 全局
// 状态），不能走 schema 字段路径（schema 字段是纯数据描述）—— 与 AudioSource
// 试播 Play / Stop 按钮同款理由。

#include "IEditorInspectorPlugin.h"

namespace Orange::Editor::Plugin
{

    class ColliderEditInspectorPlugin : public IEditorInspectorPlugin
    {
    public:
        // 按 schema.typeName == "Collider" 匹配（与 RegisterColliderComponentSchema
        // 内 ComponentSchemaBuilder<CC>("Collider", ...) 字面量一致）。
        bool CanHandle(const Orange::Editor::Schema::ComponentSchema& schema) const override;

        // 段末追加 "Edit Vertices in Viewport" / "Exit Vertex Edit" 按钮；
        // 非 Polygon / EdgeChain shape 显示 disabled 提示。
        void ParseEnd(EditorHost&                                    host,
                      Orange::Engine::Entity                         entity,
                      const Orange::Editor::Schema::ComponentSchema& schema,
                      void*                                          component) override;
    };

} // namespace Orange::Editor::Plugin

#endif // ORANGE_EDITOR_PLUGIN_COLLIDER_EDIT_INSPECTOR_PLUGIN_H
