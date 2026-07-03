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
        col.shape    = Phys::BoxDesc{{50.0f, 0.5f}, {0.0f, 0.0f}}; // 顶面 y=-4.5
        col.friction = 0.5f;
        return world.AddBody(rb, col);
    }

    Phys::BodyHandle AddPlatform(Phys::PhysicsWorld& world)
    {
        Phys::RigidBodyComponent rb;
        rb.type            = Phys::BodyType::Static;
        rb.initialPosition = {0.0f, 0.0f};
        Phys::ColliderComponent col;
        col.shape = Phys::BoxDesc{{5.0f, 0.25f}, {0.0f, 0.0f}}; // 顶面 y=0.25
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

    // static sensor box：不参与碰撞响应，只产生 sensor begin/end 事件。
    Phys::BodyHandle AddSensor(Phys::PhysicsWorld& world, glm::vec2 center, glm::vec2 halfExtents)
    {
        Phys::RigidBodyComponent rb;
        rb.type            = Phys::BodyType::Static;
        rb.initialPosition = center;
        Phys::ColliderComponent col;
        col.shape    = Phys::BoxDesc{halfExtents, {0.0f, 0.0f}};
        col.isSensor = true;
        return world.AddBody(rb, col);
    }

    // 建两个不接触的 static box（用于"无事件"验证）。
    Phys::BodyHandle AddStaticBox(Phys::PhysicsWorld& world, glm::vec2 center, glm::vec2 halfExtents)
    {
        Phys::RigidBodyComponent rb;
        rb.type            = Phys::BodyType::Static;
        rb.initialPosition = center;
        Phys::ColliderComponent col;
        col.shape = Phys::BoxDesc{halfExtents, {0.0f, 0.0f}};
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
        assert(Approx(hit.normal.y, 1.0f, 0.05f));   // 顶面法线朝上
        assert(Approx(hit.fraction, 0.475f, 0.02f)); // (5 - (-4.5)) / 20 = 0.475
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
        const auto         platform = AddPlatform(world); // 中心 (0,0)，box [-5..5]×[-0.25..0.25]

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
        assert(sawGround); // 下方 ground（顶面 y=-4.5）应正常命中
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

        // 非有限边界（NaN）→ 空、不崩（Debug 下亦不触发 Box2D b2IsValidAABB assert）。
        assert(world.OverlapAABB({std::nanf(""), -6.0f}, {60.0f, 1.0f}).empty());
    }

    void TestOverlapPoint()
    {
        Phys::PhysicsWorld world;
        const auto         ground = AddGround(world);

        // 额外放一个 static 圆（半径 1，中心 (20,20)）用于 narrow-phase 判别：
        // 圆的 AABB 是 [19,21]²，其角点落在 AABB 内（宽相命中）但在圆外（TestPoint 排除），
        // 用来锁住 OverlapPoint 的精确判定——若退化成纯宽相（去掉 b2Shape_TestPoint），角点会被误纳。
        Phys::BodyHandle circle;
        {
            Phys::RigidBodyComponent rb;
            rb.type            = Phys::BodyType::Static;
            rb.initialPosition = {20.0f, 20.0f};
            Phys::ColliderComponent col;
            col.shape = Phys::CircleDesc{1.0f, {0.0f, 0.0f}};
            circle    = world.AddBody(rb, col);
        }
        assert(circle.IsValid());

        // point 在 ground box 内部（中心）→ 含 ground。
        const auto inside = world.OverlapPoint({0.0f, -5.0f});
        assert(Contains(inside, ground));

        // 圆心 → 含 circle（narrow-phase 命中）。
        assert(Contains(world.OverlapPoint({20.0f, 20.0f}), circle));

        // 圆 AABB 角点 (20.9,20.9)：距圆心 ≈1.27 > 半径 1，在 AABB 内但在圆外 →
        // 精确判定应排除。这是相对 OverlapAABB 的唯一增量（narrow-phase）的判别性覆盖。
        assert(!Contains(world.OverlapPoint({20.9f, 20.9f}), circle));

        // 空中一点 → 空。
        const auto outside = world.OverlapPoint({0.0f, 50.0f});
        assert(outside.empty());

        // 非有限坐标（NaN）→ 空、不崩。
        assert(world.OverlapPoint({std::nanf(""), 0.0f}).empty());
    }

    void TestGetContactsGrounded()
    {
        Phys::PhysicsWorld world; // 默认 gravity (0,-9.81)
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
                // 契约：normal 从被查询 body(ball) 指向 other(ground)。ball 在 ground 上方，
                // 故应近竖直**向下**（y≈-1）——用带符号断言锁方向，防"法线反向"的实现假绿通过
                //（|normal.y| 断言对 +1/-1 都放行，测不出方向 bug）。
                assert(c.normal.y <= -0.9f);
                assert(std::fabs(c.normal.x) <= 0.1f);
            }
        }
        assert(sawGround);

        // 无效 / 未注册 handle → 空、不崩。
        assert(world.GetContacts(Phys::BodyHandle::Invalid()).empty());
    }

    // ---- shape-cast 子测试 ---------------------------------------------------

    void TestShapeCastCircleHitsGround()
    {
        Phys::PhysicsWorld world;
        const auto         ground = AddGround(world);

        // 半径 0.5 的圆从 (0,5) 竖直向下扫掠：圆底触及 ground 顶面 y=-4.5 时圆心在 y=-4.0，
        // 圆心移动 9 米 → fraction = 9/20 = 0.45；接触点在 ground 顶面。
        const auto hit = world.ShapeCastCircle({0.0f, 5.0f}, 0.5f, {0.0f, -1.0f}, 20.0f);
        assert(hit.hit);
        assert(hit.body.Value() == ground.Value());
        assert(Approx(hit.normal.y, 1.0f, 0.05f)); // ground 顶面法线朝上
        assert(Approx(hit.fraction, 0.45f, 0.02f));
        assert(Approx(hit.point.y, -4.5f, 0.05f));
    }

    void TestShapeCastCircleMiss()
    {
        Phys::PhysicsWorld world;
        AddGround(world);
        // 向上扫掠（上方无物）→ 未命中。
        const auto hit = world.ShapeCastCircle({0.0f, 5.0f}, 0.5f, {0.0f, 1.0f}, 5.0f);
        assert(!hit.hit);
        assert(!hit.body.IsValid());
    }

    void TestShapeCastCatchesWhatRayMisses()
    {
        Phys::PhysicsWorld world;
        const auto         ground = AddGround(world);

        // 偏置平台：box 中心 (1,0)、halfExtents (0.5,0.25) → 跨 x[0.5,1.5]，不含 x=0。
        Phys::BodyHandle platform;
        {
            Phys::RigidBodyComponent rb;
            rb.type            = Phys::BodyType::Static;
            rb.initialPosition = {1.0f, 0.0f};
            Phys::ColliderComponent col;
            col.shape = Phys::BoxDesc{{0.5f, 0.25f}, {0.0f, 0.0f}};
            platform  = world.AddBody(rb, col);
        }
        assert(platform.IsValid());

        // 细射线在 x=0 竖直向下：miss 掉偏置平台（x=0<0.5），只够到下方 ground。
        const auto ray = world.RaycastClosest({0.0f, 5.0f}, {0.0f, -1.0f}, 20.0f);
        assert(ray.hit);
        assert(ray.body.Value() == ground.Value());

        // 半径 0.6 的圆在同一 x=0 竖直向下扫掠：圆右缘 x=0.6>0.5 够到平台左上角 →
        // 命中平台（比 ground 更近）。这正是 shape-cast 相对 raycast 的价值所在。
        const auto swept = world.ShapeCastCircle({0.0f, 5.0f}, 0.6f, {0.0f, -1.0f}, 20.0f);
        assert(swept.hit);
        assert(swept.body.Value() == platform.Value());
        assert(swept.fraction < ray.fraction); // 平台比 ground 更早命中
    }

    void TestShapeCastCapsuleHitsGround()
    {
        Phys::PhysicsWorld world;
        const auto         ground = AddGround(world);

        // 竖直胶囊：端点 (0,1)/(0,-1) + 半径 0.5 → 最低点 y=-1.5。向下扫掠：最低点触
        // ground 顶面 y=-4.5 时移动 3 米 → fraction = 3/20 = 0.15。
        const auto hit = world.ShapeCastCapsule({0.0f, 1.0f}, {0.0f, -1.0f}, 0.5f, {0.0f, -1.0f}, 20.0f);
        assert(hit.hit);
        assert(hit.body.Value() == ground.Value());
        assert(Approx(hit.normal.y, 1.0f, 0.05f));
        assert(Approx(hit.fraction, 0.15f, 0.02f));
    }

    void TestShapeCastDegenerate()
    {
        Phys::PhysicsWorld world;
        AddGround(world);

        // maxDistance=0 / 零方向 / 负半径 / NaN → 未命中、不崩。
        assert(!world.ShapeCastCircle({0.0f, 5.0f}, 0.5f, {0.0f, -1.0f}, 0.0f).hit);
        assert(!world.ShapeCastCircle({0.0f, 5.0f}, 0.5f, {0.0f, 0.0f}, 20.0f).hit);
        assert(!world.ShapeCastCircle({0.0f, 5.0f}, -1.0f, {0.0f, -1.0f}, 20.0f).hit);
        assert(!world.ShapeCastCircle({std::nanf(""), 5.0f}, 0.5f, {0.0f, -1.0f}, 20.0f).hit);
        assert(!world.ShapeCastCapsule({0.0f, 1.0f}, {0.0f, -1.0f}, 0.5f, {0.0f, 0.0f}, 20.0f).hit);
        assert(!world.ShapeCastCapsule({std::nanf(""), 1.0f}, {0.0f, -1.0f}, 0.5f, {0.0f, -1.0f}, 20.0f).hit);
    }

    // ---- sensor / contact 事件子测试 -----------------------------------------

    void TestSensorBeginEndEvents()
    {
        Phys::PhysicsWorld world; // 默认 gravity (0,-9.81)
        // static sensor box：中心 (0,0) halfExtents (1,1) → 覆盖 y∈[-1,1]。
        const auto sensor = AddSensor(world, {0.0f, 0.0f}, {1.0f, 1.0f});
        // dynamic 小圆从上方 (0,5) 自由下落穿过 sensor（sensor 不挡，圆一路落）。
        const auto ball = AddBall(world, {0.0f, 5.0f});
        assert(sensor.IsValid());
        assert(ball.IsValid());

        bool            sawBegin = false;
        bool            sawEnd   = false;
        constexpr float kDt      = 1.0f / 60.0f;
        for (int i = 0; i < 300; ++i)
        {
            world.Step(kDt);
            for (const auto& e : world.GetSensorBeginEvents())
            {
                if (e.sensor.Value() == sensor.Value() && e.visitor.Value() == ball.Value())
                {
                    sawBegin = true;
                }
            }
            for (const auto& e : world.GetSensorEndEvents())
            {
                if (e.sensor.Value() == sensor.Value() && e.visitor.Value() == ball.Value())
                {
                    sawEnd = true;
                }
            }
        }
        if (!sawBegin)
        {
            std::fprintf(stderr, "[PhysicsQueryTest] sensor begin 事件未触发（检查 enableSensorEvents）\n");
        }
        if (!sawEnd)
        {
            std::fprintf(stderr, "[PhysicsQueryTest] sensor end 事件未触发\n");
        }
        assert(sawBegin); // 圆进入 sensor → begin
        assert(sawEnd);   // 圆离开 sensor → end
    }

    void TestContactBeginEvent()
    {
        Phys::PhysicsWorld world;
        const auto         ground = AddGround(world);
        // 贴近 ground 顶面 (y=-4.5) 上方的球：球心 -3.9、球底 -4.4，落 0.1 米即接触。
        const auto ball = AddBall(world, {0.0f, -3.9f});
        assert(ground.IsValid());
        assert(ball.IsValid());

        bool            sawBegin = false;
        constexpr float kDt      = 1.0f / 60.0f;
        for (int i = 0; i < 120 && !sawBegin; ++i)
        {
            world.Step(kDt);
            for (const auto& e : world.GetContactBeginEvents())
            {
                const bool isBallGround =
                    (e.bodyA.Value() == ball.Value() && e.bodyB.Value() == ground.Value()) ||
                    (e.bodyA.Value() == ground.Value() && e.bodyB.Value() == ball.Value());
                if (isBallGround)
                {
                    sawBegin = true;
                    // 初始接触点在 ground 顶面附近 y≈-4.5；法线近竖直（|normal.y|≈1）。
                    assert(Approx(e.point.y, -4.5f, 0.1f));
                    assert(std::fabs(std::fabs(e.normal.y) - 1.0f) <= 0.1f);
                }
            }
        }
        if (!sawBegin)
        {
            std::fprintf(stderr, "[PhysicsQueryTest] contact begin 事件未触发（检查 enableContactEvents）\n");
        }
        assert(sawBegin);
    }

    void TestContactEndEvent()
    {
        Phys::PhysicsWorld world;
        const auto         ground = AddGround(world);
        const auto         ball   = AddBall(world, {0.0f, -3.9f});
        assert(ground.IsValid());
        assert(ball.IsValid());

        // 先让球落地接触（等到 contact begin）。
        bool            sawBegin = false;
        constexpr float kDt      = 1.0f / 60.0f;
        for (int i = 0; i < 120 && !sawBegin; ++i)
        {
            world.Step(kDt);
            for (const auto& e : world.GetContactBeginEvents())
            {
                const bool isBallGround =
                    (e.bodyA.Value() == ball.Value() && e.bodyB.Value() == ground.Value()) ||
                    (e.bodyA.Value() == ground.Value() && e.bodyB.Value() == ball.Value());
                if (isBallGround)
                {
                    sawBegin = true;
                }
            }
        }
        assert(sawBegin);

        // 把球瞬移到高空 + 清零速度 → 与 ground 分离，应产生 contact end 事件。
        world.SetBodyTransform(ball, {{0.0f, 20.0f}, 0.0f});
        world.SetLinearVelocity(ball, {0.0f, 0.0f});

        bool sawEnd = false;
        for (int i = 0; i < 10 && !sawEnd; ++i)
        {
            world.Step(kDt);
            for (const auto& e : world.GetContactEndEvents())
            {
                const bool isBallGround =
                    (e.bodyA.Value() == ball.Value() && e.bodyB.Value() == ground.Value()) ||
                    (e.bodyA.Value() == ground.Value() && e.bodyB.Value() == ball.Value());
                if (isBallGround)
                {
                    sawEnd = true;
                }
            }
        }
        if (!sawEnd)
        {
            std::fprintf(stderr, "[PhysicsQueryTest] contact end 事件未触发（瞬移分离后）\n");
        }
        assert(sawEnd); // 分离 → end-touch
    }

    void TestEventsEmpty()
    {
        Phys::PhysicsWorld world;
        // 两个远隔、不接触的 static box → 无 contact、无 sensor 交叠。
        AddStaticBox(world, {0.0f, 0.0f}, {0.5f, 0.5f});
        AddStaticBox(world, {100.0f, 100.0f}, {0.5f, 0.5f});
        world.Step(1.0f / 60.0f);

        assert(world.GetSensorBeginEvents().empty());
        assert(world.GetSensorEndEvents().empty());
        assert(world.GetContactBeginEvents().empty());
        assert(world.GetContactEndEvents().empty());
    }

} // namespace

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
    TestShapeCastCircleHitsGround();
    TestShapeCastCircleMiss();
    TestShapeCastCatchesWhatRayMisses();
    TestShapeCastCapsuleHitsGround();
    TestShapeCastDegenerate();
    TestSensorBeginEndEvents();
    TestContactBeginEvent();
    TestContactEndEvent();
    TestEventsEmpty();
    return 0;
}
