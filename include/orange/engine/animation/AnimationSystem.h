#ifndef ORANGE_ENGINE_ANIMATION_ANIMATION_SYSTEM_H
#define ORANGE_ENGINE_ANIMATION_ANIMATION_SYSTEM_H

// ---------------------------------------------------------------------------
// AnimationSystem —— 每帧推进 World 上所有 AnimatorComponent 的动画时钟。
//
// 在此之前，"遍历 World 的 AnimatorComponent 并逐个 Tick"这段 ECS 集成逻辑
// 是 OrangeEditor 在自己的 Layer::OnUpdate 里手写的内联循环（编辑器 Play 模式
// 专用）。引擎本体没有可复用入口 —— 游戏侧消费引擎时得重新发明同一段循环，
// 且这条 "World → AnimatorComponent → IAnimator::Tick → 下游（MaterialInstance
// uniform / skeleton palette）" 的链路缺少引擎层的单一真相源与测试覆盖。
//
// 本文件把它沉淀为引擎能力，提供两种用法：
//
//   * 自由函数 TickAnimators(World&, dt)：核心逻辑，view<AnimatorComponent>
//     逐个 Tick（跳过 animator == nullptr 的半构造态）。无 FrameContext 依赖，
//     游戏 / 编辑器 / headless 测试都能直接调。
//   * AnimationSystem（ISystem 子类）：把自由函数接到 ISystem 调度体系，
//     OnUpdate 用 frame.time.deltaSeconds 调 TickAnimators。给将来的
//     SystemScheduler / 游戏主循环按 system 列表统一 tick 用。
//
// 设计取舍：
//   * 不在这里做 Play / Edit 门控 —— 那是消费方（编辑器）的状态语义，引擎层
//     只负责"给定 dt 就推进"。编辑器仍在自己的 PlayState::Play 分支里调本函数。
//   * 不持有 World —— 与现有 ISystem 约定一致（OnUpdate 收 World&，system 不
//     拥有 World）。
//   * 只依赖 World + AnimatorComponent + IAnimator，无 dragonbones / Render
//     依赖，故可编进纯 headless 测试（不链 Vulkan / GUI）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/scene/ISystem.h>

#include <cstddef>

namespace Orange::Engine
{
    class World;
}

namespace Orange::Engine::Animation
{

    // 遍历 world 上所有 AnimatorComponent，对每个非空 animator 调 Tick(dt)。
    // 返回实际 tick 的 animator 数量（animator == nullptr 的 component 跳过、不计入）。
    // dt 透传给 IAnimator::Tick —— 负值由各后端自行处理（ProceduralAnimator clamp
    // 到 0、不推进 elapsed）。
    ORANGE_ENGINE_API std::size_t TickAnimators(World& world, float dt);

    // ISystem 适配：把 TickAnimators 接到"每帧 tick 的 system"调度体系。
    class ORANGE_ENGINE_API AnimationSystem final : public Scene::ISystem
    {
    public:
        AnimationSystem() = default;

        // 用 frame.time.deltaSeconds（秒）调 TickAnimators(world, dt)。
        void OnUpdate(World& world, const FrameContext& frame) override;
    };

} // namespace Orange::Engine::Animation

#endif // ORANGE_ENGINE_ANIMATION_ANIMATION_SYSTEM_H
