#ifndef ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_OBJ_IMPORTER_H
#define ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_OBJ_IMPORTER_H

// ---------------------------------------------------------------------------
// ObjImporter —— Wavefront `.obj` 导入流水线（v1.1 T3）。
//
// 数据流：
//   .obj (源) → tinyobjloader 解析（attrib + shapes，三角化 + 顶点 dedup）
//             → MeshAsset (positions/uvs/normals/indices) → MeshLoader::Save
//             → assets/Models/<basename>.mesh (v3 二进制) + 同款 .meta sidecar
//             → AssetRegistry::Load<MeshAsset> 入仓
//
// 同时把源 .obj copy 到 `assets/Models/<basename>.obj`（ADR-008 议题 B2
// "copy to assets/<TypeDir>/"）—— 这样 source asset 跟 cooked .mesh 同目
// 录，未来 reimport 时按 .meta 里的 sourcePath 找回，跨机器 / VCS clone
// 不依赖外部位置。
//
// 顶点 dedup：tinyobjloader 给 face-vertex 三元组 (pos_idx, norm_idx, uv_idx)；
// 不同 face 共享同一三元组 → 同一 unified vertex。用 unordered_map 去重，
// 产物是引擎需要的 indexed mesh 形态（vertex array per attribute 同长度 +
// 32-bit index array）。
//
// 不在 T3 范围：
//   - .mtl material 解析 → v1.x 长尾，需 PBR material schema 配对
//   - normal / tangent 自动补（mikktspace）→ v1.2 F3 推动
//   - 几何 LOD / 顶点优化 → 长期不做（导入 fidelity 优先）
// ---------------------------------------------------------------------------

#include "ImportDispatcher.h"

struct EditorHost;

namespace Orange::Engine::Asset
{
class AssetRegistry;
}

namespace Orange::Editor::Import
{

// Headless 核心实现（GAP-2026-05-27 G1）：只依赖 AssetRegistry&，不出现
// EditorHost。把实际实现挪出 ImportDispatcher.cpp 让 tinyobjloader
// IMPLEMENTATION 宏只在 ObjImporter.cpp 一处 expand，避免多 TU 重定义
// （v0.7 Pipeline.cpp 拆分时撞 stb single-header 多 TU 冲突的教训，参
// [[project-pipeline-split-in-progress]] memory）。
ImportResult RunObjImportToRegistry(std::string_view srcPath,
                                    ::Orange::Engine::Asset::AssetRegistry& registry);

// GUI 包装：委托到 RunObjImportToRegistry（obj 无 material 注册副作用，
// 直接转发 host.assets.pAssets）。
ImportResult RunObjImport(std::string_view srcPath, EditorHost& host);

}  // namespace Orange::Editor::Import

#endif  // ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_OBJ_IMPORTER_H
