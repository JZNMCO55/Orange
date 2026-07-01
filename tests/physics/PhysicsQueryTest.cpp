// PhysicsQueryTest —— 空间查询 API（raycast / overlap / point / contact）验收。
//
// 覆盖 PhysicsWorld 的 5 个查询方法：
//   RaycastClosest / RaycastAll / OverlapAABB / OverlapPoint / GetContacts
//
// 场景约定（世界坐标，米）：
//   - ground   ：static box，中心 (0,-5)、halfExtents (50,0.5) → 顶面 y=-4.5
//   - platform ：static box，中心 (0, 0)、halfExtents (5,0.25) → 顶面 y=0.25
//   - far       ：static 小 box，中心 (100,100)（远离查询区域）
//   - ball      ：dynamic 圆（半径 0.5），落到 ground 顶面静止
//
// headless、裸 <cassert>；main 返回 0 = pass。

#include "orange/engine/physics/PhysicsWorld.h"

#include <glm/vec2.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace Phys = Orange::Engine::Physics;

namespace
{

// ---- fixture 构件 --------------------------------------------------------

Phys::BodyHandle AddGround(Phys::PhysicsWorld& world)
{
    Phys::RigidBodyComponent rb;
    rb.type            = Phys::BodyType::Static;
    rb.initialPosition = {0.0f, -5.0f};
    Phys::ColliderComponent col;
    col.shape    = Phys::BoxDesc{{50.0f, 0.5f}, {0.0f, 0.0f}};  // 顶面 y=-4.5
    col.friction = 0.5f;
    return world.AddBody(rb, col);
}

Phys::BodyHandle AddPlatform(Phys::PhysicsWorld& world)
{
    Phys::RigidBodyComponent rb;
    rb.type            = Phys::BodyType::Static;
    rb.initialPosition = {0.0f, 0.0f};
    Phys::ColliderComponent col;
    col.shape = Phys::BoxDesc{{5.0f, 0.25f}, {0.0f, 0.0f}};  // 顶面 y=0.25
    return world.AddBody(rb, col);
}

Phys::BodyHandle AddFar(Phys::PhysicsWorld& world)
{
    Phys::RigidBodyComponent rb;
    rb.type            = Phys::BodyType::Static;
    rb.initialPosition = {100.0f, 100.0f};
    Phys::ColliderComponent col;
    col.shape = Phys::BoxDesc{{0.5f, 0.5f}, {0.0f, 0.0f}};
    return world.AddBody(rb, col);
}

Phys::BodyHandle AddBall(Phys::PhysicsWorld& world, glm::vec2 pos)
{
    Phys::RigidBodyComponent rb;
    rb.type            = Phys::BodyType::Dynamic;
    rb.initialPosition = pos;
    Phys::ColliderComponent col;
    col.shape       = Phys::CircleDesc{0.5f, {0.0f, 0.0f}};
    col.density     = 1.0f;
    col.restitution = 0.0f;
    return world.AddBody(rb, col);
}

bool Approx(float a, float b, float tol)
{
    return std::fabs(a - b) <= tol;
}

bool Contains(const std::vector<Phys::BodyHandle>& list, Phys::BodyHandle h)
{
    for (const auto& e : list)
    {
        if (e.Value() == h.Value())
        {
            return true;
        }
    }
    return false;
}

// ---- 子测试 --------------------------------------------------------------

void TestRaycastClosestHitsGround()
{
    Phys::PhysicsWorld world;
    const auto         ground = AddGround(world);
    assert(ground.IsValid());

    // 从空中 (0,5) 竖直向下投射 20 米：应在 ground 顶面 y=-4.5 命中。
    const auto hit = world.RaycastClosest({0.0f, 5.0f}, {0.0f, -1.0f}, 20.0f);
    if (!hit.hit)
    {
        std::fprintf(stderr, "[PhysicsQueryTest] RaycastClosest 未命中 ground\n");
    }
    assert(hit.hit);
    assert(hit.body.Value() == ground.Value());
    assert(Approx(hit.point.y, -4.5f, 0.05f));
    assert(Approx(hit.normal.y, 1.0f, 0.05f));      // 顶面法线朝上
    assert(Approx(hit.fraction, 0.475f, 0.02f));    // (5 - (-4.5)) / 20 = 0.475
}

void TestRaycastClosestMiss()
{
    Phys::PhysicsWorld world;
    AddGround(world);

    // 朝上投射（上方无物）→ 未命中。
    const auto hit = world.RaycastClosest({0.0f, 5.0f}, {0.0f, 1.0f}, 5.0f);
    assert(!hit.hit);
    assert(!hit.body.IsValid());
}

void TestRaycastDegenerate()
{
    Phys::PhysicsWorld world;
    AddGround(world);

    // maxDistance = 0 → 未命中、不崩。
    const auto h0 = world.RaycastClosest({0.0f, 5.0f}, {0.0f, -1.0f}, 0.0f);
    assert(!h0.hit);

    // direction 近零 → 未命中、不崩。
    const auto h1 = world.RaycastClosest({0.0f, 5.0f}, {0.0f, 0.0f}, 20.0f);
    assert(!h1.hit);

    // RaycastAll 同样退化输入 → 空。
    assert(world.RaycastAll({0.0f, 5.0f}, {0.0f, -1.0f}, 0.0f).empty());
    assert(world.RaycastAll({0.0f, 5.0f}, {0.0f, 0.0f}, 20.0f).empty());

    // NaN / Inf 输入 → 未命中、不崩（NaN 比较恒 false，会绕过朴素 <=0 / <1e-9 guard，
    // 有限性校验必须把它们拦下，否则非法向量喂进 Box2D 会在 Debug 触发 assert）。
    const float kNan = std::nanf("");
    const float kInf = std::numeric_limits<float>::infinity();
    assert(!world.RaycastClosest({0.0f, 5.0f}, {0.0f, -1.0f}, kNan).hit);
    assert(!world.RaycastClosest({0.0f, 5.0f}, {0.0f, -1.0f}, kInf).hit);
    assert(!world.RaycastClosest({0.0f, 5.0f}, {kNan, kNan}, 20.0f).hit);
    assert(!world.RaycastClosest({kInf, 5.0f}, {0.0f, -1.0f}, 20.0f).hit);
    assert(world.RaycastAll({0.0f, 5.0f}, {0.0f, -1.0f}, kNan).empty());
    assert(world.RaycastAll({0.0f, 5.0f}, {kNan, 0.0f}, 20.0f).empty());
}

void TestRaycastAllTwoLayers()
{
    Phys::PhysicsWorld world;
    const auto         ground   = AddGround(world);
    const auto         platform = AddPlatform(world);

    // 竖直向下穿过 platform（顶面 y=0.25）与 ground（顶面 y=-4.5）。
    const auto hits = world.RaycastAll({0.0f, 5.0f}, {0.0f, -1.0f}, 20.0f);
    if (hits.size() < 2)
    {
        std::fprintf(stderr, "[PhysicsQueryTest] RaycastAll 命中数 = %zu（期望 ≥ 2）\n", hits.size());
    }
    assert(hits.size() >= 2);

    // fraction 应升序（近 → 远）。
    for (std::size_t i = 1; i < hits.size(); ++i)
    {
        assert(hits[i].fraction >= hits[i - 1].fraction);
    }

    // 命中集合含 platform 与 ground 两个 handle。
    bool sawPlatform = false;
    bool sawGround   = false;
    for (const auto& h : hits)
    {
        assert(h.hit);
        if (h.body.Value() == platform.Value())
        {
            sawPlatform = true;
        }
        if (h.body.Value() == ground.Value())
        {
            sawGround = true;
        }
    }
    assert(sawPlatform);
    assert(sawGround);
}

void TestRaycastAllInitialOverlap()
{
    Phys::PhysicsWorld world;
    const auto         ground   = AddGround(world);
    const auto         platform = AddPlatform(world);  // 中心 (0,0)，box [-5..5]×[-0.25..0.25]

    // 从 platform 内部 (0,0) 竖直向下投射：platform 是 initial-overlap（起点在其内），
    // Box2D 报零法线——应被跳过，不出现在结果里；下方 ground 正常命中。
    const auto hits = world.RaycastAll({0.0f, 0.0f}, {0.0f, -1.0f}, 20.0f);

    bool sawGround = false;
    for (const auto& h : hits)
    {
        // 任何返回的命中都必须有非退化（单位）法线——绝不含 initial-overlap 零法线。
        const float n2 = h.normal.x * h.normal.x + h.normal.y * h.normal.y;
        assert(n2 > 0.25f);
        // 起点所在的 platform 不应作为命中返回（initial overlap 被过滤）。
        assert(h.body.Value() != platform.Value());
        if (h.body.Value() == ground.Value())
        {
            sawGround = true;
        }
    }
    assert(sawGround);  // 下方 ground（顶面 y=-4.5）应正常命中
}

void TestOverlapAABB()
{
    Phys::PhysicsWorld world;
    const auto         ground   = AddGround(world);
    const auto         platform = AddPlatform(world);
    const auto         farBody  = AddFar(world);

    // 覆盖 ground + platform 区域，不覆盖 (100,100) 的远处 body。
    const auto covered = world.OverlapAABB({-60.0f, -6.0f}, {60.0f, 1.0f});
    assert(Contains(covered, ground));
    assert(Contains(covered, platform));
    assert(!Contains(covered, farBody));

    // 完全在空旷处 → 空。
    const auto empty = world.OverlapAABB({500.0f, 500.0f}, {510.0f, 510.0f});
    assert(empty.empty());
}

void TestOverlapPoint()
{
    Phys::PhysicsWorld world;
    const auto         ground = AddGround(world);

    // point 在 ground box 内部（中心）→ 含 ground。
    const auto inside = world.OverlapPoint({0.0f, -5.0f});
    assert(Contains(inside, ground));

    // 空中一点 → 空。
    const auto outside = world.OverlapPoint({0.0f, 50.0f});
    assert(outside.empty());
}

void TestGetContactsGrounded()
{
    Phys::PhysicsWorld world;  // 默认 gravity (0,-9.81)
    const auto         ground = AddGround(world);
    // 贴近 ground 顶面（y=-4.5）上方：球心 -3.9，球底 -4.4，落 0.1 米即静止。
    const auto ball = AddBall(world, {0.0f, -3.9f});
    assert(ground.IsValid());
    assert(ball.IsValid());

    // 落到静止：120 步 × 1/60 秒 = 2 秒。
    constexpr int   kSteps = 120;
    constexpr float kDt    = 1.0f / 60.0f;
    for (int i = 0; i < kSteps; ++i)
    {
        world.Step(kDt);
    }

    const auto contacts = world.GetContacts(ball);
    if (contacts.empty())
    {
        std::fprintf(stderr, "[PhysicsQueryTest] GetContacts(ball) 为空（期望静止接触）\n");
    }
    assert(!contacts.empty());

    bool sawGround = false;
    for (const auto& c : contacts)
    {
        if (c.other.Value() == ground.Value())
        {
            sawGround = true;
            // ball 落在 ground 上，接触法线应近竖直。
            assert(std::fabs(std::fabs(c.normal.y) - 1.0f) <= 0.1f);
            assert(std::fabs(c.normal.x) <= 0.1f);
        }
    }
    assert(sawGround);

    // 无效 / 未注册 handle → 空、不崩。
    assert(world.GetContacts(Phys::BodyHandle::Invalid()).empty());
}

}  // namespace

int main()
{
    TestRaycastClosestHitsGround();
    TestRaycastClosestMiss();
    TestRaycastDegenerate();
    TestRaycastAllTwoLayers();
    TestRaycastAllInitialOverlap();
    TestOverlapAABB();
    TestOverlapPoint();
    TestGetContactsGrounded();
    return 0;
}
