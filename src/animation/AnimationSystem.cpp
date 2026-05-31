// AnimationSystem 实现 —— 见同名 .h 头注释。

#include "orange/engine/animation/AnimationSystem.h"

#include "orange/engine/animation/AnimatorComponent.h"
#include "orange/engine/scene/World.h"

namespace Orange::Engine::Animation
{

std::size_t TickAnimators(World& world, float dt)
{
    std::size_t ticked = 0;
    // 直接走 registry view —— AnimatorComponent 是 move-only（持
    // unique_ptr<IAnimator>），view 拿引用不拷贝。半构造态（animator ==
    // nullptr，典型：Scene::Load 时 backend name 解析失败）跳过、不计数。
    auto& registry = world.Registry();
    for (auto e : registry.view<AnimatorComponent>())
    {
        auto& ac = registry.get<AnimatorComponent>(e);
        if (ac.animator != nullptr)
        {
            ac.animator->Tick(dt);
            ++ticked;
        }
    }
    return ticked;
}

void AnimationSystem::OnUpdate(World& world, const FrameContext& frame)
{
    TickAnimators(world, static_cast<float>(frame.time.deltaSeconds));
}

}  // namespace Orange::Engine::Animation
