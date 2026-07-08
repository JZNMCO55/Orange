// PlayAssemblyTest —— PIE M10 Play 装配同源 helper 行为契约测试。
//
// PlayAssembly 把编辑器宿主与发布 runtime 宿主共用的三段 Play 装配逻辑抽成
// 引擎层自由函数（PopulatePhysicsFromWorld / StepSimulation / InstantiateAudioSources）。
// 本测试锁死其**行为契约**，防止未来重构漂移出"编辑器能跑、发布行为不同"的缺口。
//
// headless、不接 GPU / 音频设备：
//   * PopulatePhysicsFromWorld：真 Box2D body 装配（handle 反写 + initial pos/angle 填充）。
//   * StepSimulation 扇出契约：module Tick 每帧调；宿主 physics step + 写回 ECS；
//     preStepHook 在 Step 之前触发；nullptr physics/vfx 安全。
//   * WantsOwnPhysicsStep 让位：模块自管物理时宿主不 Step、preStepHook 不触发，
//     但 module Tick 仍照常扇出。
//   （InstantiateAudioSources 需真音频设备 + SoundAsset，属直线数据搬迁，不在 headless 覆盖内。）

#include <orange/engine/game/PlayAssembly.h>

#include <orange/engine/game/GameModuleHost.h>
#include <orange/engine/game/IGameModule.h>
#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/PhysicsWorld.h>
#include <orange/engine/physics/RigidBodyComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>

namespace Phys = Orange::Engine::Physics;

using Orange::Engine::Entity;
using Orange::Engine::World;
using Orange::Engine::Game::GameModuleContext;
using Orange::Engine::Game::GameModuleHost;
using Orange::Engine::Game::IGameModule;
using Orange::Engine::Game::PopulatePhysicsFromWorld;
using Orange::Engine::Game::StepSimulation;
using Orange::Engine::Scene::TransformComponent;

namespace
{

    // 最小 IGameModule：记录 Tick 次数 + 可配 WantsOwnPhysicsStep，用于断言扇出契约。
    struct CountingModule : public IGameModule
    {
        int  tickCount{0};
        bool ownPhysics{false};

        explicit CountingModule(bool own) : ownPhysics(own) {}

        const char* Name() const noexcept override { return "counting"; }
        bool        WantsOwnPhysicsStep() const noexcept override { return ownPhysics; }
        void        Tick(GameModuleContext& /*ctx*/, float /*dt*/) override { ++tickCount; }
    };

    // 建一个挂 Transform + Dynamic RigidBody + Circle Collider 的实体。
    Entity SpawnDynamicBall(World& world, float x, float y, float angleZ)
    {
        Entity e = world.CreateEntity();

        TransformComponent tc{};
        tc.position = glm::vec3(x, y, 0.0f);
        tc.rotation = glm::quat(glm::vec3(0.0f, 0.0f, angleZ));
        world.AddComponent(e, tc);

        Phys::RigidBodyComponent rb{};
        rb.type         = Phys::BodyType::Dynamic;
        rb.gravityScale = 1.0f;
        world.AddComponent(e, rb);

        Phys::ColliderComponent col{};
        col.shape   = Phys::CircleDesc{0.5f, {0.0f, 0.0f}};
        col.density = 1.0f;
        world.AddComponent(e, col);

        return e;
    }

    // PopulatePhysicsFromWorld：从 Transform 填 initial pos/angle → AddBody → handle 反写。
    void TestPopulatePhysics()
    {
        World world;
        Entity e = SpawnDynamicBall(world, 3.0f, 7.0f, 1.2f);

        Phys::PhysicsWorldDesc d{};
        d.gravity = {0.0f, -9.81f};
        Phys::PhysicsWorld physics(d);

        PopulatePhysicsFromWorld(world, physics);

        assert(physics.BodyCount() == 1);
        const auto* rb = world.GetComponent<Phys::RigidBodyComponent>(e);
        assert(rb != nullptr);
        assert(rb->handle.IsValid());
        // initial pos/angle 从 Transform 填充（xy + Z 轴 euler）
        assert(std::fabs(rb->initialPosition.x - 3.0f) < 1e-4f);
        assert(std::fabs(rb->initialPosition.y - 7.0f) < 1e-4f);
        assert(std::fabs(rb->initialAngle - 1.2f) < 1e-3f);

        std::fprintf(stdout, "  [PASS] PopulatePhysicsFromWorld 装配 body + 反写 handle/initial\n");
    }

    // StepSimulation 扇出：module Tick 每帧 + 宿主 physics step 写回 ECS + preStepHook 触发。
    void TestStepFanOutWithPhysics()
    {
        World  world;
        Entity e = SpawnDynamicBall(world, 0.0f, 0.0f, 0.0f);

        Phys::PhysicsWorldDesc d{};
        d.gravity      = {0.0f, -9.81f};
        d.substepCount = 4;
        Phys::PhysicsWorld physics(d);
        PopulatePhysicsFromWorld(world, physics);

        GameModuleHost host;
        auto*          mod = static_cast<CountingModule*>(
            host.AddModule(std::make_unique<CountingModule>(/*own=*/false)));

        GameModuleContext ctx{};
        ctx.pWorld   = &world;
        ctx.pPhysics = &physics;
        host.EnterPlay(ctx);

        int             preHookCalls = 0;
        constexpr int   kSteps       = 60;
        constexpr float kDt          = 1.0f / 60.0f;
        for (int i = 0; i < kSteps; ++i)
        {
            StepSimulation(host, ctx, world, &physics, /*vfx=*/nullptr, kDt,
                           [&preHookCalls]() { ++preHookCalls; });
        }

        // module Tick 每帧扇出
        assert(mod->tickCount == kSteps);
        // 宿主自动 step（无模块自管）→ preStepHook 每帧在 Step 前触发
        assert(preHookCalls == kSteps);
        assert(physics.StepCount() == kSteps);
        // 物理位姿写回 ECS Transform：自由落体 1 秒 y ≈ -4.905
        const auto* tc = world.GetComponent<TransformComponent>(e);
        assert(tc != nullptr);
        const float expectedY = -0.5f * 9.81f;
        assert(std::fabs(tc->position.y - expectedY) < 0.1f);

        host.ExitPlay(ctx);
        std::fprintf(stdout, "  [PASS] StepSimulation 扇出：module Tick + physics step 写回 + preStepHook\n");
    }

    // WantsOwnPhysicsStep 让位：模块自管物理时宿主不 Step、preStepHook 不触发，module Tick 照常。
    void TestModuleOwnsPhysicsGate()
    {
        World  world;
        Entity e = SpawnDynamicBall(world, 0.0f, 0.0f, 0.0f);

        Phys::PhysicsWorld physics; // 默认 gravity
        PopulatePhysicsFromWorld(world, physics);

        GameModuleHost host;
        auto*          mod = static_cast<CountingModule*>(
            host.AddModule(std::make_unique<CountingModule>(/*own=*/true)));

        GameModuleContext ctx{};
        ctx.pWorld   = &world;
        ctx.pPhysics = &physics;
        host.EnterPlay(ctx);

        int preHookCalls = 0;
        StepSimulation(host, ctx, world, &physics, /*vfx=*/nullptr, 1.0f / 60.0f,
                       [&preHookCalls]() { ++preHookCalls; });

        // 模块自管物理 → 宿主让位：不 Step、preStepHook 不触发
        assert(physics.StepCount() == 0);
        assert(preHookCalls == 0);
        // Transform 不因宿主 step 变化（模块在自己的 accumulator 内处理，本测试模块不动它）
        const auto* tc = world.GetComponent<TransformComponent>(e);
        assert(tc != nullptr);
        assert(std::fabs(tc->position.y) < 1e-4f);
        // module Tick 仍照常扇出
        assert(mod->tickCount == 1);

        host.ExitPlay(ctx);
        std::fprintf(stdout, "  [PASS] WantsOwnPhysicsStep 让位：宿主不 step / preStepHook 不触发 / Tick 照常\n");
    }

    // nullptr physics/vfx 安全：无物理世界时 StepSimulation 不 Step、不触发 preStepHook、不崩。
    void TestNullPhysicsSafety()
    {
        World world;
        world.CreateEntity(); // 空实体，仅确保 world 非空迭代路径安全

        GameModuleHost host;
        auto*          mod = static_cast<CountingModule*>(
            host.AddModule(std::make_unique<CountingModule>(/*own=*/false)));

        GameModuleContext ctx{};
        ctx.pWorld = &world;
        host.EnterPlay(ctx);

        int preHookCalls = 0;
        StepSimulation(host, ctx, world, /*physics=*/nullptr, /*vfx=*/nullptr, 1.0f / 60.0f,
                       [&preHookCalls]() { ++preHookCalls; });

        assert(preHookCalls == 0); // physics==nullptr → 整个物理分支跳过（连带 preStepHook）
        assert(mod->tickCount == 1);

        host.ExitPlay(ctx);
        std::fprintf(stdout, "  [PASS] nullptr physics/vfx 安全：跳过物理分支 + preStepHook 不触发\n");
    }

} // namespace

int main()
{
    std::fprintf(stdout, "[PlayAssemblyTest] running\n");
    TestPopulatePhysics();
    TestStepFanOutWithPhysics();
    TestModuleOwnsPhysicsGate();
    TestNullPhysicsSafety();
    std::fprintf(stdout, "[PlayAssemblyTest] all tests passed.\n");
    return 0;
}
