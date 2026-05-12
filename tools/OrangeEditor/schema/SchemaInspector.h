#ifndef ORANGE_EDITOR_SCHEMA_SCHEMA_INSPECTOR_H
#define ORANGE_EDITOR_SCHEMA_SCHEMA_INSPECTOR_H

// SchemaInspector —— schema-driven Inspector 渲染入口。
//
// 替代 v0.2 期 EditorRenderLayer::DrawInspectorXxx 系列硬编码段：
// 之前每个内置 component 要手写一个 Draw 段（9 个成员函数），现在
// 走 ComponentSchema → 自动生成 ImGui 控件 + SetFieldValueCommand。
//
// 公开两个函数：
//   * DrawComponentSchemaSection —— 渲染单个 component 的 schema 段
//     （CollapsingHeader + 所有 property 控件 + 右键 Remove）。EditorHost 提
//     供 CommandStack + Inspector 编辑历史所需的 selectedEntity / world 上下文
//   * DrawEntityViaSchemas —— 遍历 registry 已注册所有 schema，对当前
//     entity 已挂的每个 component 调 DrawComponentSchemaSection
//
// 整段 Inspector 编辑都在 host.scene.playState == Edit 时启用；Play /
// Paused 期间不在此 disable，由 caller（InspectorPanel）外侧用
// `ImGui::BeginDisabled(!canEdit)` 统一包裹。

#include "ComponentSchema.h"

#include <orange/engine/scene/Entity.h>

struct EditorHost;

namespace Orange::Editor::Schema
{

// 渲染单个 component schema 段。caller 已确认 entity 上挂着该 schema
// 对应的 component（schema.has(world, entity) == true）。
void DrawComponentSchemaSection(EditorHost&                  host,
                                Orange::Engine::Entity       entity,
                                const ComponentSchema&       schema);

// 遍历 registry 已注册的所有 schema，对 entity 已挂的 component 画段。
// 注册顺序 = 显示顺序。未挂的 schema 直接跳过（不画空段）。
void DrawEntityViaSchemas(EditorHost& host, Orange::Engine::Entity entity);

}  // namespace Orange::Editor::Schema

#endif  // ORANGE_EDITOR_SCHEMA_SCHEMA_INSPECTOR_H
