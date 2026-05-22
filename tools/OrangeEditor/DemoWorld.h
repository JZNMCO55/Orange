#ifndef ORANGE_EDITOR_DEMO_WORLD_H
#define ORANGE_EDITOR_DEMO_WORLD_H

// 编辑器 demo 场景填充：仅做"种 entity / 配 component"，不负责启动期 builtin
// 资产层（mesh 工厂 / AssetRegistry 注册 / 内置材质实例 / namedMaterialInstances
// 表 —— 那些迁到 [[BuiltinAssets]]，v1.0.1 c11 拆分）。
//
//   * SeedDemoWorld —— 种 13 个实体展示完整视觉栈：Root > Camera /
//     Sun（平行光+阴影）/ Geometry > Ground（textured）/ Backdrop（rim_light）
//     / Platform L + R（toon）/ Tower（toon）/ Glow Box（dissolve，自动动画）
//     / Emissive Pillar（emissive）/ Fire Emitter（粒子：火焰）/
//     Sparkle Emitter（粒子：萤火）。
//   * SeedPbrShowcaseWorld —— 两组 3×3 球阵（暖橙 + 白 furnace 测试），
//     pbr_showcase.scene.json 的程序化对应物。
//
// 调用前置：InitializeEditorAssets（[[BuiltinAssets]]）必须先跑 —— 两个
// Seed 函数都依赖 host.assets 内已 lazy-bake 完成的 mesh / material handle。

#include "EditorHost.h"

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

#endif  // ORANGE_EDITOR_DEMO_WORLD_H
