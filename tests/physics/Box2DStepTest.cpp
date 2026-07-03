// Box2DStepTest —— Phase 4 / Task 06 验收：Box2D 后端真模拟自由落体。
//
// 场景：单 dynamic ball（半径 0.5）在 origin 释放，重力 (0, -9.81)。
// 1 秒后理论 y = -0.5 * 9.81 * 1² = -4.905。
//
// 误差容忍 1%（±0.05），覆盖 substep / numerical drift / b2 内部小修正。
//
// 60 fps × 1 秒 = 60 步；与游戏典型主循环节奏一致。

#include "orange/engine/physics/PhysicsWorld.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace Phys = Orange::Engine::Physics;

namespace
{

    void TestFreeFall()
    {
        Phys::PhysicsWorldDesc d;
        d.gravity      = {0.0f, -9.81f};
        d.substepCount = 4;
        Phys::PhysicsWorld world(d);

        Phys::RigidBodyComponent rb;
        rb.type            = Phys::BodyType::Dynamic;
        rb.initialPosition = {0.0f, 0.0f};
        rb.gravityScale    = 1.0f;

        Phys::ColliderComponent col;
        col.shape   = Phys::CircleDesc{0.5f, {0.0f, 0.0f}};
        col.density = 1.0f;

        auto h = world.AddBody(rb, col);
        assert(h.IsValid());
        assert(world.BodyCount() == 1);

        // 60 步 × 1/60 秒 = 1 秒
        constexpr int   kSteps = 60;
        constexpr float kDt    = 1.0f / 60.0f;
        for (int i = 0; i < kSteps; ++i)
        {
            world.Step(kDt);
        }
        assert(world.StepCount() == kSteps);

        const auto  xf        = world.GetBodyTransform(h);
        const float expectedY = -0.5f * 9.81f * 1.0f * 1.0f; // -4.905
        const float dy        = xf.position.y - expectedY;
        const float tolerance = 0.05f; // ~1% of 4.905

        if (std::fabs(dy) > tolerance)
        {
            std::fprintf(stderr,
                         "[Box2DStepTest] free-fall: y=%.4f expected=%.4f diff=%.4f tol=%.4f\n",
                         xf.position.y, expectedY, dy, tolerance);
        }
        assert(std::fabs(dy) <= tolerance);

        // 速度大致 = -g * t = -9.81 m/s（同样 1% 容忍）
        const auto  vel       = world.GetLinearVelocity(h);
        const float expectedV = -9.81f;
        const float dv        = vel.y - expectedV;
        if (std::fabs(dv) > 0.1f)
        {
            std::fprintf(stderr,
                         "[Box2DStepTest] free-fall: vy=%.4f expected=%.4f\n",
                         vel.y, expectedV);
        }
        assert(std::fabs(dv) <= 0.1f);
    }

    void TestStaticGroundCollision()
    {
        Phys::PhysicsWorld world; // 默认 gravity (0, -9.81)

        // 地面：static box，y = -5（让自由落体球 1 秒后正好砸到）
        Phys::RigidBodyComponent ground;
        ground.type            = Phys::BodyType::Static;
        ground.initialPosition = {0.0f, -5.0f};
        Phys::ColliderComponent groundCol;
        groundCol.shape       = Phys::BoxDesc{{50.0f, 0.5f}, {0.0f, 0.0f}};
        groundCol.friction    = 0.5f;
        groundCol.restitution = 0.0f;
        auto gh               = world.AddBody(ground, groundCol);
        assert(gh.IsValid());

        // ball：半径 0.5，从 y=0 落下
        Phys::RigidBodyComponent ball;
        ball.type            = Phys::BodyType::Dynamic;
        ball.initialPosition = {0.0f, 0.0f};
        Phys::ColliderComponent ballCol;
        ballCol.shape       = Phys::CircleDesc{0.5f, {0.0f, 0.0f}};
        ballCol.density     = 1.0f;
        ballCol.restitution = 0.0f;
        auto bh             = world.AddBody(ball, ballCol);
        assert(bh.IsValid());

        // 跑 2 秒（120 步），ball 应该静止在 ground 顶面之上：
        //   ground 顶面 y = -5 + 0.5（half height）= -4.5
        //   ball 球心   y = -4.5 + 0.5（半径）= -4.0
        constexpr int   kSteps = 120;
        constexpr float kDt    = 1.0f / 60.0f;
        for (int i = 0; i < kSteps; ++i)
        {
            world.Step(kDt);
        }

        const auto bxf = world.GetBodyTransform(bh);
        // 容忍：±0.1（contact 解算 / overlap 修正会有一两个像素位移）
        if (std::fabs(bxf.position.y - (-4.0f)) > 0.1f)
        {
            std::fprintf(stderr,
                         "[Box2DStepTest] ground rest: ball y=%.4f expected ≈ -4.0\n",
                         bxf.position.y);
        }
        assert(std::fabs(bxf.position.y - (-4.0f)) <= 0.1f);

        // ball 速度应该已经收敛接近 0（重力 vs 法向支持力平衡）
        const auto vel = world.GetLinearVelocity(bh);
        if (std::fabs(vel.y) > 0.5f)
        {
            std::fprintf(stderr,
                         "[Box2DStepTest] ground rest: ball vy=%.4f (expected ≈ 0)\n",
                         vel.y);
        }
        assert(std::fabs(vel.y) <= 0.5f);
    }

    void TestSetTransformAndVelocity()
    {
        Phys::PhysicsWorld world;

        Phys::RigidBodyComponent rb;
        rb.type            = Phys::BodyType::Kinematic; // kinematic：物理不主动改它
        rb.initialPosition = {1.0f, 2.0f};
        Phys::ColliderComponent col;
        col.shape = Phys::BoxDesc{{0.5f, 0.5f}, {0.0f, 0.0f}};
        auto h    = world.AddBody(rb, col);
        assert(h.IsValid());

        auto xf = world.GetBodyTransform(h);
        assert(std::fabs(xf.position.x - 1.0f) < 1e-4f);
        assert(std::fabs(xf.position.y - 2.0f) < 1e-4f);

        // 写入新位置
        world.SetBodyTransform(h, Phys::BodyTransform{{3.0f, 4.0f}, 0.0f});
        xf = world.GetBodyTransform(h);
        assert(std::fabs(xf.position.x - 3.0f) < 1e-4f);
        assert(std::fabs(xf.position.y - 4.0f) < 1e-4f);

        // kinematic body 有 velocity 概念但物理不积分；SetLinearVelocity 设了
        // 之后 GetLinearVelocity 应能立即读回。
        world.SetLinearVelocity(h, {5.0f, -3.0f});
        const auto vel = world.GetLinearVelocity(h);
        assert(std::fabs(vel.x - 5.0f) < 1e-4f);
        assert(std::fabs(vel.y + 3.0f) < 1e-4f);
    }

} // namespace

int main()
{
    TestFreeFall();
    TestStaticGroundCollision();
    TestSetTransformAndVelocity();
    return 0;
}
