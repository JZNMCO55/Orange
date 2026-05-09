// FixtureReplaceTest —— Phase 4 / Task 07 验收：
// PhysicsWorld::ReplaceFixture 在运行时把 body 上的 collider 整体换掉，
// 同时（a）让新 shape 真接触场景做 contact 解算，（b）让 dynamic body 的
// mass 跟着新形状 + density 重算。
//
// 场景设计：
//   - 静态 ground box，顶面 y = -4.5；
//   - dynamic ball，初始半径 R1 = 1.0、density 1.0。
//     ↳ 跑 2 秒 → 静止时球心 y ≈ -4.5 + R1 = -3.5；
//     ↳ 此时质量 M1 ≈ ρ·π·R1² = π。
//   - 调 ReplaceFixture，把球换成 R2 = 0.3 / density 1.0：
//     ↳ 再跑 2 秒 → 静止时球心 y ≈ -4.5 + R2 = -4.2；
//     ↳ 质量 M2 ≈ ρ·π·R2² = 0.09 π ≈ 0.283。
//
// 这一对前后比较同时覆盖：
//   * 旧 shape 被正确销毁（如不销毁、新 R=0.3 shape 与旧 R=1.0 shape 同
//     时存在，球心会卡在旧半径决定的位置 -3.5 上）；
//   * 新 shape 真挂上 body 并参与 contact filtering（球心落到新半径）；
//   * mass 由 ApplyMassFromShapes 重算（M2 != M1，且 M2 < M1）。
//
// 也覆盖几条 invalid-handle 边界：替换 invalid handle / 已 Remove 的 handle
// 都返回 false 且不崩。

#include "orange/engine/physics/PhysicsWorld.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace Phys = Orange::Engine::Physics;

namespace
{

constexpr float kPi = 3.14159265358979323846f;

void RunSeconds(Phys::PhysicsWorld& world, float seconds)
{
    constexpr float kDt = 1.0f / 60.0f;
    const int       n   = static_cast<int>(seconds * 60.0f + 0.5f);
    for (int i = 0; i < n; ++i)
    {
        world.Step(kDt);
    }
}

void TestReplaceCircleShrinksRestPosition()
{
    Phys::PhysicsWorld world;  // 默认 gravity (0, -9.81)

    // ground：static box，顶面 y = -4.5
    Phys::RigidBodyComponent ground;
    ground.type            = Phys::BodyType::Static;
    ground.initialPosition = {0.0f, -5.0f};
    Phys::ColliderComponent groundCol;
    groundCol.shape       = Phys::BoxDesc{{50.0f, 0.5f}, {0.0f, 0.0f}};
    groundCol.friction    = 0.5f;
    groundCol.restitution = 0.0f;
    auto gh = world.AddBody(ground, groundCol);
    assert(gh.IsValid());

    // ball：dynamic 半径 1.0、density 1.0、不弹
    Phys::RigidBodyComponent ball;
    ball.type            = Phys::BodyType::Dynamic;
    ball.initialPosition = {0.0f, 0.0f};
    Phys::ColliderComponent ballCol;
    ballCol.shape       = Phys::CircleDesc{1.0f, {0.0f, 0.0f}};
    ballCol.density     = 1.0f;
    ballCol.restitution = 0.0f;
    auto bh = world.AddBody(ball, ballCol);
    assert(bh.IsValid());

    // R1 静止 → 球心 y ≈ -3.5
    RunSeconds(world, 2.0f);
    const auto xf1 = world.GetBodyTransform(bh);
    if (std::fabs(xf1.position.y - (-3.5f)) > 0.1f)
    {
        std::fprintf(stderr,
                     "[FixtureReplaceTest] R1 rest: y=%.4f expected ≈ -3.5\n",
                     xf1.position.y);
    }
    assert(std::fabs(xf1.position.y - (-3.5f)) <= 0.1f);

    const float massBefore = world.GetMass(bh);
    const float expectedM1 = 1.0f * kPi * 1.0f * 1.0f;  // ρ·π·R²
    if (std::fabs(massBefore - expectedM1) > 0.05f)
    {
        std::fprintf(stderr,
                     "[FixtureReplaceTest] M1: %.4f expected ≈ %.4f\n",
                     massBefore, expectedM1);
    }
    assert(std::fabs(massBefore - expectedM1) <= 0.05f);

    // 把 collider 换成半径 0.3 的圆
    Phys::ColliderComponent ballCol2;
    ballCol2.shape       = Phys::CircleDesc{0.3f, {0.0f, 0.0f}};
    ballCol2.density     = 1.0f;
    ballCol2.restitution = 0.0f;
    const bool ok = world.ReplaceFixture(bh, ballCol2);
    assert(ok);

    // 替换后 mass 立刻应当变为 ρ·π·0.3² ≈ 0.283。
    const float massAfter  = world.GetMass(bh);
    const float expectedM2 = 1.0f * kPi * 0.3f * 0.3f;
    if (std::fabs(massAfter - expectedM2) > 0.02f)
    {
        std::fprintf(stderr,
                     "[FixtureReplaceTest] M2: %.4f expected ≈ %.4f\n",
                     massAfter, expectedM2);
    }
    assert(std::fabs(massAfter - expectedM2) <= 0.02f);
    assert(massAfter < massBefore);

    // 再跑 2 秒，球心应静止在 y ≈ -4.2（地面 -4.5 + 新半径 0.3）。
    // 这是 contact filtering 是否真用上"新形状"的核心验证：如果旧 R=1
    // shape 没被清掉，球心会卡在 -3.5；只有新 shape 接管了 contact，
    // 球才会继续落到 -4.2。
    RunSeconds(world, 2.0f);
    const auto xf2 = world.GetBodyTransform(bh);
    if (std::fabs(xf2.position.y - (-4.2f)) > 0.1f)
    {
        std::fprintf(stderr,
                     "[FixtureReplaceTest] R2 rest: y=%.4f expected ≈ -4.2\n",
                     xf2.position.y);
    }
    assert(std::fabs(xf2.position.y - (-4.2f)) <= 0.1f);
    assert(xf2.position.y < xf1.position.y);  // 球确实进一步下沉
}

void TestReplaceWithBoxThenBackToCircle()
{
    Phys::PhysicsWorld world;

    Phys::RigidBodyComponent body;
    body.type            = Phys::BodyType::Dynamic;
    body.initialPosition = {0.0f, 0.0f};
    Phys::ColliderComponent col;
    col.shape   = Phys::CircleDesc{1.0f, {0.0f, 0.0f}};
    col.density = 1.0f;
    auto h = world.AddBody(body, col);
    assert(h.IsValid());

    const float m0 = world.GetMass(h);
    assert(m0 > 0.0f);

    // 换成 1×1 的盒子（half-extents 0.5）：mass = ρ·(2·0.5)·(2·0.5)·1 = 1.0
    Phys::ColliderComponent boxCol;
    boxCol.shape   = Phys::BoxDesc{{0.5f, 0.5f}, {0.0f, 0.0f}};
    boxCol.density = 1.0f;
    assert(world.ReplaceFixture(h, boxCol));
    const float mBox = world.GetMass(h);
    if (std::fabs(mBox - 1.0f) > 0.02f)
    {
        std::fprintf(stderr, "[FixtureReplaceTest] box mass=%.4f expected ≈ 1.0\n", mBox);
    }
    assert(std::fabs(mBox - 1.0f) <= 0.02f);

    // 再换回 R=2 的圆：mass = ρ·π·4 ≈ 12.566
    Phys::ColliderComponent bigCircle;
    bigCircle.shape   = Phys::CircleDesc{2.0f, {0.0f, 0.0f}};
    bigCircle.density = 1.0f;
    assert(world.ReplaceFixture(h, bigCircle));
    const float mBig = world.GetMass(h);
    const float expected = kPi * 4.0f;
    if (std::fabs(mBig - expected) > 0.1f)
    {
        std::fprintf(stderr, "[FixtureReplaceTest] big-circle mass=%.4f expected ≈ %.4f\n",
                     mBig, expected);
    }
    assert(std::fabs(mBig - expected) <= 0.1f);
    assert(mBig > mBox);
}

void TestReplaceInvalidHandle()
{
    Phys::PhysicsWorld world;

    Phys::ColliderComponent col;
    col.shape = Phys::CircleDesc{0.5f, {0.0f, 0.0f}};

    // invalid handle
    assert(!world.ReplaceFixture(Phys::BodyHandle::Invalid(), col));

    // remove 后再 replace → false（handle 已不在表里）
    Phys::RigidBodyComponent rb;
    rb.type = Phys::BodyType::Dynamic;
    auto h  = world.AddBody(rb, col);
    assert(h.IsValid());
    world.RemoveBody(h);
    assert(!world.ReplaceFixture(h, col));

    // 不崩 / 不破坏 world：还能继续 AddBody / Step
    auto h2 = world.AddBody(rb, col);
    assert(h2.IsValid());
    world.Step(1.0f / 60.0f);
}

}  // namespace

int main()
{
    TestReplaceCircleShrinksRestPosition();
    TestReplaceWithBoxThenBackToCircle();
    TestReplaceInvalidHandle();
    return 0;
}
