#ifndef ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_FBX_SCENE_IMPORTER_H
#define ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_FBX_SCENE_IMPORTER_H

// ---------------------------------------------------------------------------
// FbxSceneImporter —— Autodesk FBX `.fbx` "scene-level" 导入（对标 glTF
// GltfSceneImporter 的 scene import 维度）。
//
// 与 FbxImporter（"asset import" 维度，把整个 .fbx 所有 mesh 塌平合并成单个
// MeshAsset）的区别：本模块走 "scene import" 维度 —— 遍历 FBX node 层级，
// **每个 node 一个 Entity**、**每个 mesh node 单独写一个 .mesh（不跨 node
// 合并、不丢层级）**，产出一份与 DCC 摆位同构的 `.scene.json` + 多个 `.mesh`。
// 对齐 Unity model prefab / Unreal scene import / Godot "import as scene"。
//
// 数据流：
//   .fbx (源) → OpenFBX 解析（保留 model node 层级）
//     → 遍历 node 树（从 getRoot() 起，按 connection 图取子 node）
//     → 每个 node 建 Entity（NameComponent / TransformComponent /
//       HierarchyComponent）；mesh node 额外写独立 .mesh（复用 FbxImporter
//       的 mesh / material 提取）+ 挂 RenderableComponent + per-mesh material
//       （单 material → materialInstance，多 material → SubMeshMaterialsComponent）
//     → Scene::Save 写 assets/scenes/<stem>.scene.json + scene .meta（source hash）
//
// 坐标系（FBX 头号坑，本模块的核心摩擦）：
//   * 顶点 / 法线沿用 FbxImporter 的 AxisConverter（Z-up→Y-up
//     (x,y,z)→(x,z,-y) + 信任已烘米）。
//   * **node local transform** 是相似变换（基变换）下的共轭：换轴矩阵 R
//     （Z-up→Y-up）下，同一 local 变换在引擎基里 = R · M_fbx_local · R⁻¹
//     （**不是**直接 R·M）。从 OpenFBX 拿 node local DMatrix → 共轭 →
//     decompose 成引擎 TRS（position / rotation / scale）。引擎 post-A1 沿
//     HierarchyComponent 累积父变换，故只存 local（与 GltfSceneImporter 一致）。
//
// 范围（与 FbxImporter MVP 对位）：
//   - 保留 hierarchy + 每 mesh 单独不塌平 + transform 树。
//   - per-mesh material：复用 FbxImporter 的 Phong→PBR 提取（diffuse →
//     uBaseColor，shininess → roughness，emissive；diffuse/normal/emissive
//     贴图 co-locate）。单 material → Renderable.materialInstance；多 material
//     → SubMeshMaterialsComponent。
//   - 只接受三角化几何（OpenFBX triangulate）。skinning / animation / blend
//     shape / camera / light skip（OpenFBX 全 IGNORE，与 FbxImporter 一致）。
//
// OpenFBX 头声明只在 .cpp 引用（ofbx.cpp / libdeflate.c 作为独立 TU 由 CMake
// 接进 target），本头不引 ofbx.h —— 保持链接边界清晰（与 FbxImporter.h 同款）。
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
// srcPath:  .fbx 源文件路径（不能为空）。
// registry: 注入 mesh 资产的目标 AssetRegistry（须已注册 Mesh loader）。
//           Scene::Save 用它把 RenderableComponent.mesh handle 反查为
//           assets/Models/<stem>/<name>.mesh 相对路径写进 scene.json。
//
// 成功时 result.destPath = 写出的 .scene.json 路径；result.message 含
// entity / mesh 计数。失败语义复用 ImportStatus（SourceReadFailed /
// CopyFailed / AssetLoadFailed / MetaWriteFailed）。
//
// importScale：FBX 单位 → 米的显式缩放（默认 1.0 = 信任已烘米，对 Blender 默认
// 导出正确；真 cm 文件传约 0.01）。同时作用于顶点 + node 平移，整场一致缩放。
// 见 FbxAxisConverter.h 的单位歧义说明。
ImportResult RunFbxSceneImportToRegistry(
    std::string_view srcPath,
    ::Orange::Engine::Asset::AssetRegistry& registry,
    float importScale = 1.0f);

}  // namespace Orange::Editor::Import

#endif  // ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_FBX_SCENE_IMPORTER_H
