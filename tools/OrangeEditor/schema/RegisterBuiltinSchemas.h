#ifndef ORANGE_EDITOR_SCHEMA_REGISTER_BUILTIN_SCHEMAS_H
#define ORANGE_EDITOR_SCHEMA_REGISTER_BUILTIN_SCHEMAS_H

// RegisterBuiltinSchemas —— 编辑器启动期一次性把所有内置 component schema
// 注册进 ComponentSchemaRegistry。
//
// 调用方：tools/OrangeEditor/main.cpp，在创建 EditorHost 之前 / 紧随其后
// 调用一次。重复调用会触发 registry 内部的重复注册断言（开发期捕获）。
//
// v0.9.5 c3：AssetRef get/set 改走 PropertyDescriptor::AssetRefGetFn /
// AssetRefSetFn 新签名（带 `const EditorAssetContext&` 参数），SchemaInspector
// 在 dispatch + 命令栈 replay 显式传 ctx。原 SetEditorAssetContextForSchema
// 启动期注入路径不再需要，已下架。

namespace Orange::Editor::Schema
{

    void RegisterBuiltinSchemas();

} // namespace Orange::Editor::Schema

#endif // ORANGE_EDITOR_SCHEMA_REGISTER_BUILTIN_SCHEMAS_H
