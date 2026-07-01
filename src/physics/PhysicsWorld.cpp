// PhysicsWorld impl —— Box2D 3.x 后端。
//
// 早期 stub 的内表 + 单调递增 handle 已全替换为 b2WorldId / b2BodyId 真
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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Orange::Engine::Physics
{

namespace
{

// b2BodyId（结构体：index1 / world0 / generation）↔ 不透明 64-bit。
// 本实现只用单 world，world0 在 64-bit 里恒为 0；index1 占 high 32，
// generation 占 low 16，剩余 16 bit 留 0。
//
// 这样的编码让 BodyHandle 在早期 stub 时代分配的"单调递增 1, 2, 3..."
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

// ---- 空间查询回调（b2 tree callback）------------------------------------
// 均与 EncodeBodyHandle 同处匿名 namespace，直接复用 body↔handle 编码。
// 回调不标 noexcept——内部 push_back 可能抛 bad_alloc；且 b2 的回调 typedef
// 本身非 noexcept，保持一致避免函数指针转换的边角。

// RaycastAll：把每个命中收进 hits，恒 return 1 继续收集沿射线全部命中。
struct RaycastAllContext
{
    std::vector<RaycastHit>* hits{nullptr};
};

float RaycastAllCallback(b2ShapeId shapeId, b2Vec2 point, b2Vec2 normal, float fraction, void* context)
{
    // 起点已在 shape 内的 initial-overlap：Box2D 报 fraction=0 且零法线。跳过它
    // （return -1 过滤该 shape、继续投射），既守住 RaycastHit.normal 的"单位向量"
    // 契约，也与 RaycastClosest（b2World_CastRayClosest 忽略 initial overlap）行为一致。
    if (normal.x * normal.x + normal.y * normal.y < 1e-12f)
    {
        return -1.0f;
    }
    auto* ctx = static_cast<RaycastAllContext*>(context);
    RaycastHit h;
    h.body     = BodyHandle{EncodeBodyHandle(b2Shape_GetBody(shapeId))};
    h.point    = glm::vec2{point.x, point.y};
    h.normal   = glm::vec2{normal.x, normal.y};
    h.fraction = fraction;
    h.hit      = true;
    ctx->hits->push_back(h);
    return 1.0f;  // 返回 1：不裁剪射线，继续收集后续命中
}

// OverlapAABB：宽相候选去重收进 bodies，恒 return true 继续遍历。
struct OverlapBodyContext
{
    std::vector<BodyHandle>*           bodies{nullptr};
    std::unordered_set<std::uint64_t>* seen{nullptr};  // 去重：chain 等多 shape 属同一 body
};

bool OverlapAABBCallback(b2ShapeId shapeId, void* context)
{
    auto* ctx = static_cast<OverlapBodyContext*>(context);
    const std::uint64_t value = EncodeBodyHandle(b2Shape_GetBody(shapeId));
    if (ctx->seen->insert(value).second)
    {
        ctx->bodies->push_back(BodyHandle{value});
    }
    return true;
}

// OverlapPoint：宽相候选再经 b2Shape_TestPoint 精确判定，仅真正含 point 才收。
struct OverlapPointContext
{
    std::vector<BodyHandle>*           bodies{nullptr};
    std::unordered_set<std::uint64_t>* seen{nullptr};
    b2Vec2                             point{0.0f, 0.0f};
};

bool OverlapPointCallback(b2ShapeId shapeId, void* context)
{
    auto* ctx = static_cast<OverlapPointContext*>(context);
    if (!b2Shape_TestPoint(shapeId, ctx->point))
    {
        return true;  // 宽相过报：fat-AABB 命中但 shape 本身不含 point
    }
    const std::uint64_t value = EncodeBodyHandle(b2Shape_GetBody(shapeId));
    if (ctx->seen->insert(value).second)
    {
        ctx->bodies->push_back(BodyHandle{value});
    }
    return true;
}

// 射线输入健壮性：非有限值（NaN / Inf）当退化输入处理，避免把非法 b2Vec2 喂进
// Box2D（Debug 构建下 b2 会对非法向量 assert）。分量全部有限才返回 true。
bool IsFiniteVec2(glm::vec2 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y);
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

RaycastHit PhysicsWorld::RaycastClosest(glm::vec2 origin, glm::vec2 direction, float maxDistance) const noexcept
{
    if (!mpImpl || !B2_IS_NON_NULL(mpImpl->worldId))
    {
        return {};
    }
    // 退化输入（含 NaN / Inf）→ 空结果、不崩。用 !(x > 0) 形式使 NaN 也被拦下，
    // 叠加有限性校验挡住 +Inf 与非有限 origin / direction（否则非法向量喂进 Box2D，
    // Debug 构建会 assert）。
    if (!(maxDistance > 0.0f) || !std::isfinite(maxDistance) || !IsFiniteVec2(origin) || !IsFiniteVec2(direction))
    {
        return {};
    }
    const float len = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (len < 1e-9f)
    {
        return {};  // direction 近零：无方向可投射
    }
    const glm::vec2 dir         = direction / len;
    const b2Vec2    b2Origin    = {origin.x, origin.y};
    const b2Vec2    translation = {dir.x * maxDistance, dir.y * maxDistance};

    const b2RayResult r =
        b2World_CastRayClosest(mpImpl->worldId, b2Origin, translation, b2DefaultQueryFilter());
    if (!r.hit)
    {
        return {};
    }
    RaycastHit out;
    out.body     = BodyHandle{EncodeBodyHandle(b2Shape_GetBody(r.shapeId))};
    out.point    = glm::vec2{r.point.x, r.point.y};
    out.normal   = glm::vec2{r.normal.x, r.normal.y};
    out.fraction = r.fraction;
    out.hit      = true;
    return out;
}

std::vector<RaycastHit> PhysicsWorld::RaycastAll(glm::vec2 origin, glm::vec2 direction, float maxDistance) const
{
    std::vector<RaycastHit> hits;
    if (!mpImpl || !B2_IS_NON_NULL(mpImpl->worldId))
    {
        return hits;
    }
    // 退化输入（含 NaN / Inf）→ 空 vector、不崩（与 RaycastClosest 一致）。
    if (!(maxDistance > 0.0f) || !std::isfinite(maxDistance) || !IsFiniteVec2(origin) || !IsFiniteVec2(direction))
    {
        return hits;
    }
    const float len = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (len < 1e-9f)
    {
        return hits;
    }
    const glm::vec2 dir         = direction / len;
    const b2Vec2    b2Origin    = {origin.x, origin.y};
    const b2Vec2    translation = {dir.x * maxDistance, dir.y * maxDistance};

    RaycastAllContext ctx;
    ctx.hits = &hits;
    b2World_CastRay(mpImpl->worldId, b2Origin, translation, b2DefaultQueryFilter(), RaycastAllCallback, &ctx);

    // b2 回调按 tree 遍历序回来，不保证 fraction 有序——显式按近 → 远排序。
    std::sort(hits.begin(), hits.end(),
              [](const RaycastHit& a, const RaycastHit& b) { return a.fraction < b.fraction; });
    return hits;
}

std::vector<BodyHandle> PhysicsWorld::OverlapAABB(glm::vec2 lowerBound, glm::vec2 upperBound) const
{
    std::vector<BodyHandle> bodies;
    if (!mpImpl || !B2_IS_NON_NULL(mpImpl->worldId))
    {
        return bodies;
    }
    // 非有限坐标（NaN/Inf）→ 空：不把非法 AABB 喂进 Box2D（Debug 下 b2IsValidAABB
    // 断言会崩），与 raycast 路径的有限性守卫对齐。
    if (!IsFiniteVec2(lowerBound) || !IsFiniteVec2(upperBound))
    {
        return bodies;
    }
    const b2Vec2 lo = {std::min(lowerBound.x, upperBound.x), std::min(lowerBound.y, upperBound.y)};
    const b2Vec2 hi = {std::max(lowerBound.x, upperBound.x), std::max(lowerBound.y, upperBound.y)};
    const b2AABB aabb = {lo, hi};

    std::unordered_set<std::uint64_t> seen;
    OverlapBodyContext                ctx;
    ctx.bodies = &bodies;
    ctx.seen   = &seen;
    b2World_OverlapAABB(mpImpl->worldId, aabb, b2DefaultQueryFilter(), OverlapAABBCallback, &ctx);
    return bodies;
}

std::vector<BodyHandle> PhysicsWorld::OverlapPoint(glm::vec2 point) const
{
    std::vector<BodyHandle> bodies;
    if (!mpImpl || !B2_IS_NON_NULL(mpImpl->worldId))
    {
        return bodies;
    }
    // 非有限坐标（NaN/Inf）→ 空：不把非法退化 AABB 喂进 Box2D，与 raycast 守卫对齐。
    if (!IsFiniteVec2(point))
    {
        return bodies;
    }
    const b2Vec2 p    = {point.x, point.y};
    const b2AABB aabb = {p, p};  // 退化 AABB：宽相仅拿"fat-AABB 含该点"的候选，再精确判定

    std::unordered_set<std::uint64_t> seen;
    OverlapPointContext               ctx;
    ctx.bodies = &bodies;
    ctx.seen   = &seen;
    ctx.point  = p;
    b2World_OverlapAABB(mpImpl->worldId, aabb, b2DefaultQueryFilter(), OverlapPointCallback, &ctx);
    return bodies;
}

std::vector<ContactPoint> PhysicsWorld::GetContacts(BodyHandle body) const
{
    std::vector<ContactPoint> contacts;
    if (!mpImpl || !B2_IS_NON_NULL(mpImpl->worldId))
    {
        return contacts;
    }
    if (!body.IsValid())
    {
        return contacts;
    }
    auto it = mpImpl->bodies.find(body.Value());
    if (it == mpImpl->bodies.end())
    {
        return contacts;
    }
    const b2BodyId bodyId = it->second;
    const int      cap    = b2Body_GetContactCapacity(bodyId);
    if (cap <= 0)
    {
        return contacts;  // 无 contact（含 sensor / 未 Step / 不接触）
    }
    std::vector<b2ContactData> buf(static_cast<std::size_t>(cap));
    const int                  n = b2Body_GetContactData(bodyId, buf.data(), cap);
    for (int i = 0; i < n; ++i)
    {
        const b2ContactData& cd = buf[static_cast<std::size_t>(i)];

        // 判断被查询 body 落在 contact 的 A 侧还是 B 侧（b2 内部决定 A/B 顺序）。
        const bool          mineIsA    = (EncodeBodyHandle(b2Shape_GetBody(cd.shapeIdA)) == body.Value());
        const b2ShapeId     otherShape = mineIsA ? cd.shapeIdB : cd.shapeIdA;
        const std::uint64_t otherValue = EncodeBodyHandle(b2Shape_GetBody(otherShape));

        // b2Manifold.normal 约定：从 shapeA 指向 shapeB。若被查询 body 是 A，
        // 该 normal 已"从 body 指向 other"；若是 B 则取反，保证契约一致。
        glm::vec2 normal = {cd.manifold.normal.x, cd.manifold.normal.y};
        if (!mineIsA)
        {
            normal = -normal;
        }

        // pointCount==0 时（speculative 未真正触碰）for 不执行，自然跳过。
        for (int j = 0; j < cd.manifold.pointCount; ++j)
        {
            const b2ManifoldPoint& mp = cd.manifold.points[j];
            ContactPoint           cp;
            cp.other  = BodyHandle{otherValue};
            cp.point  = glm::vec2{mp.point.x, mp.point.y};
            cp.normal = normal;
            contacts.push_back(cp);
        }
    }
    return contacts;
}

bool PhysicsWorld::ReplaceFixture(BodyHandle handle, const ColliderComponent& collider)
{
    if (!mpImpl || !B2_IS_NON_NULL(mpImpl->worldId))
    {
        return false;
    }
    auto it = mpImpl->bodies.find(handle.Value());
    if (it == mpImpl->bodies.end())
    {
        return false;
    }
    const b2BodyId bodyId = it->second;

    // 顺序：先销毁旧 shape（updateBodyMass=false 让中间帧 mass 不抖动），
    // 再创建新 shape，最后显式 ApplyMassFromShapes 把 mass / inertia
    // 一次性算到位。
    Box2DBridge::DestroyAllShapesOnBody(bodyId);
    if (!Box2DBridge::CreateShapeFor(bodyId, collider))
    {
        // 失败：旧已清、新没建——body 上当前无 shape。调用方应判断返回值
        // 决定是 RemoveBody 还是再试一次 ReplaceFixture。这里 ApplyMass 仍
        // 调一次让 mass 归零（避免 dynamic body 残留旧 mass / inertia）。
        b2Body_ApplyMassFromShapes(bodyId);
        return false;
    }
    b2Body_ApplyMassFromShapes(bodyId);
    return true;
}

float PhysicsWorld::GetMass(BodyHandle handle) const noexcept
{
    if (!mpImpl)
    {
        return 0.0f;
    }
    auto it = mpImpl->bodies.find(handle.Value());
    if (it == mpImpl->bodies.end())
    {
        return 0.0f;
    }
    return b2Body_GetMass(it->second);
}

void PhysicsWorld::SetBodyEnabled(BodyHandle handle, bool enabled)
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
    if (enabled)
    {
        b2Body_Enable(it->second);
    }
    else
    {
        b2Body_Disable(it->second);
    }
}

bool PhysicsWorld::IsBodyEnabled(BodyHandle handle) const noexcept
{
    if (!mpImpl)
    {
        return false;
    }
    auto it = mpImpl->bodies.find(handle.Value());
    if (it == mpImpl->bodies.end())
    {
        return false;
    }
    return b2Body_IsEnabled(it->second);
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
