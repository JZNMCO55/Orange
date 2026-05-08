// PhysicsWorld stub impl —— Phase 4 / Task 05 接口阶段。
//
// 本期不接 Box2D。AddBody / RemoveBody 只在内表里登记 / 注销，反写一个
// 单调递增的 BodyHandle；Step 仅累加 step 计数。验收能让 ctest 走通
// "World 构造 / Add-Remove 不崩 / Step 不崩"路径。
//
// Task 06 接 Box2D 后端时把本文件整体替换：内表换成 BodyHandle ↔
// b2BodyId 映射、AddBody 走 b2CreateBody + 按 ColliderDesc.shape variant
// 派发 b2CreatePolygonShape / b2CreateCircleShape / ... ；Step 转 b2World_Step。
// 公共面（PhysicsWorld.h）不动。

#include "orange/engine/physics/PhysicsWorld.h"

#include <unordered_map>

namespace Orange::Engine::Physics
{

struct PhysicsWorld::Impl
{
    PhysicsWorldDesc desc;

    // BodyHandle 的 64-bit value 直接用单调递增计数器。Task 06 切到 b2BodyId
    // 编码（index + generation）时这层语义改变，但公共面 BodyHandle 不动。
    BodyHandle::ValueType nextHandle{1};  // 0 留给 default-init，Invalid 为 max

    struct BodyEntry
    {
        RigidBodyComponent body;
        ColliderComponent  collider;
    };

    std::unordered_map<BodyHandle::ValueType, BodyEntry> bodies;

    std::size_t stepCount{0};
};

PhysicsWorld::PhysicsWorld()
    : PhysicsWorld(PhysicsWorldDesc{})
{
}

PhysicsWorld::PhysicsWorld(const PhysicsWorldDesc& desc)
    : mpImpl(std::make_unique<Impl>())
{
    mpImpl->desc = desc;
}

PhysicsWorld::~PhysicsWorld() = default;

PhysicsWorld::PhysicsWorld(PhysicsWorld&&) noexcept            = default;
PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&&) noexcept = default;

void PhysicsWorld::Step(float dt)
{
    if (!mpImpl)
    {
        return;
    }
    if (dt < 0.0f)
    {
        dt = 0.0f;  // 后端可能允许负值，本期 stub 一律 clamp 到 0
    }
    (void)dt;  // stub：不消费 dt，仅累加 step 计数
    ++mpImpl->stepCount;
    // Task 06 起：b2World_Step(m_b2World, dt, mpImpl->desc.substepCount);
}

BodyHandle PhysicsWorld::AddBody(const RigidBodyComponent& body,
                                 const ColliderComponent&  collider)
{
    if (!mpImpl)
    {
        return BodyHandle::Invalid();
    }
    const BodyHandle::ValueType id = mpImpl->nextHandle++;
    mpImpl->bodies[id] = Impl::BodyEntry{body, collider};
    return BodyHandle{id};
}

void PhysicsWorld::RemoveBody(BodyHandle handle)
{
    if (!mpImpl || !handle.IsValid())
    {
        return;
    }
    mpImpl->bodies.erase(handle.Value());
    // Task 06 起：b2DestroyBody(b2BodyId 对应)；handle generation 失效。
}

bool PhysicsWorld::IsValid(BodyHandle handle) const noexcept
{
    if (!mpImpl || !handle.IsValid())
    {
        return false;
    }
    return mpImpl->bodies.find(handle.Value()) != mpImpl->bodies.end();
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
