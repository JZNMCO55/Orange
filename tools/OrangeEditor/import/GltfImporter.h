#ifndef ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_GLTF_IMPORTER_H
#define ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_GLTF_IMPORTER_H

// ---------------------------------------------------------------------------
// GltfImporter —— glTF 2.0 `.gltf` / `.glb` 导入流水线（v1.1 T4）。
//
// 数据流：
//   .gltf / .glb (源) → cgltf 解析 + buffer 加载 → 遍历 meshes / primitives
//     → 合并全部 triangle primitive 的 attributes 到一个 unified MeshAsset
//     → MeshLoader::Save → assets/Models/<basename>.mesh + .meta sidecar
//     → AssetRegistry::Load<MeshAsset>
//
// T4 范围限制（与 T3 .obj 对位）：
//   - 多 primitive / 多 mesh **合并**为单个 MeshAsset，丢失 per-primitive
//     material 划分；理由：ADR-008 决策 "PBR material 解析延 v1.2"，T4 阶
//     段不消费 material；多 primitive 拆 sub-mesh 需引擎 sub-mesh API + 多
//     material slot，超出 v1.1 范围
//   - 只接受 triangle primitive；point / line / strip 类型 skip + log warn
//   - skinning / morph targets / animation 全部 skip（v1.x 长尾）
//   - .glb 自带 embedded buffer + .gltf 外部 buffer 都支持（cgltf 自动）
//
// 顶点 dedup：cgltf 已经给出 indexed primitive；无需像 .obj 那样 face-vertex
// 三元组 dedup。直接拷贝 attribute 数组 + 调整 index offset 拼接到 unified
// arrays。
// ---------------------------------------------------------------------------

#include "ImportDispatcher.h"  // ImportResult / MaterialRegisterFn

struct EditorHost;

namespace Orange::Engine::Asset
{
class AssetRegistry;
}

namespace Orange::Editor::Import
{

// Headless 核心实现（GAP-2026-05-27 G1）：只依赖 AssetRegistry&，不出现
// EditorHost。本头单独存在让 cgltf CGLTF_IMPLEMENTATION 仅在 GltfImporter.cpp
// 一处 expand。onMaterialWritten 在写出 .material sidecar 后回调（GUI 路径注入
// EnsureMaterialInstance；headless 传空 = 不注册编辑器缓存，材质文件仍照常写盘）。
ImportResult RunGltfImportToRegistry(std::string_view srcPath,
                                     ::Orange::Engine::Asset::AssetRegistry& registry,
                                     const MaterialRegisterFn& onMaterialWritten = {});

// GUI 包装：委托到 RunGltfImportToRegistry，注入把刚写出的 .material 注册进
// host 编辑器缓存的 EnsureMaterialInstance 回调。
ImportResult RunGltfImport(std::string_view srcPath, EditorHost& host);

}  // namespace Orange::Editor::Import

#endif  // ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_GLTF_IMPORTER_H
