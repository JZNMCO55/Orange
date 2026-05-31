// AnimationSystemTest —— 锁住 "World 上所有 AnimatorComponent 每帧被 Tick"
// 这条 ECS 集成链（Animation::TickAnimators / AnimationSystem）。
//
// 此前 ProceduralAnimatorTest 只测单体 animator 的 Tick → SetUniform；这条
// "World → view<AnimatorComponent> → IAnimator::Tick → MaterialInstance
// uniform" 的 ECS 集成是 OrangeEditor 内联手写、零自动化覆盖。本测试把它锁在
// 引擎层（headless，只链 orange_engine，无 Vulkan / GUI）。
//
// 覆盖：
//   1. 多 entity 多 animator：TickAnimators 返回 tick 数 = 非空 animator 数；
//      每个 animator 的 elapsed / 写出的 uniform 都被推进。
//   2. animator == nullptr 的 AnimatorComponent（半构造态，仿 Scene::Load
//      backend 解析失败）被跳过、不计入返回数、不崩。
//   3. ProceduralAnimator 真驱动 MaterialInstance uniform：Tick 后
//      GetUniformVec4 拿到 fn(elapsed) 的值（验证 uBaseColor 这条首游动画路径）。
//   4. AnimationSystem(ISystem)::OnUpdate 用 FrameContext.time.deltaSeconds
//      调 TickAnimators，行为与直接调自由函数一致。
//   5. 空 World / 无 AnimatorComponent：返回 0、不崩。
//
// 走最小 <cassert> + 独立 main()，exit 0 即通过。

#include <orange/engine/animation/AnimationSystem.h>
#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/animation/ProceduralAnimator.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/render/Material.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialTypes.h>
#include <orange/engine/scene/World.h>

#include <glm/vec4.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>

namespace Ani = Orange::Engine::Animation;
namespace Rd  = Orange::Engine::Render;
using Orange::Engine::Entity;
using Orange::Engine::FrameContext;
using Orange::Engine::World;

namespace
{

// 构造一个带 uBaseColor(vec4) uniform 的 Material —— ProceduralAnimator 的
// channel 写它（与 DemoWorld 史莱姆呼吸 demo 同款 uniform）。shader handle 不
// 填，本测试不渲染，Material 只作 MaterialInstance 的 uniform 描述源。
Rd::Material MakeBaseColorMaterial()
{
    Rd::Material m;
    m.name     = "test/anim";
    m.uniforms = {
        {"uBaseColor", Rd::MaterialUniformType::Vec4},
    };
    return m;
}

bool Approx(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) <= eps;
}

// 建一个 ProceduralAnimator，channel 把 uBaseColor.x 设为 elapsed 本身（线性，
// 便于断言：Tick(dt) 后 uBaseColor.x == 累计 elapsed）。
std::unique_ptr<Ani::ProceduralAnimator> MakeLinearAnimator(Rd::MaterialInstance* target)
{
    auto anim = std::make_unique<Ani::ProceduralAnimator>(target);
    anim->AddChannel<glm::vec4>(
        "uBaseColor",
        [](float t) { return glm::vec4(t, 0.0f, 0.0f, 1.0f); });
    return anim;
}

// ===== 1 + 3. 多 animator 被 tick + 真驱动 uniform =====
void TestMultipleAnimatorsTicked()
{
    World world;

    auto matA = MakeBaseColorMaterial();
    auto matB = MakeBaseColorMaterial();
    Rd::MaterialInstance miA(&matA);
    Rd::MaterialInstance miB(&matB);

    Entity eA = world.CreateEntity();
    Entity eB = world.CreateEntity();
    {
        Ani::AnimatorComponent ac{};
        ac.animator = MakeLinearAnimator(&miA);
        world.AddComponent<Ani::AnimatorComponent>(eA, std::move(ac));
    }
    {
        Ani::AnimatorComponent ac{};
        ac.animator = MakeLinearAnimator(&miB);
        world.AddComponent<Ani::AnimatorComponent>(eB, std::move(ac));
    }

    const std::size_t n = Ani::TickAnimators(world, 0.5f);
    assert(n == 2 && "两个非空 animator 都应被 tick");

    auto a = miA.GetUniformVec4("uBaseColor");
    auto b = miB.GetUniformVec4("uBaseColor");
    assert(a.has_value() && Approx(a->x, 0.5f) && "animator A 应把 uBaseColor.x 推到 0.5");
    assert(b.has_value() && Approx(b->x, 0.5f) && "animator B 应把 uBaseColor.x 推到 0.5");

    // 再 Tick 一次累计：elapsed = 0.5 + 0.5 = 1.0
    Ani::TickAnimators(world, 0.5f);
    a = miA.GetUniformVec4("uBaseColor");
    assert(a.has_value() && Approx(a->x, 1.0f) && "累计 elapsed 应到 1.0");

    std::fprintf(stdout, "  [PASS] 多 animator 被 tick + 真驱动 MaterialInstance uniform\n");
}

// ===== 2. nullptr animator 跳过 =====
void TestNullAnimatorSkipped()
{
    World world;

    auto mat = MakeBaseColorMaterial();
    Rd::MaterialInstance mi(&mat);

    Entity eLive = world.CreateEntity();
    Entity eNull = world.CreateEntity();
    {
        Ani::AnimatorComponent ac{};
        ac.animator = MakeLinearAnimator(&mi);
        world.AddComponent<Ani::AnimatorComponent>(eLive, std::move(ac));
    }
    {
        // 半构造态：animator == nullptr（仿 Scene::Load backend 解析失败）
        Ani::AnimatorComponent ac{};
        ac.animator = nullptr;
        world.AddComponent<Ani::AnimatorComponent>(eNull, std::move(ac));
    }

    const std::size_t n = Ani::TickAnimators(world, 1.0f);
    assert(n == 1 && "nullptr animator 应被跳过、不计入 tick 数");

    auto v = mi.GetUniformVec4("uBaseColor");
    assert(v.has_value() && Approx(v->x, 1.0f) && "非空 animator 仍正常 tick");

    std::fprintf(stdout, "  [PASS] nullptr animator 跳过、不崩\n");
}

// ===== 4. AnimationSystem::OnUpdate 用 dt 调 TickAnimators =====
void TestAnimationSystemOnUpdate()
{
    World world;

    auto mat = MakeBaseColorMaterial();
    Rd::MaterialInstance mi(&mat);

    Entity e = world.CreateEntity();
    {
        Ani::AnimatorComponent ac{};
        ac.animator = MakeLinearAnimator(&mi);
        world.AddComponent<Ani::AnimatorComponent>(e, std::move(ac));
    }

    Ani::AnimationSystem system;
    FrameContext frame{};
    frame.time.deltaSeconds = 0.25;
    system.OnUpdate(world, frame);

    auto v = mi.GetUniformVec4("uBaseColor");
    assert(v.has_value() && Approx(v->x, 0.25f) &&
           "AnimationSystem::OnUpdate 应用 frame dt 调 TickAnimators");

    std::fprintf(stdout, "  [PASS] AnimationSystem::OnUpdate 行为与自由函数一致\n");
}

// ===== 5. 空 World / 无 AnimatorComponent =====
void TestEmptyWorld()
{
    World world;
    const std::size_t n = Ani::TickAnimators(world, 1.0f);
    assert(n == 0 && "空 World tick 数应为 0");

    // 有 entity 但无 AnimatorComponent
    world.CreateEntity();
    const std::size_t n2 = Ani::TickAnimators(world, 1.0f);
    assert(n2 == 0 && "无 AnimatorComponent 的 entity 不应被 tick");

    std::fprintf(stdout, "  [PASS] 空 World / 无 AnimatorComponent 返回 0、不崩\n");
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[AnimationSystemTest] start\n");
    TestMultipleAnimatorsTicked();
    TestNullAnimatorSkipped();
    TestAnimationSystemOnUpdate();
    TestEmptyWorld();
    std::fprintf(stdout, "[AnimationSystemTest] all tests passed.\n");
    return 0;
}
