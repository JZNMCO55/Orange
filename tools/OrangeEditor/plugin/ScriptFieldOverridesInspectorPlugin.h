#ifndef ORANGE_EDITOR_PLUGIN_SCRIPT_FIELD_OVERRIDES_INSPECTOR_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_SCRIPT_FIELD_OVERRIDES_INSPECTOR_PLUGIN_H

// ScriptFieldOverridesInspectorPlugin —— Inspector 内 Script 段末的 fieldOverrides
// 列表编辑器（ADR-017 B1.3 authored tweakable）。
//
// 职责：在 Script 段末（ParseEnd 钩子）追加一个 "Field Overrides" 列表 UI——
// 增 / 删一条 override + 编辑每条的 name（string）/ type（Float/Int/Bool/String
// 下拉）/ value（string）。
//
// 选型理由：fieldOverrides 是 `std::vector<ScriptFieldOverride{name, type(enum),
// value}>`——"动态长度 + 每行 3 个异构字段"的 struct 数组，超出 schema 通用
// PropertyType 的标量 / 固定长度数组（AssetRefArray）表达力。为它单独新增一个
// PropertyType + 改 SchemaInspector 是大改架构、且只有这一个消费者；按
// IEditorInspectorPlugin 头注释的"装饰式扩展"约定，列表编辑走 plugin ParseEnd
// 钩子做自定义 ImGui UI（与 ColliderEditInspectorPlugin 顶点表 / AudioSource
// Play 按钮同款分工：schema 管标量字段，plugin 管超出字段控件的 UI）。
//
// 命令栈：本 plugin 直接 mutate component（与 ColliderEditInspectorPlugin 顶点
// 编辑同款），增 / 删 / 编辑不接 Undo 命令栈——fieldOverrides 是编辑期 authored
// 数据，且复合 struct 数组的逐字段 Undo 粒度需要专门命令类型，超出本任务 P1
// 范围。脏标记仍由 Inspector 主路径的常规保存流程承载（用户改完 Ctrl+S 落盘）。

#include "IEditorInspectorPlugin.h"

namespace Orange::Editor::Plugin
{

class ScriptFieldOverridesInspectorPlugin : public IEditorInspectorPlugin
{
public:
    // 按 schema.typeName == "Script" 字符串比较（与 RegisterScriptComponentSchema
    // 内字面量保持一致）。
    bool CanHandle(const Orange::Editor::Schema::ComponentSchema& schema) const override;

    // 段末追加 fieldOverrides 列表 UI（增 / 删 / 编辑每条 name / type / value）。
    void ParseEnd(EditorHost&                                            host,
                  Orange::Engine::Entity                                 entity,
                  const Orange::Editor::Schema::ComponentSchema&         schema,
                  void*                                                  component) override;
};

}  // namespace Orange::Editor::Plugin

#endif  // ORANGE_EDITOR_PLUGIN_SCRIPT_FIELD_OVERRIDES_INSPECTOR_PLUGIN_H
