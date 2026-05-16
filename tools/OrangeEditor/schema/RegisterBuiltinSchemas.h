#ifndef ORANGE_EDITOR_SCHEMA_REGISTER_BUILTIN_SCHEMAS_H
#define ORANGE_EDITOR_SCHEMA_REGISTER_BUILTIN_SCHEMAS_H

// RegisterBuiltinSchemas —— 编辑器启动期一次性把所有内置 component schema
// 注册进 ComponentSchemaRegistry。
//
// 调用方：tools/OrangeEditor/main.cpp，在创建 EditorHost 之前 / 紧随其后
// 调用一次。重复调用会触发 registry 内部的重复注册断言（开发期捕获）。
//
// v0.2.5 整骨进度（按 commit 顺序）：
//   * commit 3（当前）：DirectionalLight
//   * commit 4+：Transform / Name / Hierarchy / Renderable / RigidBody /
//                Collider / ParticleEmitter / Animator
//
// 注册完成后 EditorRenderLayer::DrawInspectorXxx 系列硬编码段对应地逐
// component 删除；commit 5/6 末尾整体清除"硬编码 fast path"。

#include <string>
#include <unordered_map>

namespace Orange::Engine::Asset { class AssetRegistry; }
namespace Orange::Engine::Render { class MaterialInstance; }

namespace Orange::Editor::Schema
{

void RegisterBuiltinSchemas();

// v0.5 c1：schema 注册侧需要的全局 AssetRegistry / namedMaterialInstances
// 注入入口。main.cpp 在 InitializeEditorAssets 之后调一次，让 Renderable
// 等 AssetRef 字段在 get/set lambda 内通过裸指针访问 path ↔ handle / ptr
// 映射。host 生命周期 ≥ schema 渲染，裸指针不悬挂。
void SetAssetRegistryForSchema(::Orange::Engine::Asset::AssetRegistry* p);
void SetNamedMaterialInstancesForSchema(
    std::unordered_map<std::string,
                       ::Orange::Engine::Render::MaterialInstance*>* p);

}  // namespace Orange::Editor::Schema

#endif  // ORANGE_EDITOR_SCHEMA_REGISTER_BUILTIN_SCHEMAS_H
