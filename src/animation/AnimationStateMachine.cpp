// AnimationStateMachine impl ——
// flat-weighted FSM；状态名 + 进入/退出回调 + 出向 transition 列表。
// 本期不做 blend tree 也不做 layered ASM——按 design-plan Phase 4 /
// Task 01 范围。

#include "orange/engine/animation/AnimationStateMachine.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Orange::Engine::Animation
{

struct AnimationStateMachine::Impl
{
    struct State
    {
        OnEnterFn onEnter;
        OnExitFn  onExit;
    };

    struct Transition
    {
        std::string from;
        std::string to;
        ConditionFn condition;
    };

    std::unordered_map<std::string, State> states;
    std::vector<Transition>                transitions;

    std::string currentStateName;       // 空 = 未设初始
    float       currentElapsed{0.0f};

    bool HasState(std::string_view name) const
    {
        // unordered_map<string> 比较需要拷一份；hot path 不在这（仅 SetInitial / transition），不优化。
        return states.find(std::string(name)) != states.end();
    }

    void EnterState(std::string_view name)
    {
        const std::string newName{name};
        auto it = states.find(newName);
        if (it == states.end())
        {
            return;
        }
        currentStateName = newName;
        currentElapsed   = 0.0f;
        if (it->second.onEnter)
        {
            it->second.onEnter();
        }
    }

    void ExitCurrentState()
    {
        if (currentStateName.empty())
        {
            return;
        }
        auto it = states.find(currentStateName);
        if (it == states.end())
        {
            return;
        }
        if (it->second.onExit)
        {
            it->second.onExit();
        }
    }
};

AnimationStateMachine::AnimationStateMachine()
    : mpImpl(std::make_unique<Impl>())
{
}

AnimationStateMachine::~AnimationStateMachine() = default;

AnimationStateMachine::AnimationStateMachine(AnimationStateMachine&&) noexcept            = default;
AnimationStateMachine& AnimationStateMachine::operator=(AnimationStateMachine&&) noexcept = default;

void AnimationStateMachine::AddState(std::string_view name,
                                     OnEnterFn        onEnter,
                                     OnExitFn         onExit)
{
    if (!mpImpl)
    {
        return;
    }
    Impl::State s;
    s.onEnter = std::move(onEnter);
    s.onExit  = std::move(onExit);
    mpImpl->states[std::string(name)] = std::move(s);  // 重名覆盖，与文档约定一致
}

void AnimationStateMachine::AddTransition(std::string_view from,
                                          std::string_view to,
                                          ConditionFn      condition)
{
    if (!mpImpl || !condition)
    {
        return;  // 空 condition 永远 false 等价于不加；直接拒绝避免后续 Tick 解引用空 std::function
    }
    Impl::Transition t;
    t.from      = std::string(from);
    t.to        = std::string(to);
    t.condition = std::move(condition);
    mpImpl->transitions.push_back(std::move(t));
}

void AnimationStateMachine::SetInitialState(std::string_view name)
{
    if (!mpImpl || !mpImpl->HasState(name))
    {
        return;
    }
    mpImpl->ExitCurrentState();
    mpImpl->EnterState(name);
}

void AnimationStateMachine::Tick(float dt)
{
    if (!mpImpl || mpImpl->currentStateName.empty())
    {
        return;
    }
    mpImpl->currentElapsed += dt;

    // 当前 state 的所有出向 transition 顺序求值；命中第一条即 fire 并返回，
    // 同帧不连发——避免 condition 一直 true 时无限递归切状态。
    const StateContext ctx{
        mpImpl->currentStateName,
        mpImpl->currentElapsed,
    };
    for (const auto& t : mpImpl->transitions)
    {
        if (t.from != mpImpl->currentStateName)
        {
            continue;
        }
        if (t.condition(ctx))
        {
            mpImpl->ExitCurrentState();
            mpImpl->EnterState(t.to);
            return;
        }
    }
}

std::string_view AnimationStateMachine::CurrentState() const noexcept
{
    if (!mpImpl)
    {
        return {};
    }
    return mpImpl->currentStateName;
}

float AnimationStateMachine::ElapsedSeconds() const noexcept
{
    if (!mpImpl)
    {
        return 0.0f;
    }
    return mpImpl->currentElapsed;
}

std::size_t AnimationStateMachine::StateCount() const noexcept
{
    return mpImpl ? mpImpl->states.size() : 0;
}

std::size_t AnimationStateMachine::TransitionCount() const noexcept
{
    return mpImpl ? mpImpl->transitions.size() : 0;
}

}  // namespace Orange::Engine::Animation
