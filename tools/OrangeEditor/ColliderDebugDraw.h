#ifndef ORANGE_EDITOR_COLLIDER_DEBUG_DRAW_H
#define ORANGE_EDITOR_COLLIDER_DEBUG_DRAW_H

// ColliderDebugDraw —— 把 World 内所有 ColliderComponent 的几何形状以
// wireframe 形式提交给 DebugDrawScene。
//
// 设计意图：让美术 / 关卡设计师在 viewport 内**直接看见**物理碰撞盒位置
// / 大小 / 朝向，而不需要打开 Inspector 一一核对字段值。v0.x 期 Collider
// 编辑 UX 长期断裂的一环：用户加 Collider 后看不到任何视觉反馈，必须 Play
// Mode 把物体砸下去才知道盒子在哪。
//
// 实现要点：
//   * 投影到 entity 所在 XY 平面：Box2D 物理本身是 2D（XY），shape 跟 body
//     rotation 走（见 src/physics/box2d/Box2DBridge.cpp:79-92）。Debug draw
//     与物理实际行为一致 —— Z = transform.position.z（平面跳跃 2.5D 平台
//     entity 通常 z = 0，但保留通用性）
//   * Box rotation：transform.rotation 是 quat，用 quat 直接旋转 4 个本地
//     角点 (±halfX, ±halfY, 0) 得到世界坐标
//   * 不依赖 Box2D body —— 即使 Play Mode 未运行（编辑期 body 不存在），
//     仍能根据 ColliderComponent 字段画出预期碰撞盒；同时避免本 TU 拉
//     box2d 头（与 CLAUDE.md header isolation 一致：box2d/* 只允许在
//     src/physics/box2d/ 内 include）
//   * 选中 / 未选中两种配色（与 v0.9 DebugDraw selection sphere 同色系）；
//     additionalSelected 走与 primary 相同的"selected"配色
//
// 调用方约束：调用前应自行确认 `dbg.IsEnabled()`；本函数不做 IsEnabled
// 检查（让 caller 透明：disabled 时 Add* 已自然 no-op，但调本函数仍会
// 遍历 ECS view，cost 接近 0 但不为 0）。

#include <orange/engine/scene/Entity.h>

#include <vector>

namespace Orange::Engine
{
    class World;
    namespace Render
    {
        class DebugDrawScene;
    }
} // namespace Orange::Engine

namespace Orange::Editor
{

    // 遍历 world 内所有有 ColliderComponent + TransformComponent 的 entity，
    // 把对应几何 shape 以 wireframe 形式 push 到 dbg。
    //
    // selectedEntity 与 additionalSelectedEntities 内的 entity 走 "selected"
    // 高亮色；其它走 "unselected" 常规色。
    void DrawColliders(Orange::Engine::Render::DebugDrawScene&    dbg,
                       Orange::Engine::World&                     world,
                       Orange::Engine::Entity                     selectedEntity,
                       const std::vector<Orange::Engine::Entity>& additionalSelectedEntities);

} // namespace Orange::Editor

#endif // ORANGE_EDITOR_COLLIDER_DEBUG_DRAW_H
