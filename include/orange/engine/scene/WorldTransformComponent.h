#ifndef ORANGE_ENGINE_SCENE_WORLD_TRANSFORM_COMPONENT_H
#define ORANGE_ENGINE_SCENE_WORLD_TRANSFORM_COMPONENT_H

// ---------------------------------------------------------------------------
// WorldTransformComponent —— 实体的**世界变换矩阵 cache**（派生量，不序列化）。
//
// 由 `TransformSystem::PropagateWorldTransforms` 每帧从 HierarchyComponent
// 自顶向下累积重算：world = parentWorld * localTRS（local 由 TransformComponent
// 合成）。消费者（Render 收集 drawable / 光源方向 / Physics 同步 / gizmo）
// **只读本组件，不再各自从 local Transform 自算 world**——这是 ADR-016
// （Transform 层级传播，方案 A）确立的不变量。
//
// 为什么不序列化：world 是派生量（= hierarchy + 各 local TRS 的函数），写盘
// 会双源真相。沿用 TransformComponent "不缓存派生量" 的同款纪律——区别只是
// 把"world 在哪算"从 per-drawable 提升到 per-frame transform pass + 共享 cache。
//
// 生命周期：transient——每帧被 PropagateWorldTransforms 覆盖重写（emplace_or_
// replace）。未跑过 pass 的 entity 没有本组件（消费者需 null-guard 退回 local，
// 或保证 pass 在消费前跑过）。
// ---------------------------------------------------------------------------

#include <glm/mat4x4.hpp>

namespace Orange::Engine::Scene
{

struct WorldTransformComponent
{
    glm::mat4 world{1.0f};
};

}  // namespace Orange::Engine::Scene

#endif  // ORANGE_ENGINE_SCENE_WORLD_TRANSFORM_COMPONENT_H
