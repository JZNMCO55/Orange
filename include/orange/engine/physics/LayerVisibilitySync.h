#ifndef ORANGE_ENGINE_PHYSICS_LAYER_VISIBILITY_SYNC_H
#define ORANGE_ENGINE_PHYSICS_LAYER_VISIBILITY_SYNC_H

// ---------------------------------------------------------------------------
// LayerVisibilitySync —— 把 WorldPartition 的 layer 可见性映射到
// PhysicsWorld 的 body enable / disable 状态。
//
// 设计意图：
//   * Render 端 layer 过滤走 Pipeline.SetWorldPartition + RenderScene.Collect
//     的"跳过 drawable"路径，是声明式的，不需要 mutate ECS。
//   * Physics 端不一样：body 是否参与 b2World_Step 由 b2Body 自己的
//     enabled 标志决定；不"显式同步"的话，hidden layer 上的 dynamic body
//     仍然会自由落体、与 visible layer 的 body 产生碰撞——是用户期望
//     避免的"hide 一个 layer 后整个 layer 不要参与物理"语义。
//   * 用 free function 而不是把这条逻辑塞进 PhysicsWorld：保持
//     PhysicsWorld 不依赖 Scene 模块；调用方在每帧 Step 前主动调一次
//     即可，控制权在外面。
//
// 调用时机：每帧 `PhysicsWorld::Step` 之前。可见性没变化时调本函数也几
// 乎零成本（O(N) 遍历 + 大多数 SetBodyEnabled 走"状态没变"快路径）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

namespace Orange::Engine
{
class World;
}

namespace Orange::Engine::Scene
{
class WorldPartition;
}

namespace Orange::Engine::Physics
{

class PhysicsWorld;

// 遍历 `world` 内所有挂 RigidBodyComponent 的 entity，根据 entity 所在
// layer 的 visible 状态调 `physics.SetBodyEnabled(handle, visible)`。
// RigidBodyComponent.handle 必须 valid（未注册 / 注册失败的 entity 默
// 认 enabled=true，不受本函数影响）。
ORANGE_ENGINE_API void ApplyLayerVisibility(
    const ::Orange::Engine::World&                  world,
    const ::Orange::Engine::Scene::WorldPartition&  partition,
    PhysicsWorld&                                   physics);

}  // namespace Orange::Engine::Physics

#endif  // ORANGE_ENGINE_PHYSICS_LAYER_VISIBILITY_SYNC_H
