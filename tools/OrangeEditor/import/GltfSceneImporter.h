#ifndef ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_GLTF_SCENE_IMPORTER_H
#define ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_GLTF_SCENE_IMPORTER_H

// ---------------------------------------------------------------------------
// GltfSceneImporter —— glTF 2.0 "scene-level" 导入（GAP-2026-05-28 G1）。
//
// 与 GltfImporter（"asset import" 维度，把整个 .gltf 塌平合并成单个
// MeshAsset）的区别：本模块走 "scene import" 维度 —— 遍历 glTF
// scenes[0].nodes 的 transform 树，**每个 node 一个 Entity**、**每个
// cgltf mesh 单独写一个 .mesh（不跨 mesh 合并、不丢层级）**，产出一份
// 与 DCC 摆位同构的 `.scene.json` + 多个 `.mesh`。对齐 Unity model prefab /
// Unreal scene import / Godot ".glb as scene" / Lumix per-mesh import。
//
// 数据流：
//   .gltf / .glb → cgltf 解析 + buffer 加载
//     → 为每个被引用的 cgltf mesh 写出独立 .mesh（merge 该 mesh 自身的
//       primitive，但不跨 mesh 合并）+ .meta，Load 进 AssetRegistry
//     → 遍历 scene node 树，在临时 World 内建 Entity（TransformComponent /
//       HierarchyComponent / NameComponent；带 mesh 的 node 加
//       RenderableComponent 指向对应 .mesh handle）
//     → Scene::Save 写出 assets/scenes/<basename>.scene.json（复用引擎序列化，
//       Hierarchy 索引链由序列化层保证自洽）+ scene .meta（source hash）
//
// 范围（与 gap 文档 GAP-2026-05-28 拆解对齐）：
//   - G1 ✅ hierarchy + 每 mesh 单独不塌平 + transform 树。
//   - G3（部分 ✅）scene-level lights：消费 KHR_lights_punctual →
//     DirectionalLight / PointLight / SpotLight（方向沿 glTF -Z 转引擎 -Y）。
//     **intensity 单位未映射**（glTF lux/candela vs 引擎无单位乘子，忠实透传，
//     导入后需手调）；cameras 仍延后。
//   - G2（未做）per-mesh PBR material 划分：本阶段 RenderableComponent.material
//     留空（默认材质）。材质需 MaterialInstance 对象做 Scene::Save 反查，
//     headless 路径不构造 —— 留 G2（PBR material 基础设施已落地，单列子缺口）。
//   - 只接受 triangle primitive；skinning / morph / animation skip（v1.x 长尾）。
//
// cgltf IMPLEMENTATION 宏仅在 GltfImporter.cpp 一处 expand；本 TU 只取 cgltf
// 声明（同一 lib / test exe 内链 GltfImporter.cpp 提供的实现）。
// ---------------------------------------------------------------------------

#include "ImportDispatcher.h"  // ImportResult / ImportStatus

namespace Orange::Engine::Asset
{
class AssetRegistry;
}

namespace Orange::Editor::Import
{

// Headless 核心实现：只依赖 AssetRegistry&，不出现 EditorHost。
//
// srcPath:  .gltf / .glb 源文件路径（不能为空）。
// registry: 注入 mesh 资产的目标 AssetRegistry（须已注册 Mesh loader）。
//           Scene::Save 用它把 RenderableComponent.mesh handle 反查为
//           assets/Models/<basename>/<name>.mesh 相对路径写进 scene.json。
//
// 成功时 result.destPath = 写出的 .scene.json 路径；result.message 含
// entity / mesh 计数。失败语义复用 ImportStatus（SourceReadFailed /
// CopyFailed / AssetLoadFailed / MetaWriteFailed）。
ImportResult RunGltfSceneImportToRegistry(
    std::string_view srcPath,
    ::Orange::Engine::Asset::AssetRegistry& registry);

}  // namespace Orange::Editor::Import

#endif  // ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_GLTF_SCENE_IMPORTER_H
