#ifndef ORANGE_EDITOR_DEMO_WORLD_H
#define ORANGE_EDITOR_DEMO_WORLD_H

// 编辑器启动期资产 / demo 世界种植：
//   * Make{Plane,Cube}Mesh —— 程序生成内置 mesh，与 samples/07_full_pipeline
//     的同名 helper 字段顺序一致；
//   * InitializeEditorAssets —— 一次性建好 AssetRegistry + 注册 ShaderLoader
//     + Insert cube/plane + MaterialSystem::RegisterBuiltins + CreateInstance
//     ("textured") × 2；失败仅 log，不抛；
//   * SeedDemoWorld —— 给 EditorState.pWorld 种 Root / Camera / Light /
//     Geometry / Floor / Wall / Misc Sibling 七个实体 + 父子关系 +
//     Camera::Perspective + Floor 平面 + Wall 立方 + RigidBody / Collider /
//     Light component；Scene 视口（S4 起）真正显示画面靠的就是它。
//
// 注意：场景 Save / Load 当前不串联 AssetRegistry，所以新开场景 / load 老
// 存档时 RenderableComponent 的 mesh / material handle 会失效；本头不解
// 决该问题（后续 task 把 AssetRegistry 也参与序列化）。

#include "EditorState.h"

#include <orange/engine/asset/MeshAsset.h>

#include <memory>

std::unique_ptr<Orange::Engine::Asset::MeshAsset>
MakePlaneMesh(float halfSize);

std::unique_ptr<Orange::Engine::Asset::MeshAsset>
MakeCubeMesh(float halfSize);

void InitializeEditorAssets(EditorState& state);

void SeedDemoWorld(EditorState& state);

#endif  // ORANGE_EDITOR_DEMO_WORLD_H
