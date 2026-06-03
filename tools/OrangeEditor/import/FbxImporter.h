#ifndef ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_FBX_IMPORTER_H
#define ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_FBX_IMPORTER_H

// ---------------------------------------------------------------------------
// FbxImporter —— Autodesk FBX `.fbx` 静态 mesh + 材质导入流水线（MVP）。
//
// 数据流（对标 GltfImporter / ObjImporter 的单 mesh asset 路径）：
//   .fbx (源) → OpenFBX 解析 → 遍历所有 mesh 的 GeometryData
//     → 按 material partition triangulate + 提取 positions/normals/uvs
//     → 坐标系转换（FBX up-axis / unit scale → 引擎 Y-up / 米）
//     → 合并全部 mesh 到一个 unified MeshAsset（多 material → sub-mesh slot）
//     → MikkTSpace 切线（有 UV+normal 时）→ MeshLoader::Save
//     → assets/Models/<stem>/<stem>.mesh + per-material .material + .meta sidecar
//     → AssetRegistry::Load<MeshAsset>
//
// MVP 范围（与 GltfImporter 单 mesh asset 导入对位）：
//   - 多 mesh / 多 material **塌平合并**为单个 MeshAsset；多 material 拆
//     sub-mesh slot（对标 GltfImporter）。不做 scene-level 层级（后续，像 glTF G1）
//   - FBX material（Phong / Lambert）→ PBR 口径：diffuse → uBaseColor，
//     shininess → roughness，emissive → uEmissive；diffuse/normal/emissive
//     贴图 → ImportTexture co-locate。无 metallic/roughness 贴图（FBX 标准
//     无对应通道，标量按 Phong 推导）
//   - 无 skinning / 无 animation / 无 blend shape（MVP 不做，OpenFBX 全 IGNORE）
//
// 坐标系：FBX 常 Z-up + cm 单位。按 GlobalSettings 的 UpAxis + UnitScaleFactor
// 转到引擎 Y-up + 米（这是 FBX 头号坑，importer 内统一处理 + 测试断言）。
// ---------------------------------------------------------------------------

#include "ImportDispatcher.h"  // ImportResult / MaterialRegisterFn

struct EditorHost;

namespace Orange::Engine::Asset
{
class AssetRegistry;
}

namespace Orange::Editor::Import
{

// Headless 核心实现：只依赖 AssetRegistry&，不出现 EditorHost。本头单独存在让
// OpenFBX vendor TU（ofbx.cpp / libdeflate.c）的链接边界清晰。onMaterialWritten
// 在写出 .material sidecar 后回调（GUI 路径注入 EnsureMaterialInstance；headless
// 传空 = 不注册编辑器缓存，材质文件仍照常写盘）。
// importScale：FBX 单位 → 米的显式缩放（默认 1.0 = 信任已烘米，对 Blender 默认
// 导出正确；真 cm 文件传约 0.01）。见 FbxAxisConverter.h 的单位歧义说明。
ImportResult RunFbxImportToRegistry(std::string_view srcPath,
                                    ::Orange::Engine::Asset::AssetRegistry& registry,
                                    const MaterialRegisterFn& onMaterialWritten = {},
                                    float importScale = 1.0f);

// GUI 包装：委托到 RunFbxImportToRegistry，注入把刚写出的 .material 注册进 host
// 编辑器缓存的 EnsureMaterialInstance 回调。
ImportResult RunFbxImport(std::string_view srcPath, EditorHost& host);

}  // namespace Orange::Editor::Import

#endif  // ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_FBX_IMPORTER_H
