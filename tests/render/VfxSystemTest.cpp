// VfxSystem 单元测试。
//
// 覆盖 4 条线（不依赖 GPU——仅 Tick 路径）：
//   * Empty World 无副作用：Tick 后 0 粒子；
//   * spawn 计数随 emissionRate × time 单调（freeze on cap）；
//   * 粒子寿命到点被回收；
//   * Entity 销毁后池被回收（TotalLiveParticleCount 归零）。
//
// 不进 GPU 路径（DrawParticles 走 RHI，需要真 RenderDevice + window）；
// 视觉层验证留给 samples/09_vfx_demo 跑起来肉眼确认。

#include <orange/engine/render/ParticleEmitterComponent.h>
#include <orange/engine/render/VfxSystem.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <cassert>
#include <cstdio>

using Orange::Engine::Entity;
using Orange::Engine::World;
using Orange::Engine::Render::ParticleEmitterComponent;
using Orange::Engine::Render::ParticleEmitterDesc;
using Orange::Engine::Render::VfxSystem;
using Orange::Engine::Scene::TransformComponent;

namespace
{

ParticleEmitterDesc MakeDesc(float rate, float lifetime, std::uint32_t cap = 256)
{
    ParticleEmitterDesc d{};
    d.emissionRate       = rate;
    d.lifetimeMin        = lifetime;
    d.lifetimeMax        = lifetime;
    d.spawnOffsetMin     = {0.0f, 0.0f};
    d.spawnOffsetMax     = {0.0f, 0.0f};
    d.initialVelocityMin = {0.0f, 0.0f};
    d.initialVelocityMax = {0.0f, 0.0f};
    d.gravity            = {0.0f, 0.0f};
    d.maxParticles       = cap;
    d.sizeStart          = 0.05f;
    d.sizeEnd            = 0.05f;
    return d;
}

void TestEmptyWorldNoEffect()
{
    World world;
    VfxSystem vfx;

    vfx.Tick(world, 1.0f / 60.0f);
    assert(vfx.TotalLiveParticleCount() == 0);

    std::fprintf(stdout, "  [PASS] empty world -> 0 particles\n");
}

void TestSpawnCountMonotonic()
{
    World world;
    Entity e = world.CreateEntity();
    world.AddComponent<TransformComponent>(e, {});
    world.AddComponent<ParticleEmitterComponent>(e, {MakeDesc(/*rate=*/100.0f,
                                                              /*lifetime=*/10.0f,
                                                              /*cap=*/512), true});

    VfxSystem vfx;
    // 在 100 / s + lifetime=10s 下，前 1 秒应 spawn ~100 粒子。Tick 60 次
    // dt=1/60 模拟 1 秒。
    for (int i = 0; i < 60; ++i)
    {
        vfx.Tick(world, 1.0f / 60.0f);
    }
    const std::size_t after1s = vfx.LiveParticleCount(e);
    // emissionRate=100/s × 1s = 100，允许 ±2 的 spawn-累加器边界误差。
    assert(after1s >= 98 && after1s <= 102);

    // 再跑 1 秒，粒子继续增长（远未达 cap）。
    for (int i = 0; i < 60; ++i)
    {
        vfx.Tick(world, 1.0f / 60.0f);
    }
    const std::size_t after2s = vfx.LiveParticleCount(e);
    assert(after2s >= 198 && after2s <= 202);

    std::fprintf(stdout, "  [PASS] spawn count monotonic: 1s=%zu, 2s=%zu\n",
                 after1s, after2s);
}

void TestParticleAgesOutAndRecycles()
{
    World world;
    Entity e = world.CreateEntity();
    world.AddComponent<TransformComponent>(e, {});
    // emissionRate=100/s + lifetime=0.5s → 稳定状态约 50 粒子。
    world.AddComponent<ParticleEmitterComponent>(e,
        {MakeDesc(/*rate=*/100.0f, /*lifetime=*/0.5f, /*cap=*/512), true});

    VfxSystem vfx;
    // 先跑 0.5 秒 → ~50 粒子，最早 spawn 的开始到期。
    for (int i = 0; i < 30; ++i)
    {
        vfx.Tick(world, 1.0f / 60.0f);
    }
    const std::size_t at_05 = vfx.LiveParticleCount(e);

    // 再跑 0.5 秒 → 旧粒子已被回收，活粒子稳定在约 50 上下（spawn ≈ kill）。
    for (int i = 0; i < 30; ++i)
    {
        vfx.Tick(world, 1.0f / 60.0f);
    }
    const std::size_t at_1s = vfx.LiveParticleCount(e);

    // 关键：at_1s 不应远超 at_05（说明 recycle 工作中）。给充裕的窗口
    // [40, 60]——稳定态 ~50。
    assert(at_1s >= 40 && at_1s <= 60);

    // 关掉发射 → 再跑 1 秒后应该全部到期归零。
    auto* pe = world.GetComponent<ParticleEmitterComponent>(e);
    pe->emitting = false;
    for (int i = 0; i < 60; ++i)
    {
        vfx.Tick(world, 1.0f / 60.0f);
    }
    const std::size_t after_off = vfx.LiveParticleCount(e);
    assert(after_off == 0);

    std::fprintf(stdout, "  [PASS] aging + recycling: 0.5s=%zu, 1s=%zu, off+1s=%zu\n",
                 at_05, at_1s, after_off);
}

void TestCapNotExceeded()
{
    World world;
    Entity e = world.CreateEntity();
    world.AddComponent<TransformComponent>(e, {});
    // 高 emissionRate + 长 lifetime + 低 cap → 池满后新 spawn 被 drop。
    world.AddComponent<ParticleEmitterComponent>(e,
        {MakeDesc(/*rate=*/10000.0f, /*lifetime=*/10.0f, /*cap=*/32), true});

    VfxSystem vfx;
    // 跑 1 秒：emissionRate × time = 10000，远超 cap=32。
    for (int i = 0; i < 60; ++i)
    {
        vfx.Tick(world, 1.0f / 60.0f);
    }
    const std::size_t live = vfx.LiveParticleCount(e);
    assert(live == 32);

    std::fprintf(stdout, "  [PASS] maxParticles cap respected (live=%zu)\n", live);
}

void TestEntityDestroyReleasesPool()
{
    World world;
    Entity e = world.CreateEntity();
    world.AddComponent<TransformComponent>(e, {});
    world.AddComponent<ParticleEmitterComponent>(e,
        {MakeDesc(/*rate=*/100.0f, /*lifetime=*/2.0f, /*cap=*/256), true});

    VfxSystem vfx;
    for (int i = 0; i < 30; ++i)
    {
        vfx.Tick(world, 1.0f / 60.0f);
    }
    assert(vfx.TotalLiveParticleCount() > 0);

    world.DestroyEntity(e);
    // 下一次 Tick：池在校验 world.IsValid 时被擦掉。
    vfx.Tick(world, 1.0f / 60.0f);
    assert(vfx.TotalLiveParticleCount() == 0);
    assert(vfx.LiveParticleCount(e) == 0);

    std::fprintf(stdout, "  [PASS] entity destroy -> pool released\n");
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[VfxSystemTest] running\n");
    TestEmptyWorldNoEffect();
    TestSpawnCountMonotonic();
    TestParticleAgesOutAndRecycles();
    TestCapNotExceeded();
    TestEntityDestroyReleasesPool();
    std::fprintf(stdout, "[VfxSystemTest] all tests passed.\n");
    return 0;
}
