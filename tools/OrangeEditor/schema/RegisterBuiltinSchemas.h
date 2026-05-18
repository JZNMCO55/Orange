#ifndef ORANGE_EDITOR_SCHEMA_REGISTER_BUILTIN_SCHEMAS_H
#define ORANGE_EDITOR_SCHEMA_REGISTER_BUILTIN_SCHEMAS_H

// RegisterBuiltinSchemas —— 编辑器启动期一次性把所有内置 component schema
// 注册进 ComponentSchemaRegistry。
//
// 调用方：tools/OrangeEditor/main.cpp，在创建 EditorHost 之前 / 紧随其后
// 调用一次。重复调用会触发 registry 内部的重复注册断言（开发期捕获）。

struct EditorAssetContext;

namespace Orange::Editor::Schema
{

void RegisterBuiltinSchemas();

// v0.8 整骨（消除 L15）：单一 EditorAssetContext 注入入口，替代原
// SetAssetRegistryForSchema + SetNamedMaterialInstancesForSchema 两个独立
// setter。main.cpp 在 InitializeEditorAssets + BuildNamedMaterialInstances
// 完成后调一次 SetEditorAssetContextForSchema(&editorHost.assets)；schema
// 内 AssetRef get/set lambda 通过 gpAssetContext 访问 pAssets +
// namedMaterialInstances 两段数据。host 生命周期 ≥ schema 渲染，裸指针不
// 悬挂。
void SetEditorAssetContextForSchema(const EditorAssetContext* p);

}  // namespace Orange::Editor::Schema

#endif  // ORANGE_EDITOR_SCHEMA_REGISTER_BUILTIN_SCHEMAS_H
