#ifndef ORANGE_ENGINE_PHYSICS_RIGID_BODY_COMPONENT_H
#define ORANGE_ENGINE_PHYSICS_RIGID_BODY_COMPONENT_H

// ---------------------------------------------------------------------------
// RigidBodyComponent —— ECS 上的"这个 entity 是一个物理 body"标记 + 参数。
//
// trivially-copyable POD：BodyType / 浮点 / vec2 / 一个 BodyHandle（也是
// POD）。EnTT archetype 行迁移走 memcpy 路径。
//
// 字段语义对齐 Box2D 3.x b2BodyDef，方便 Task 06 接后端时直接转换：
//   - type             ↔ b2BodyType
//   - linearVelocity   ↔ b2BodyDef.linearVelocity
//   - angularVelocity  ↔ b2BodyDef.angularVelocity
//   - linearDamping    ↔ b2BodyDef.linearDamping
//   - angularDamping   ↔ b2BodyDef.angularDamping
//   - fixedRotation    ↔ b2BodyDef.fixedRotation
//   - gravityScale     ↔ b2BodyDef.gravityScale
//
// `handle` 字段由 PhysicsWorld::AddBody 反写——构造时为 Invalid()，注册
// 进 PhysicsWorld 后被填上对应 BodyHandle。Task 06 sync 路径据此把 ECS
// transform ↔ Box2D body 双向同步起来。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/physics/BodyHandle.h>

#include <glm/vec2.hpp>

#include <cstdint>

namespace Orange::Engine::Physics
{

enum class BodyType : std::uint8_t
{
    Static,      // 不动、不受力影响（地面、墙）
    Kinematic,   // 由游戏代码直接驱动，物理只读速度（移动平台、电梯）
    Dynamic,     // 完整物理模拟（角色、可推动物体）
};

struct RigidBodyComponent
{
    BodyType  type{BodyType::Dynamic};

    // body 在 PhysicsWorld 中的初始位置 / 朝向。Box2D 文档明示：在原点
    // 创建 body 再 SetTransform 移到目标位置，几乎让 body 创建成本翻倍；
    // 因此 RigidBodyComponent 在 AddBody 之前必须把 initial 字段填成
    // 期望值（典型路径：从 ECS TransformComponent 投影 xy 平面拷过来）。
    glm::vec2 initialPosition{0.0f, 0.0f};
    float     initialAngle{0.0f};  // 弧度

    glm::vec2 linearVelocity{0.0f, 0.0f};
    float     angularVelocity{0.0f};

    float     linearDamping{0.0f};
    float     angularDamping{0.0f};

    bool      fixedRotation{false};
    float     gravityScale{1.0f};

    // PhysicsWorld::AddBody 反写；构造时 Invalid()。
    BodyHandle handle{};
};

}  // namespace Orange::Engine::Physics

#endif  // ORANGE_ENGINE_PHYSICS_RIGID_BODY_COMPONENT_H
