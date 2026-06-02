#ifndef ORANGE_EDITOR_EDITOR_ASSET_DROP_HANDLER_H
#define ORANGE_EDITOR_EDITOR_ASSET_DROP_HANDLER_H

// ---------------------------------------------------------------------------
// EditorAssetDropHandler —— "把 .material / .mesh / .wav 等资源拖到实体上
// 时怎么 apply" 的单点路由（v1.2.3 patch 引入）。
//
// 两个 caller：
//   * EntityTreePanel —— Hierarchy 行 BeginDragDropTarget 接到 ORANGE_ASSET
//     payload（直接拿到 target entity）
//   * ScenePanel —— viewport ImGui::Image 区域 BeginDragDropTarget +
//     PickEntityAt(光标 ndc) 找到命中 entity 后调本 helper
//
// 分派规则（按 path 文件扩展名）：
//   * .material            → Renderable.materialInstance（需 entity 有 RenderableComponent）
//   * .mesh / .obj         → Renderable.mesh（同上前置）
//   * .wav/.ogg/.mp3/.flac → AudioSource.sound（需 AudioSourceComponent）
//   * 其他扩展名           → log warn + 静默忽略
//
// 命令栈集成：每条 apply 都走 `SetFieldValueCommand<std::string>`，支持
// Undo / Redo（与 EditorRenderLayer "Pick to Renderable.material" 同款）。
//
// 失败语义（宽容口径）：entity invalid / 缺所需 component / 不识别扩展名
// 都 silent skip + log warn，不抛错。返回 true 表示已 push 命令；false
// 表示 skip（caller 可按需做视觉反馈）。
// ---------------------------------------------------------------------------

#include <orange/engine/scene/Entity.h>

#include <glm/vec3.hpp>

#include <string>

struct EditorHost;

namespace Orange::Editor
{

bool ApplyAssetDropToEntity(EditorHost&                 host,
                            Orange::Engine::Entity      target,
                            const std::string&          assetPath);

// 把一个 .mesh / .obj 资源在指定世界位置创建成一个新实体（拖资源到 viewport
// 空白处时用 —— 对齐 Unity / Lumix 拖模型进空场景生成 GameObject）。新实体挂
// Name(<stem>) + Transform(position) + Renderable(mesh + 材质)；多材质 mesh 一并
// 挂 SubMeshMaterialsComponent。走 CreateEntityCommand 命令栈（可 Undo / Redo
// ——材质在 factory 内由**预解析**的 MaterialInstance* 构建，redo 重放一致）。
// 非 mesh 扩展名 / mesh 加载失败 / world 缺失 → 返回 Invalid + log warn，不创建。
// 返回创建出的实体（caller 可据此设选中）。
Orange::Engine::Entity
CreateEntityFromMeshAsset(EditorHost&        host,
                          const std::string& meshPath,
                          const glm::vec3&   position);

// 把一个（已设到 entity 的 Renderable 上的）mesh 的多材质 slot 同步到
// SubMeshMaterialsComponent —— 多 material mesh（MeshAsset::HasSubMeshes()）回读
// 同名 `.meta` 的 subMeshMaterials，按 slot 经 EnsureMaterialInstance 挂
// SubMeshMaterialsComponent + slot 0 兜底 Renderable.materialInstance；单 material
// mesh / 空 path / 无 .meta 映射则撤掉残留组件。
//
// 调用方（两条都需"设 mesh 后"调本函数，保证 viewport drop 与 Inspector 设
// mesh 字段行为一致）：
//   * ApplyAssetDropToEntity 的 mesh 分支（drop 到实体）——内部调用
//   * SchemaInspector 的 Renderable.mesh AssetRef 字段 apply（Inspector 设/DnD/Pick）
//
// 前置：caller 已把 meshPath 对应 mesh 设到 entity 的 Renderable.mesh（本函数
// 不设 mesh，只按 meshPath 回读 .meta + 同步 sub-mesh 材质）。meshPath 为空 =
// 清 mesh，撤掉组件。命令 apply 内调用 → undo/redo 重放时按当时 mesh 重新派生。
void SyncSubMeshMaterialsForMesh(EditorHost&            host,
                                 Orange::Engine::Entity entity,
                                 const std::string&     meshPath);

}  // namespace Orange::Editor

#endif  // ORANGE_EDITOR_EDITOR_ASSET_DROP_HANDLER_H
