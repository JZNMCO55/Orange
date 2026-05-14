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

void InitializeEditorAssets(EditorHost& host);

void SeedDemoWorld(EditorHost& host);

// 从 EditorAssetContext 构建 materialInstance 名称表，供 Scene::Save/Load 的
// namedMaterialInstances 字段使用。名字格式 "builtin/<key>"，与 .scene.json
// 里写出的 materialInstanceId 字符串对应。
std::unordered_map<std::string, Orange::Engine::Render::MaterialInstance*>
BuildNamedMaterialInstances(const EditorAssetContext& assets);

#endif  // ORANGE_EDITOR_DEMO_WORLD_H
