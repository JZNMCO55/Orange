#ifndef ORANGE_EDITOR_SCHEMA_ASSET_REF_SIDE_EFFECTS_H
#define ORANGE_EDITOR_SCHEMA_ASSET_REF_SIDE_EFFECTS_H

// AssetRef 字段写入的"前置 / 后置副作用"——GUI（SchemaInspector）与 MCP
// （set_field）共用同一实现，避免两处平行重复而 MCP 漏掉某个副作用。
//
// 背景：写一个 AssetRef 字段（Renderable.materialInstance / Renderable.mesh 等）
// 时，光调 schema 的 assetRefSet 还不够：
//   * Material：assetRefSet（materialSet）只在 namedMaterialInstances 表里查，
//     找不到就静默 no-op（materialInstance 不变）。刚导入 / 新建的 .material 不
//     在表里 → 设不上但写入"看似成功"。必须在 set 之前先 EnsureMaterialInstance
//     把它 lazy 注册进表。MCP set_field 早先漏掉这一步即 F1（经 MCP 设
//     materialInstance 不视觉生效）。
//   * Mesh：设 mesh 后要把多材质 slot 同步到 SubMeshMaterialsComponent，与
//     viewport drop / Inspector 行为一致。
// 顺序：Prepare（写前）→ assetRefSet（写）→ Finish（写后）。

#include "../EditorHost.h"

#include <orange/engine/scene/Entity.h>

#include <string>

namespace Orange::Editor::Schema
{
struct PropertyDescriptor;
}

namespace Orange::Editor
{

// 写 AssetRef 字段前的副作用。Material 字段先把 path 对应 .material lazy 注册到
// namedMaterialInstances（否则 materialSet 查表 miss → 静默 no-op）。返回 false
// 表示 Material instance 创建失败（文件缺失 / 解析失败 / template 未注册），
// 调用方应中止写入并报错；非 Material 字段 / 空 path 恒返回 true。
bool PrepareAssetRefWrite(EditorHost&                       host,
                          const Schema::PropertyDescriptor& prop,
                          const std::string&                path);

// 写 AssetRef 字段后的副作用。Mesh 字段把多材质 slot 同步到
// SubMeshMaterialsComponent（前置：caller 已把 mesh 设到 Renderable）。非 Mesh
// 字段为 no-op。
void FinishAssetRefWrite(EditorHost&                       host,
                         Orange::Engine::Entity            entity,
                         const Schema::PropertyDescriptor& prop,
                         const std::string&                path);

}  // namespace Orange::Editor

#endif  // ORANGE_EDITOR_SCHEMA_ASSET_REF_SIDE_EFFECTS_H
