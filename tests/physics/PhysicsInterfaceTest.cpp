// PhysicsInterfaceTest —— Phase 4 / Task 05 公共面端到端验证。
//
// 覆盖：
//   * default 构造 / 自定义 PhysicsWorldDesc 构造 / 析构干净；
//   * Step(dt) 在无 body / 有 body / dt < 0 三种路径下都不崩、StepCount 正确累加；
//   * AddBody 返回的 BodyHandle 唯一、有效；RigidBodyComponent.handle 字段调
//     用方自己写回（接口不暗中改 caller 的 component）；
//   * RemoveBody 后 IsValid → false；多次 RemoveBody 同 handle 是 idempotent；
//   * BodyCount 正确反映已注册 body 数；
//   * 4 种 ColliderDesc shape variant 都能构造 ColliderComponent 并提交。
//
// 不验证物理模拟正确性——本期 Step 是 no-op。Task 06 接 Box2D 后端后由
// Box2DStepTest 覆盖"自由落体 ball 1 秒后 y 误差 < 1%"这类语义验收。

#include "orange/engine/physics/PhysicsWorld.h"

#include <cassert>
#include <cstdint>
#include <unordered_set>

namespace Phys = Orange::Engine::Physics;

namespace
{

void TestConstructDestruct()
{
    // default
    {
        Phys::PhysicsWorld world;
        assert(world.BodyCount() == 0);
        assert(world.StepCount() == 0);
        assert(world.Desc().substepCount == 4);  // PhysicsWorldDesc 默认值
    }
    // explicit desc
    {
        Phys::PhysicsWorldDesc d;
        d.gravity      = {0.0f, -19.6f};
        d.substepCount = 8;
        Phys::PhysicsWorld world(d);
        assert(world.Desc().gravity.y < -19.0f);
        assert(world.Desc().substepCount == 8);
    }
}

void TestStepWithoutBodies()
{
    Phys::PhysicsWorld world;
    world.Step(1.0f / 60.0f);
    world.Step(1.0f / 60.0f);
    world.Step(1.0f / 60.0f);
    assert(world.StepCount() == 3);

    // 负 dt clamp 到 0：步进计数仍 +1（与正常 dt 同语义）。
    world.Step(-1.0f);
    assert(world.StepCount() == 4);
}

void TestAddRemoveBody()
{
    Phys::PhysicsWorld world;

    Phys::RigidBodyComponent body;
    body.type           = Phys::BodyType::Dynamic;
    body.gravityScale   = 1.0f;
    body.linearVelocity = {0.0f, 0.0f};

    Phys::ColliderComponent col;
    col.shape   = Phys::CircleDesc{1.0f, {0.0f, 0.0f}};
    col.density = 1.0f;

    auto h1 = world.AddBody(body, col);
    auto h2 = world.AddBody(body, col);
    auto h3 = world.AddBody(body, col);

    assert(h1.IsValid() && h2.IsValid() && h3.IsValid());
    // handle 互不相同
    std::unordered_set<Phys::BodyHandle::ValueType> seen{h1.Value(), h2.Value(), h3.Value()};
    assert(seen.size() == 3);

    assert(world.BodyCount() == 3);
    assert(world.IsValid(h1));
    assert(world.IsValid(h2));
    assert(world.IsValid(h3));

    world.RemoveBody(h2);
    assert(world.BodyCount() == 2);
    assert(world.IsValid(h1));
    assert(!world.IsValid(h2));
    assert(world.IsValid(h3));

    // idempotent：第二次 remove 同一 handle 不崩、不影响其他
    world.RemoveBody(h2);
    assert(world.BodyCount() == 2);

    // invalid handle 也不崩
    world.RemoveBody(Phys::BodyHandle::Invalid());
    assert(world.BodyCount() == 2);
}

void TestStepWithBodies()
{
    Phys::PhysicsWorld world;

    Phys::RigidBodyComponent body;
    Phys::ColliderComponent  col;
    col.shape = Phys::BoxDesc{{0.5f, 0.5f}, {0.0f, 0.0f}};

    auto h = world.AddBody(body, col);
    assert(h.IsValid());

    world.Step(1.0f / 60.0f);
    world.Step(1.0f / 60.0f);
    assert(world.StepCount() == 2);
    // body 仍在（stub 不会因为 Step 自动清理）
    assert(world.IsValid(h));
}

void TestAllShapeVariants()
{
    Phys::PhysicsWorld world;
    Phys::RigidBodyComponent body;

    // Circle
    {
        Phys::ColliderComponent col;
        col.shape = Phys::CircleDesc{0.5f, {0.0f, 0.0f}};
        assert(world.AddBody(body, col).IsValid());
    }
    // Box
    {
        Phys::ColliderComponent col;
        col.shape = Phys::BoxDesc{{1.0f, 0.25f}, {0.0f, 0.0f}};
        assert(world.AddBody(body, col).IsValid());
    }
    // Polygon
    {
        Phys::PolygonDesc poly{};
        poly.count       = 3;
        poly.vertices[0] = {0.0f, 0.0f};
        poly.vertices[1] = {1.0f, 0.0f};
        poly.vertices[2] = {0.5f, 1.0f};
        Phys::ColliderComponent col;
        col.shape = poly;
        assert(world.AddBody(body, col).IsValid());
    }
    // EdgeChain
    {
        Phys::EdgeChainDesc chain{};
        chain.count       = 4;
        chain.vertices[0] = {-5.0f, 0.0f};
        chain.vertices[1] = {-2.0f, 1.0f};
        chain.vertices[2] = { 2.0f, 1.0f};
        chain.vertices[3] = { 5.0f, 0.0f};
        chain.isLoop      = false;
        Phys::ColliderComponent col;
        col.shape = chain;
        assert(world.AddBody(body, col).IsValid());
    }

    assert(world.BodyCount() == 4);
}

}  // namespace

int main()
{
    TestConstructDestruct();
    TestStepWithoutBodies();
    TestAddRemoveBody();
    TestStepWithBodies();
    TestAllShapeVariants();
    return 0;
}
