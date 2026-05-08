#ifndef ORANGE_ENGINE_ANIMATION_ANIMATOR_COMPONENT_H
#define ORANGE_ENGINE_ANIMATION_ANIMATOR_COMPONENT_H

// ---------------------------------------------------------------------------
// AnimatorComponent —— ECS 组件，把 IAnimator 实例挂到 entity 上。
//
// 与 RenderableComponent / MaterialInstance 的"非拥有指针"模式**不同**：
// Animator 比 Material 更状态化（per-entity local clock / 动画进度），
// 让 component **拥有** unique_ptr 是更安全的语义——entity 销毁时
// animator 自动析构，不需要调用方在外侧手动管理 vector<unique_ptr>。
//
// 后果：AnimatorComponent 是 move-only，不再 trivially-copyable——
// EnTT archetype 在 add component 时会用 move 构造，但**不能 memcpy**。
// 这是相对其它 component 的特例；可接受，因为 archetype 行迁移代价仍在
// move 范畴内。日后若发现 hot-path 影响，可改回非拥有式 + 外侧 vector
// 持有，但目前 0.x 阶段 entity 数量不大，move-only 拥有式更易用。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/animation/IAnimator.h>

#include <memory>

namespace Orange::Engine::Animation
{

struct AnimatorComponent
{
    std::unique_ptr<IAnimator> animator;
};

}  // namespace Orange::Engine::Animation

#endif  // ORANGE_ENGINE_ANIMATION_ANIMATOR_COMPONENT_H
