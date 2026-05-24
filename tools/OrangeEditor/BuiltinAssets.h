#ifndef ORANGE_EDITOR_BUILTIN_ASSETS_H
#define ORANGE_EDITOR_BUILTIN_ASSETS_H

// 编辑器启动期"必备 builtin 资产"层 —— 与 [[DemoWorld]] 拆开（DemoWorld 仅
// 负责 demo 场景填充，本文件负责"无论有没有 demo 都必须就位"的程序化资产）：
//
//   * Make{Plane,Cube,Sphere}Mesh —— 程序化生成 builtin 几何体；
//   * InitializeEditorAssets —— 一次性建好 AssetRegistry + 注册 ShaderLoader /
//     TextureLoader / MeshLoader / SoundLoader / SkeletonLoader + lazy bake
//     assets/meshes/{cube,plane,sphere}.mesh + assets/sounds/beep.wav +
//     MaterialSystem::RegisterBuiltins + 所有内置材质实例（textured / toon /
//     rim_light / dissolve / emissive / pbr / pbr-showcase 18 个）。失败仅
//     log，不抛。
//   * BuildNamedMaterialInstances —— 返回 path → MaterialInstance* 表，供
//     Scene::Save/Load 的 namedMaterialInstances 字段使用。名字格式
//     "assets/materials/builtin/<key>.material"，与 .scene.json 里写出的
//     materialInstanceId 字符串对应。
//
// v1.0.1 c11：原属于 DemoWorld.{h,cpp} 的 builtin asset 工厂迁出，让
// DemoWorld 名字真正只承担"demo 场景"职责。零行为变化。

#include "EditorHost.h"

#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/render/MaterialInstance.h>

#include <memory>
#include <string>
#include <unordered_map>

std::unique_ptr<Orange::Engine::Asset::MeshAsset>
MakePlaneMesh(float halfSize);

std::unique_ptr<Orange::Engine::Asset::MeshAsset>
MakeCubeMesh(float halfSize);

// lat/lon UV-sphere（共享顶点路径，ComputeSmoothNormalsFromTriangles 自然
// 得到 normalize(position) 平滑法线）。与 sample 13_pbr_direct /
// 14_pbr_ibl 同款，编辑器 PBR showcase scene 复用。
std::unique_ptr<Orange::Engine::Asset::MeshAsset>
MakeSphereMesh(float radius, std::uint32_t lon, std::uint32_t lat);

void InitializeEditorAssets(EditorHost& host);

// 从 EditorAssetContext 构建 materialInstance 名称表，供 Scene::Save/Load 的
// namedMaterialInstances 字段使用。名字格式 "assets/materials/builtin/<key>
// .material"，与 .scene.json 里写出的 materialInstanceId 字符串对应。
std::unordered_map<std::string, Orange::Engine::Render::MaterialInstance*>
BuildNamedMaterialInstances(const EditorAssetContext& assets);

// v1.2.4 patch · 按 path 获取 live MaterialInstance（含 lazy create 兜
// 底）。统一了 v1.2.2 Inspector 路径 + v1.2.3 ApplyAssetDropToEntity 路径
// 的 lazy create 逻辑——避免 DnD apply 拖一个未被 Inspector 选过的新
// .material 时 BuildNamedMaterialInstances 找不到 → 设 nullptr 设回去
// （v1.2.3 验收 bug 根因）。
//
// 行为：
//   1. 先查 BuildNamedMaterialInstances 已有 → 命中即返回 raw ptr
//   2. 不在 map → ReadMaterialFile 拿 templateName + override → MaterialSystem::
//      CreateInstance + ApplyDataToInstance → own 到 host.assets.userMaterials
//      → 返回 raw ptr
//   3. 任一步失败（文件不存在 / 解析失败 / templateName 空 / template 未
//      注册）→ 返回 nullptr + log warn
//
// 调用方按 nullptr 决定是否继续（DnD apply 时设回 nullptr / Inspector
// 显示 "no live instance" 等）。
Orange::Engine::Render::MaterialInstance*
EnsureMaterialInstance(EditorHost& host, const std::string& materialPath);

#endif  // ORANGE_EDITOR_BUILTIN_ASSETS_H
