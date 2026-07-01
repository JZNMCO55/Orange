#ifndef ORANGE_ENGINE_PHYSICS_COLLIDER_COMPONENT_H
#define ORANGE_ENGINE_PHYSICS_COLLIDER_COMPONENT_H

// ---------------------------------------------------------------------------
// ColliderComponent —— ECS 上的几何碰撞描述。
//
// shape 走 std::variant<CircleDesc, BoxDesc, PolygonDesc, EdgeChainDesc>。
// 四个 desc 都是 trivially-copyable POD，所以 variant 整体也 trivially-
// copyable（C++17 起：std::variant 在所有 alternative 都 trivially-copyable
// 时 trivially-copyable）。
//
// 字段语义对齐 Box2D 3.x b2ShapeDef：
//   - density      ↔ b2ShapeDef.density
//   - friction     ↔ b2ShapeDef.friction
//   - restitution  ↔ b2ShapeDef.restitution
//   - isSensor     ↔ b2ShapeDef.isSensor
//   - categoryBits ↔ b2ShapeDef.filter.categoryBits（本 collider 所属碰撞类）
//   - maskBits     ↔ b2ShapeDef.filter.maskBits（本 collider 愿与哪些类碰撞）
//
// 一个 entity 一个 ColliderComponent = 一个 collider shape。Box2D 支持单
// body 多 shape，但 0.x 阶段 ECS 维度先按"1 entity = 1 shape"处理；多
// shape 角色（一个 body 上挂头 / 身 / 腿三个 collider）等真撞上需求后
// 再扩 ColliderComponent 为 std::array<...> 或拆成"主 entity + 子 entity"
// 模式。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/physics/ColliderDesc.h>

#include <cstdint>
#include <variant>

namespace Orange::Engine::Physics
{

struct ColliderComponent
{
    std::variant<CircleDesc, BoxDesc, PolygonDesc, EdgeChainDesc> shape{CircleDesc{}};

    float density{1.0f};
    float friction{0.3f};
    float restitution{0.0f};
    bool  isSensor{false};

    // collision filtering（碰撞过滤位）。两 collider 碰撞当且仅当
    //   (A.categoryBits & B.maskBits) != 0 && (B.categoryBits & A.maskBits) != 0
    // 用 std::uint32_t（非 uint64）表达：uint32 整个值域都能精确落进 JSON 整数，
    // 避开 uint64 高位在序列化时丢精度的风险。默认 category=1 / mask=all →
    // 与所有 body 碰撞 = 未设置过滤时的零变化行为。扩宽到 Box2D 的 uint64
    // b2Filter 由 Box2DBridge 隐式完成。
    std::uint32_t categoryBits{0x0001u};       // 本 collider 所属碰撞类（位）
    std::uint32_t maskBits{0xFFFFFFFFu};        // 本 collider 愿与哪些类碰撞（位）
};

}  // namespace Orange::Engine::Physics

#endif  // ORANGE_ENGINE_PHYSICS_COLLIDER_COMPONENT_H
