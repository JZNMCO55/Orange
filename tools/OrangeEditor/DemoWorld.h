#ifndef ORANGE_EDITOR_DEMO_WORLD_H
#define ORANGE_EDITOR_DEMO_WORLD_H

// 编辑器启动期资产 / demo 世界种植：
//   * Make{Plane,Cube}Mesh —— 程序生成内置 mesh；
//   * InitializeEditorAssets —— 一次性建好 AssetRegistry + 注册 ShaderLoader
//     + Insert cube/plane + MaterialSystem::RegisterBuiltins + 全部内置材质
//     实例（textured / toon / rim_light / dissolve / emissive）；失败仅 log，
//     不抛；
//   * SeedDemoWorld —— 种 13 个实体展示完整视觉栈：Root > Camera /
//     Sun（平行光+阴影）/ Geometry > Ground（textured）/ Backdrop（rim_light）
//     / Platform L + R（toon）/ Tower（toon）/ Glow Box（dissolve，自动动画）
//     / Emissive Pillar（emissive）/ Fire Emitter（粒子：火焰）/
//     Sparkle Emitter（粒子：萤火）。

#include "EditorHost.h"

#include <orange/engine/asset/MeshAsset.h>

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

void SeedDemoWorld(EditorHost& host);

// PBR showcase scene seeder：两组 3×3 球阵——左组暖橙 baseColor（对应
// sample 13_pbr_direct）/ 右组白色 baseColor（对应 sample 14_pbr_ibl furnace
// 测试）。每球独立 MaterialInstance（不同 metallic / roughness）。同时
// 挂 Camera 正前方 + Sun 暖光平行光 + Environment 占位（cubemap 空，等用户
// 后续拖 HDR 进去激活 IBL）。
//
// 调用前置：InitializeEditorAssets 必须先跑（assets 内 sphereMeshHandle +
// pbrShowcaseMaterials 都已 lazy-bake 完成）。直接传 targetWorld 而非通过
// host.scene.pWorld，便于 caller 在 temp World 内构造再 Save 落盘。
void SeedPbrShowcaseWorld(Orange::Engine::World& targetWorld,
                          const EditorAssetContext& assets);

// 从 EditorAssetContext 构建 materialInstance 名称表，供 Scene::Save/Load 的
// namedMaterialInstances 字段使用。名字格式 "builtin/<key>"，与 .scene.json
// 里写出的 materialInstanceId 字符串对应。
std::unordered_map<std::string, Orange::Engine::Render::MaterialInstance*>
BuildNamedMaterialInstances(const EditorAssetContext& assets);

#endif  // ORANGE_EDITOR_DEMO_WORLD_H
