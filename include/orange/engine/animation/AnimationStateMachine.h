#ifndef ORANGE_ENGINE_ANIMATION_ANIMATION_STATE_MACHINE_H
#define ORANGE_ENGINE_ANIMATION_ANIMATION_STATE_MACHINE_H

// ---------------------------------------------------------------------------
// AnimationStateMachine —— 轻量 flat-weighted FSM。
//
// 按 `vendor/Orange-Wiki/wiki/techniques/animation/animation-state-machine.md`
// 的"两种 ASM 架构"，当前只交付最简的 flat
// weighted-average 入口（状态名 + 进入/退出回调 + 条件过渡）；blend
// tree 留待后续扩展。状态本身的 cross-fade 由各 backend 自管（DragonBones
// 切 anim 时设 fade 时长，Procedural 后端不需要 fade）。
//
// 状态节点：用 `std::string` 名字索引；进入 / 退出回调 + 用户自定义
// onTick 都通过 `std::function` 传入。过渡条件是一个 predicate
// `bool(const StateContext&)`——由调用方写，避免引入专用 condition DSL
// （与项目"无 reflection / 无 codegen"约束一致）。
//
// **不**支持：
//   * blend tree（待后续扩展）；
//   * layered ASM（后续随 IK / additive 一起做）；
//   * 状态间 cross-fade 时长本身（每个 backend 自管，在 OnEnter 回调里
//     调 backend.Play(animName, fadeIn) 即可）。
//
// 用法（典型）：
//   AnimationStateMachine fsm;
//   fsm.AddState("idle", [&] { backend.Play("idle", 0.2f); });
//   fsm.AddState("walk", [&] { backend.Play("walk", 0.2f); });
//   fsm.AddTransition("idle", "walk", [&](const auto&) { return inputAxis != 0; });
//   fsm.AddTransition("walk", "idle", [&](const auto&) { return inputAxis == 0; });
//   fsm.SetInitialState("idle");
//   fsm.Tick(dt);
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace Orange::Engine::Animation
{

// 过渡条件求值时的当前帧上下文。允许扩展但不破坏：未来加 layer
// 标识 / blend tree 句柄等字段时，旧 condition 函数仍可读取既有字段。
struct StateContext
{
    std::string_view currentStateName;  // 当前活跃状态名（静态生命周期，至少活到 Tick 返回）
    float            elapsedSeconds{0.0f};  // 进入当前状态以来累计秒数
};

class ORANGE_ENGINE_API AnimationStateMachine
{
public:
    using OnEnterFn   = std::function<void()>;
    using OnExitFn    = std::function<void()>;
    using ConditionFn = std::function<bool(const StateContext&)>;

    AnimationStateMachine();
    ~AnimationStateMachine();

    AnimationStateMachine(const AnimationStateMachine&)            = delete;
    AnimationStateMachine& operator=(const AnimationStateMachine&) = delete;

    AnimationStateMachine(AnimationStateMachine&&) noexcept;
    AnimationStateMachine& operator=(AnimationStateMachine&&) noexcept;

    // 注册一个新状态。重名 → 覆盖回调（不报错；与 MaterialSystem 的
    // AlreadyExists 拒绝节奏不同——这里 FSM 是调用方手写的、覆盖更
    // 友好）。onEnter / onExit 任一可空（默认 no-op）。
    void AddState(std::string_view name,
                  OnEnterFn        onEnter = {},
                  OnExitFn         onExit  = {});

    // 注册一条 from → to 的过渡边。同 (from, to) 多次注册会**追加**——
    // Tick 时按注册顺序逐个 evaluate condition，第一条返回 true 的 fire。
    // condition 返回 true 时 fire；先调 onExit(from)、再调 onEnter(to)、
    // elapsedSeconds 清零。
    void AddTransition(std::string_view from,
                       std::string_view to,
                       ConditionFn      condition);

    // 设置初始状态。必须在第一次 Tick 之前调用一次。设置后立即调一次
    // 该状态的 onEnter。重复调用：先 onExit 旧状态 → 再 onEnter 新状态。
    // 状态名未注册 → no-op（不抛、不崩；调用方按 CurrentState() 验证）。
    void SetInitialState(std::string_view name);

    // 推进一帧：累加 elapsedSeconds、按当前状态的所有出向 transition 顺序
    // evaluate condition；命中 → onExit 旧 / onEnter 新 / elapsed 清零。
    // 一帧内可能连发多条过渡（例如 condition A 触发后 condition B 又满足），
    // 但同一帧内本函数仅 fire **一次过渡**——避免无限循环。
    // CurrentState 未设 / 没有任何 state 注册时 Tick 是 no-op。
    void Tick(float dt);

    // 当前状态名。CurrentState 未设时返回空 string_view。
    std::string_view CurrentState() const noexcept;

    // 进入当前状态以来累计秒数。CurrentState 未设时返回 0。
    float ElapsedSeconds() const noexcept;

    // 已注册状态数 / 过渡边数（诊断 / 单测用）。
    std::size_t StateCount()      const noexcept;
    std::size_t TransitionCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine::Animation

#endif  // ORANGE_ENGINE_ANIMATION_ANIMATION_STATE_MACHINE_H
