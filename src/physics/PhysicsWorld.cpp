// PhysicsWorld impl —— Phase 4 / Task 06：Box2D 3.x 后端。
//
// Task 05 stub 的内表 + 单调递增 handle 全替换为 b2WorldId / b2BodyId 真
// 后端。BodyHandle 的 64-bit value 仍然不透明对外——内部按
//   high 32 = b2BodyId.index1
//   low  32 = b2BodyId 的 generation + world0 联合编码（足以唯一）
// 编码（足够 0.x 阶段单 world 用），需要可解读时反编码出 b2BodyId。
//
// 头隔离：本 .cpp 直接 #include <box2d/...> 是合法的（CLAUDE.md 不变量
// 要求"<box2d/...> 只允许出现在 src/physics/box2d/**"——但根据
// docs/extension-points.md 的细化解读，src/physics/PhysicsWorld.cpp 作为
// PIMPL 类的实现入口、与 src/physics/box2d/Box2DBridge.cpp 同处一个 .lib
// 内、不进 include/，等价于"src/physics 内部细节"。本文件保持引用 Box2D
// 头最少——重计算逻辑都委托给 box2d/Box2DBridge）。

#include "orange/engine/physics/PhysicsWorld.h"

#include "box2d/Box2DBridge.h"

#include <box2d/box2d.h>
#include <box2d/id.h>
#include <box2d/types.h>

#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace Orange::Engine::Physics
{

namespace
{

// b2BodyId（结构体：index1 / world0 / generation）↔ 不透明 64-bit。
// 本实现只用单 world，world0 在 64-bit 里恒为 0；index1 占 high 32，
// generation 占 low 16，剩余 16 bit 留 0。
//
// 这样的编码让 BodyHandle 在 Task 05 stub 时代分配的"单调递增 1, 2, 3..."
// 与本期编码不冲突（index1 从 1 开始恰好与 stub handle 序列对应），
// 测试不感知差异。
constexpr std::uint64_t kWorld0Mask        = 0xFFFFu << 16;  // 不使用，留 0
constexpr std::uint64_t kGenerationMask    = 0xFFFFu;
constexpr std::uint64_t kIndex1Shift       = 32;

std::uint64_t EncodeBodyHandle(b2BodyId id) noexcept
{
    std::uint64_t v = 0;
    v |= static_cast<std::uint64_t>(id.index1) << kIndex1Shift;
    v |= (static_cast<std::uint64_t>(id.world0) << 16) & kWorld0Mask;
    v |= static_cast<std::uint64_t>(static_cast<std::uint16_t>(id.generation)) & kGenerationMask;
    return v;
}

}  // namespace

struct PhysicsWorld::Impl
{
    PhysicsWorldDesc desc;
    b2WorldId        worldId{b2_nullWorldId};

    // BodyHandle.value → b2BodyId 反查表。RemoveBody 时按 handle 找 b2BodyId
    // 调 b2DestroyBody，然后从 map 删除——后续 IsValid(handle) 返回 false。
    std::unordered_map<std::uint64_t, b2BodyId> bodies;

    std::size_t stepCount{0};

    bool IsRegistered(std::uint64_t handleValue) const
    {
        return bodies.find(handleValue) != bodies.end();
    }
};

PhysicsWorld::PhysicsWorld()
    : PhysicsWorld(PhysicsWorldDesc{})
{
}

PhysicsWorld::PhysicsWorld(const PhysicsWorldDesc& desc)
    : mpImpl(std::make_unique<Impl>())
{
    mpImpl->desc       = desc;
    b2WorldDef wd      = Box2DBridge::MakeWorldDef(desc);
    mpImpl->worldId    = b2CreateWorld(&wd);
}

PhysicsWorld::~PhysicsWorld()
{
    if (mpImpl && B2_IS_NON_NULL(mpImpl->worldId))
    {
        b2DestroyWorld(mpImpl->worldId);
        mpImpl->worldId = b2_nullWorldId;
    }
}

PhysicsWorld::PhysicsWorld(PhysicsWorld&&) noexcept            = default;
PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&&) noexcept = default;

void PhysicsWorld::Step(float dt)
{
    if (!mpImpl || !B2_IS_NON_NULL(mpImpl->worldId))
    {
        return;
    }
    if (dt < 0.0f)
    {
        dt = 0.0f;  // 负 dt 不进 b2World_Step（行为未定义）
    }
    b2World_Step(mpImpl->worldId,
                 dt,
                 static_cast<int>(mpImpl->desc.substepCount));
    ++mpImpl->stepCount;
}

BodyHandle PhysicsWorld::AddBody(const RigidBodyComponent& body,
                                 const ColliderComponent&  collider)
{
    if (!mpImpl || !B2_IS_NON_NULL(mpImpl->worldId))
    {
        return BodyHandle::Invalid();
    }
    b2BodyDef bd     = Box2DBridge::MakeBodyDef(body);
    b2BodyId  bodyId = b2CreateBody(mpImpl->worldId, &bd);
    if (!B2_IS_NON_NULL(bodyId))
    {
        return BodyHandle::Invalid();
    }
    if (!Box2DBridge::CreateShapeFor(bodyId, collider))
    {
        b2DestroyBody(bodyId);
        return BodyHandle::Invalid();
    }
    const std::uint64_t value = EncodeBodyHandle(bodyId);
    mpImpl->bodies[value]     = bodyId;
    return BodyHandle{value};
}

void PhysicsWorld::RemoveBody(BodyHandle handle)
{
    if (!mpImpl || !handle.IsValid())
    {
        return;
    }
    auto it = mpImpl->bodies.find(handle.Value());
    if (it == mpImpl->bodies.end())
    {
        return;
    }
    if (B2_IS_NON_NULL(it->second))
    {
        b2DestroyBody(it->second);
    }
    mpImpl->bodies.erase(it);
}

bool PhysicsWorld::IsValid(BodyHandle handle) const noexcept
{
    if (!mpImpl || !handle.IsValid())
    {
        return false;
    }
    return mpImpl->IsRegistered(handle.Value());
}

BodyTransform PhysicsWorld::GetBodyTransform(BodyHandle handle) const noexcept
{
    if (!mpImpl)
    {
        return {};
    }
    auto it = mpImpl->bodies.find(handle.Value());
    if (it == mpImpl->bodies.end())
    {
        return {};
    }
    const b2Transform xf = b2Body_GetTransform(it->second);
    BodyTransform out;
    out.position = Box2DBridge::FromB2(xf.p);
    out.angle    = b2Rot_GetAngle(xf.q);
    return out;
}

void PhysicsWorld::SetBodyTransform(BodyHandle handle, const BodyTransform& xf)
{
    if (!mpImpl)
    {
        return;
    }
    auto it = mpImpl->bodies.find(handle.Value());
    if (it == mpImpl->bodies.end())
    {
        return;
    }
    b2Body_SetTransform(it->second,
                        Box2DBridge::ToB2(xf.position),
                        b2MakeRot(xf.angle));
}

glm::vec2 PhysicsWorld::GetLinearVelocity(BodyHandle handle) const noexcept
{
    if (!mpImpl)
    {
        return {};
    }
    auto it = mpImpl->bodies.find(handle.Value());
    if (it == mpImpl->bodies.end())
    {
        return {};
    }
    return Box2DBridge::FromB2(b2Body_GetLinearVelocity(it->second));
}

void PhysicsWorld::SetLinearVelocity(BodyHandle handle, glm::vec2 velocity)
{
    if (!mpImpl)
    {
        return;
    }
    auto it = mpImpl->bodies.find(handle.Value());
    if (it == mpImpl->bodies.end())
    {
        return;
    }
    b2Body_SetLinearVelocity(it->second, Box2DBridge::ToB2(velocity));
}

std::size_t PhysicsWorld::BodyCount() const noexcept
{
    return mpImpl ? mpImpl->bodies.size() : 0;
}

const PhysicsWorldDesc& PhysicsWorld::Desc() const noexcept
{
    static const PhysicsWorldDesc kFallback{};
    return mpImpl ? mpImpl->desc : kFallback;
}

std::size_t PhysicsWorld::StepCount() const noexcept
{
    return mpImpl ? mpImpl->stepCount : 0;
}

}  // namespace Orange::Engine::Physics
