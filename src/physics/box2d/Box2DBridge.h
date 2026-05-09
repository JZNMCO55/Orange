#ifndef ORANGE_ENGINE_PHYSICS_BOX2D_BOX2D_BRIDGE_H
#define ORANGE_ENGINE_PHYSICS_BOX2D_BOX2D_BRIDGE_H

// Box2DBridge —— OrangeEngine Physics 公共类型 ↔ Box2D 3.x 类型的转换层。
//
// 仅 src/physics/box2d/ 内部消费，不进 include/。**这是 OrangeEngine 里
// 唯一允许 #include <box2d/...> 的目录**（CLAUDE.md "Header isolation" 不变量）。
//
// 公共面（include/orange/engine/physics/...）持有的是中性物理概念
// (CircleDesc / BoxDesc / PolygonDesc / EdgeChainDesc / RigidBodyComponent /
// ColliderComponent)；本头把它们映射到 b2WorldDef / b2BodyDef / b2ShapeDef
// / b2Polygon / b2Circle / b2ChainDef 等 b2 类型。

#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/PhysicsWorld.h>
#include <orange/engine/physics/RigidBodyComponent.h>

#include <box2d/box2d.h>
#include <box2d/types.h>

#include <glm/vec2.hpp>

namespace Orange::Engine::Physics::Box2DBridge
{

// 类型映射 ----------------------------------------------------------------

inline b2BodyType ToB2BodyType(BodyType t) noexcept
{
    switch (t)
    {
    case BodyType::Static:    return b2_staticBody;
    case BodyType::Kinematic: return b2_kinematicBody;
    case BodyType::Dynamic:   return b2_dynamicBody;
    }
    return b2_dynamicBody;
}

inline b2Vec2 ToB2(const glm::vec2& v) noexcept
{
    return b2Vec2{v.x, v.y};
}

inline glm::vec2 FromB2(const b2Vec2& v) noexcept
{
    return glm::vec2{v.x, v.y};
}

// b2WorldDef 工厂：从 PhysicsWorldDesc 派生。
b2WorldDef MakeWorldDef(const PhysicsWorldDesc& desc) noexcept;

// b2BodyDef 工厂：从 RigidBodyComponent 派生（含 initial position/angle）。
b2BodyDef  MakeBodyDef(const RigidBodyComponent& rb) noexcept;

// b2ShapeDef 工厂：从 ColliderComponent 公共字段派生（density / material 里
// 的 friction / restitution / isSensor）。具体几何（Circle / Polygon /
// Chain）由 CreateShapeFor 根据 variant 派发。
b2ShapeDef MakeShapeDef(const ColliderComponent& col) noexcept;

// 把 ColliderComponent.shape variant 派发成对应的 b2Create*Shape 调用。
// 返回 true 时几何已成功挂在 body 上。失败原因：polygon count 不在
// [3, 8]、chain count < 4 等——本期不强制校验，b2 内部 assert 会兜底。
bool CreateShapeFor(b2BodyId bodyId, const ColliderComponent& col);

// 销毁 body 上挂的全部 shape（含 chain segment）。先清掉所有 chain（chain
// 拥有自己的 segment shape），再清残留的独立 shape；销毁过程统一传
// updateBodyMass=false 避免中间帧 mass 抖动，调用方在新 shape 落地后再
// 显式 b2Body_ApplyMassFromShapes。
//
// 用于 PhysicsWorld::ReplaceFixture 的"原子替换"路径。
void DestroyAllShapesOnBody(b2BodyId bodyId);

}  // namespace Orange::Engine::Physics::Box2DBridge

#endif  // ORANGE_ENGINE_PHYSICS_BOX2D_BOX2D_BRIDGE_H
