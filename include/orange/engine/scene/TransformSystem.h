#ifndef ORANGE_ENGINE_SCENE_TRANSFORM_SYSTEM_H
#define ORANGE_ENGINE_SCENE_TRANSFORM_SYSTEM_H

// ---------------------------------------------------------------------------
// TransformSystem —— 把 HierarchyComponent 的父子关系**传播成世界变换**。
//
// ADR-016（Transform 层级传播，方案 A）：每帧自顶向下 DFS 从 hierarchy 根
// 重算每个 entity 的 world matrix（= parentWorld * localTRS），写进各自的
// WorldTransformComponent（派生 cache，不序列化）。**全量重算，无 dirty-flag**
// ——2.5D 中等规模下成本可忽略，且避免隐蔽派生状态 bug；dirty-flag 留未来
// profiling 拉动（不破坏本消费接口）。
//
// 这修复 GAP-2026-06-02-hierarchy-transform-not-propagated：此前引擎渲染/光源
// 直接用 entity 自身 local Transform 当 world，parenting 对世界位置无效。
//
// 落地分步（A1.1）：
//   step 1（本函数 + WorldTransformComponent）—— **additive，零消费者、零行为
//          变化**：只产出 cache，渲染/光源仍走旧 local 路径。本阶段就是它。
//   step 2 —— 逐消费者（RenderScene::Collect / Pipeline 光向 / Physics / gizmo）
//          切到读 WorldTransformComponent；每切一个验回归。
//   A1.2  —— 迁移现有"local 当 world"的 scene/sample 内容 + 回退 glTF scene
//          import 的 world-bake workaround。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

namespace Orange::Engine
{
    class World;
}

namespace Orange::Engine::Scene
{

    // 自顶向下从 hierarchy 根累积 world matrix 写进每个 entity 的
    // WorldTransformComponent（emplace_or_replace）。无 HierarchyComponent 或
    // parent==Invalid 的 entity 视为根（world = 自身 local matrix）。无
    // TransformComponent 的 entity 取 identity local。要求 hierarchy 无环
    // （编辑器/importer 保证）。
    ORANGE_ENGINE_API void PropagateWorldTransforms(World& world);

} // namespace Orange::Engine::Scene

#endif // ORANGE_ENGINE_SCENE_TRANSFORM_SYSTEM_H
