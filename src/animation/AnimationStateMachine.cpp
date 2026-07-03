// AnimationStateMachine impl ——
// flat-weighted FSM；状态名 + 进入/退出回调 + 出向 transition 列表
// + parameter table（v0.7 c2-7 / ADR-005）。本期不做 blend tree 也不做
// layered ASM。

#include "orange/engine/animation/AnimationStateMachine.h"

#include "orange/engine/core/Log.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Orange::Engine::Animation
{

    namespace
    {

        // 把 variant<bool,int32,float> widening 到 float 做数值比较 —— Greater/
        // Less/Equal 等比较运算 op 走这条路径；Bool/Trigger 走 false=0/true=1。
        float AsFloat(const std::variant<bool, std::int32_t, float>& v) noexcept
        {
            return std::visit(
                [](auto x) -> float
                {
                    using T = std::decay_t<decltype(x)>;
                    if constexpr (std::is_same_v<T, bool>)
                    {
                        return x ? 1.0f : 0.0f;
                    }
                    else if constexpr (std::is_same_v<T, std::int32_t>)
                    {
                        return static_cast<float>(x);
                    }
                    else
                    {
                        return x;
                    }
                },
                v);
        }

        // 把 variant<...> 当 bool 读 —— If/IfNot 走这条路径；Int/Float 走
        // 非零判定（与 Unity Animator Bool/Trigger 同款）。
        bool AsBool(const std::variant<bool, std::int32_t, float>& v) noexcept
        {
            return std::visit(
                [](auto x) -> bool
                {
                    using T = std::decay_t<decltype(x)>;
                    if constexpr (std::is_same_v<T, bool>)
                    {
                        return x;
                    }
                    else if constexpr (std::is_same_v<T, std::int32_t>)
                    {
                        return x != 0;
                    }
                    else
                    {
                        return x != 0.0f;
                    }
                },
                v);
        }

        // EvaluateExpr —— 单条 ConditionExpr 对 Parameter 的求值。
        bool EvaluateExpr(const ConditionExpr& expr, const Parameter& param) noexcept
        {
            switch (expr.op)
            {
                case ConditionOp::If:
                    return AsBool(param.value);
                case ConditionOp::IfNot:
                    return !AsBool(param.value);
                case ConditionOp::Greater:
                    return AsFloat(param.value) > AsFloat(expr.threshold);
                case ConditionOp::Less:
                    return AsFloat(param.value) < AsFloat(expr.threshold);
                case ConditionOp::Equal:
                    return AsFloat(param.value) == AsFloat(expr.threshold);
                case ConditionOp::NotEqual:
                    return AsFloat(param.value) != AsFloat(expr.threshold);
                case ConditionOp::GreaterEqual:
                    return AsFloat(param.value) >= AsFloat(expr.threshold);
                case ConditionOp::LessEqual:
                    return AsFloat(param.value) <= AsFloat(expr.threshold);
            }
            return false;
        }

    } // anonymous namespace

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

        std::unordered_map<std::string, State>     states;
        std::unordered_map<std::string, Parameter> parameters;
        std::vector<Transition>                    transitions;

        std::string currentStateName; // 空 = 未设初始
        float       currentElapsed{0.0f};

        bool HasState(std::string_view name) const
        {
            return states.find(std::string(name)) != states.end();
        }

        void EnterState(std::string_view name)
        {
            const std::string newName{name};
            auto              it = states.find(newName);
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

        // Tick fire 一条 transition 后调用——把所有 Trigger 类型参数 reset 为
        // false（与 Unity Animator Trigger 语义一致）。
        void ResetAllTriggers()
        {
            for (auto& [name, p] : parameters)
            {
                if (p.type == ParameterType::Trigger)
                {
                    p.value = false;
                }
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
        s.onEnter                         = std::move(onEnter);
        s.onExit                          = std::move(onExit);
        mpImpl->states[std::string(name)] = std::move(s); // 重名覆盖，与文档约定一致
    }

    void AnimationStateMachine::AddTransition(std::string_view from,
                                              std::string_view to,
                                              ConditionFn      condition)
    {
        if (!mpImpl || !condition)
        {
            return; // 空 condition 永远 false 等价于不加；直接拒绝避免后续 Tick 解引用空 std::function
        }
        Impl::Transition t;
        t.from      = std::string(from);
        t.to        = std::string(to);
        t.condition = std::move(condition);
        mpImpl->transitions.push_back(std::move(t));
    }

    void AnimationStateMachine::AddTransition(std::string_view           from,
                                              std::string_view           to,
                                              std::vector<ConditionExpr> conditions)
    {
        if (!mpImpl)
        {
            return;
        }
        // 把 vector<ConditionExpr> 包装成 ConditionFn lambda（捕获 conditions
        // 副本），存入同一 transitions 容器。两条 API 路径在 Tick 内合一。
        Impl::Transition t;
        t.from      = std::string(from);
        t.to        = std::string(to);
        t.condition = [conds = std::move(conditions)](const StateContext& ctx) -> bool
        {
            if (conds.empty())
            {
                return true; // 空 list = 无条件 transition
            }
            if (ctx.parameters == nullptr)
            {
                return false; // 数据驱动 condition 需要 parameter table；缺即 no-fire
            }
            for (const auto& c : conds)
            {
                auto it = ctx.parameters->find(c.paramName);
                if (it == ctx.parameters->end())
                {
                    return false; // 引用未注册参数 → 不 fire
                }
                if (!EvaluateExpr(c, it->second))
                {
                    return false; // AND 组合：一条 false 即整体 false
                }
            }
            return true;
        };
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

        const StateContext ctx{
            mpImpl->currentStateName,
            mpImpl->currentElapsed,
            &mpImpl->parameters,
        };
        // index 迭代 + 拷出本迭代字段：transition 的 condition 是用户回调，若回调
        // 再入 AddTransition 会让 transitions 扩容重分配，使持有的元素引用悬垂——
        // 之后读 t.to（EnterState 参数）或执行期访问 std::function 内部状态即 UAF。
        // 拷 toState（迭代后跳转用）与 condition（执行期 vector 可能 realloc 移动
        // 该 std::function 自身）到局部，与容器解耦后再求值。
        for (std::size_t i = 0; i < mpImpl->transitions.size(); ++i)
        {
            if (mpImpl->transitions[i].from != mpImpl->currentStateName)
            {
                continue;
            }
            const std::string toState   = mpImpl->transitions[i].to;
            const auto        condition = mpImpl->transitions[i].condition;
            if (condition && condition(ctx))
            {
                mpImpl->ExitCurrentState();
                mpImpl->EnterState(toState);
                mpImpl->ResetAllTriggers();
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

    // ---------------------------------------------------------------------------
    // Parameter table API（v0.7 c2-7 / ADR-005）
    // ---------------------------------------------------------------------------

    namespace
    {

        // 按 type 给 Parameter.value 设零初值
        void InitParameterValueByType(Parameter& p, ParameterType type)
        {
            p.type = type;
            switch (type)
            {
                case ParameterType::Bool:
                case ParameterType::Trigger:
                    p.value = false;
                    break;
                case ParameterType::Int:
                    p.value = std::int32_t{0};
                    break;
                case ParameterType::Float:
                    p.value = 0.0f;
                    break;
            }
        }

    } // anonymous namespace

    void AnimationStateMachine::RegisterParameter(std::string_view name, ParameterType type)
    {
        if (!mpImpl)
        {
            return;
        }
        Parameter p;
        InitParameterValueByType(p, type);
        mpImpl->parameters[std::string(name)] = std::move(p); // 重名覆盖（重置 value）
    }

    void AnimationStateMachine::SetParameterBool(std::string_view name, bool value)
    {
        if (!mpImpl)
        {
            return;
        }
        auto it = mpImpl->parameters.find(std::string(name));
        if (it == mpImpl->parameters.end())
        {
            ORANGE_LOG_WARN("[AnimationStateMachine] SetParameterBool '{}' 未注册", name);
            return;
        }
        // Bool / Trigger 接受 bool；Int / Float 接受 widening
        switch (it->second.type)
        {
            case ParameterType::Bool:
            case ParameterType::Trigger:
                it->second.value = value;
                break;
            case ParameterType::Int:
                it->second.value = static_cast<std::int32_t>(value ? 1 : 0);
                break;
            case ParameterType::Float:
                it->second.value = value ? 1.0f : 0.0f;
                break;
        }
    }

    void AnimationStateMachine::SetParameterInt(std::string_view name, std::int32_t value)
    {
        if (!mpImpl)
        {
            return;
        }
        auto it = mpImpl->parameters.find(std::string(name));
        if (it == mpImpl->parameters.end())
        {
            ORANGE_LOG_WARN("[AnimationStateMachine] SetParameterInt '{}' 未注册", name);
            return;
        }
        switch (it->second.type)
        {
            case ParameterType::Bool:
            case ParameterType::Trigger:
                it->second.value = (value != 0);
                break;
            case ParameterType::Int:
                it->second.value = value;
                break;
            case ParameterType::Float:
                it->second.value = static_cast<float>(value);
                break;
        }
    }

    void AnimationStateMachine::SetParameterFloat(std::string_view name, float value)
    {
        if (!mpImpl)
        {
            return;
        }
        auto it = mpImpl->parameters.find(std::string(name));
        if (it == mpImpl->parameters.end())
        {
            ORANGE_LOG_WARN("[AnimationStateMachine] SetParameterFloat '{}' 未注册", name);
            return;
        }
        switch (it->second.type)
        {
            case ParameterType::Bool:
            case ParameterType::Trigger:
                // float → bool widening 易出错（0.0/NaN 都 false）—— warn 但不阻塞
                ORANGE_LOG_WARN("[AnimationStateMachine] SetParameterFloat 写入 Bool/Trigger 参数 "
                                "'{}'：按非零判定",
                                name);
                it->second.value = (value != 0.0f);
                break;
            case ParameterType::Int:
                it->second.value = static_cast<std::int32_t>(value);
                break;
            case ParameterType::Float:
                it->second.value = value;
                break;
        }
    }

    void AnimationStateMachine::SetTrigger(std::string_view name)
    {
        if (!mpImpl)
        {
            return;
        }
        auto it = mpImpl->parameters.find(std::string(name));
        if (it == mpImpl->parameters.end())
        {
            ORANGE_LOG_WARN("[AnimationStateMachine] SetTrigger '{}' 未注册", name);
            return;
        }
        // Trigger / Bool 都设 true；其它类型按 SetParameterBool(true) 的 widening
        SetParameterBool(name, true);
    }

    bool AnimationStateMachine::GetParameterBool(std::string_view name) const noexcept
    {
        if (!mpImpl)
        {
            return false;
        }
        auto it = mpImpl->parameters.find(std::string(name));
        if (it == mpImpl->parameters.end())
        {
            return false;
        }
        return AsBool(it->second.value);
    }

    std::int32_t AnimationStateMachine::GetParameterInt(std::string_view name) const noexcept
    {
        if (!mpImpl)
        {
            return 0;
        }
        auto it = mpImpl->parameters.find(std::string(name));
        if (it == mpImpl->parameters.end())
        {
            return 0;
        }
        return static_cast<std::int32_t>(AsFloat(it->second.value));
    }

    float AnimationStateMachine::GetParameterFloat(std::string_view name) const noexcept
    {
        if (!mpImpl)
        {
            return 0.0f;
        }
        auto it = mpImpl->parameters.find(std::string(name));
        if (it == mpImpl->parameters.end())
        {
            return 0.0f;
        }
        return AsFloat(it->second.value);
    }

    std::size_t AnimationStateMachine::ParameterCount() const noexcept
    {
        return mpImpl ? mpImpl->parameters.size() : 0;
    }

} // namespace Orange::Engine::Animation
