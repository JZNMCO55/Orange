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

namespace Orange::Engine::Asset
{
    class AssetRegistry;
}

std::unique_ptr<Orange::Engine::Asset::MeshAsset>
MakePlaneMesh(float halfSize);

std::unique_ptr<Orange::Engine::Asset::MeshAsset>
MakeCubeMesh(float halfSize);

// lat/lon UV-sphere（共享顶点路径，ComputeSmoothNormalsFromTriangles 自然
// 得到 normalize(position) 平滑法线）。与 sample 13_pbr_direct /
// 14_pbr_ibl 同款，编辑器 PBR showcase scene 复用。
std::unique_ptr<Orange::Engine::Asset::MeshAsset>
MakeSphereMesh(float radius, std::uint32_t lon, std::uint32_t lat);

// 注册 headless 资产导入（ImportDispatcher::DispatchToRegistry）落盘所必需的
// loader —— 当前是 Mesh + Texture（MeshLoader::Save/Load + TextureLoader 解码，
// 二者都纯 CPU，不依赖 Vulkan/GLFW/ImGui/RenderDevice/ThumbnailService）。
// InitializeEditorAssets 复用它（DRY），headless CLI / 测试也用它建一个最小
// AssetRegistry。每个 RegisterLoader 失败仅 log，不抛。
void RegisterImportLoaders(Orange::Engine::Asset::AssetRegistry& registry);

// 工厂：建一个仅注册了 import 必需 loader（Mesh + Texture）的最小 AssetRegistry，
// 供 headless 导入路径（CLI / ctest）使用。不触碰任何 GPU / GUI 子系统，也不
// 建 MaterialSystem / 内置 mesh-bake / 材质实例——纯 import 落盘够用。
std::unique_ptr<Orange::Engine::Asset::AssetRegistry> CreateImportAssetRegistry();

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

// EditorAssetContext& 重载 —— EnsureMaterialInstance 只用 host.assets，故下沉到
// 此可独立测试的 seam（EditorHost 聚合 ThumbnailService / AudioEngine，拖
// Vulkan / ImGui 无法 headless 链接，见 GltfMaterialImportTest 注释）。EditorHost&
// 版仅转调本版，零行为变化。
Orange::Engine::Render::MaterialInstance*
EnsureMaterialInstance(EditorAssetContext& assets, const std::string& materialPath);

#endif // ORANGE_EDITOR_BUILTIN_ASSETS_H
