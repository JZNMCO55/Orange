#ifndef ORANGE_ENGINE_FSM_STATE_MACHINE_H
#define ORANGE_ENGINE_FSM_STATE_MACHINE_H

// ---------------------------------------------------------------------------
// StateMachine —— 通用逻辑状态机 (gameplay / AI finite state machine)。可复用的
// 行为原语：状态 (state) + 转移 (transition) + 生命周期回调 (onEnter/onUpdate/
// onExit)，服务 enemy patrol/chase/attack、UI 流程、关卡阶段等一切"有限状态 +
// 事件 / 条件驱动跳转"的玩法逻辑。
//
// 与 animation/AnimationStateMachine 的区别：那是动画剪辑专用（state 绑 clip、
// 按归一化时长做 crossfade）；本模块是**通用逻辑** FSM，state / event 只是用户
// 自定义 int（把自己的 enum cast 成 int 即可），与动画 / 渲染 / 资产全解耦，纯
// 逻辑 headless 完全可验。
//
// 转移两类：事件触发 (event-driven，Fire 一个 EventId 触发) + 自动 (guard 自动，
// 每次 Update 检查 guard 谓词)。转移执行时按 onExit(current) → 切态 → onEnter(to)
// 的确定顺序调回调。
//
// 抗再入 (re-entrant safe)：转移回调（onEnter/onExit）里可以再 Fire 事件——靠
// mInTransition 标志 + mPending 队列做**链式转移**，由顶层 drain 循环处理，不
// 递归、不重入 DoTransition。故"onEnter(B) 里 Fire 使 B→C 的事件"会在 onEnter(B)
// 完整跑完后才继续，最终落到 C，不会栈爆。
//
// 线程不安全：为单线程游戏循环设计，不做任何锁。公共头只依赖标准库（纯逻辑，
// 不引 glm）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstddef>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Orange::Engine::Fsm
{

// 状态 / 事件标识：用户可把自己的 enum cast 成 int。int 本身无语义，只作 key。
using StateId = int;
using EventId = int;

// 状态生命周期回调 (state lifecycle callbacks)，均可空 (empty 即 no-op)。
struct StateCallbacks
{
    std::function<void()>      onEnter;   // 进入该状态时调用一次
    std::function<void(float)> onUpdate;  // 处于该状态时每次 StateMachine::Update(dt) 调用
    std::function<void()>      onExit;    // 离开该状态时调用一次
};

// 通用逻辑状态机 (gameplay / AI)：状态 + 转移（事件触发 / guard 自动）+ 生命周期
// 回调。单线程用。转移回调里可再 Fire 事件——靠 pending 队列做链式转移、抗再入
// （不递归不重入）。
class ORANGE_ENGINE_API StateMachine
{
public:
    // 未运行 / 已停机时 Current() 返回的 sentinel。取 INT_MIN，避免与任何合理的
    // 用户 state id 相撞（用户几乎不会拿 INT_MIN 当状态号）。
    static constexpr StateId kNoState = -2147483647 - 1;  // INT_MIN

    StateMachine() = default;

    // 不可拷贝 / 移动（回调可能捕获外部状态，单例式服务；同 EventBus / TweenManager）：
    // 值语义搬运会让"哪个 machine 拥有这批回调 / 转移"语义模糊，且转移回调捕获的
    // this / 外部对象一旦被拷贝就可能悬垂 (dangling)。用引用 / 指针 / unique_ptr 持有。
    StateMachine(const StateMachine&)            = delete;
    StateMachine& operator=(const StateMachine&) = delete;
    StateMachine(StateMachine&&)                 = delete;
    StateMachine& operator=(StateMachine&&)      = delete;

    // 注册状态（回调可空）。重复注册同 id 覆盖其回调。
    void AddState(StateId id, StateCallbacks callbacks = {});

    // 事件触发转移：处于 from 且收到 event 且 guard()（若有）为真 → 转 to。同一
    // (from,event) 可加多条，按添加顺序取第一条 guard 通过的。
    void AddTransition(StateId from, EventId event, StateId to, std::function<bool()> guard = {});

    // 自动转移：处于 from 时每次 Update 检查 guard()，为真 → 转 to（按添加顺序第一条
    // 通过的）。每次 Update 至多触发一次自动转移，避免同帧无限跳。
    void AddAutoTransition(StateId from, std::function<bool()> guard, StateId to);

    void Start(StateId initial);  // 设初态 + 调其 onEnter（已运行则先 Stop 再起）
    void Stop();                  // 调当前 onExit + 停机（未运行 no-op）
    bool Fire(EventId event);     // 处理事件触发转移；返回是否发生（至少一次）转移
    void Update(float dt);        // 调当前 onUpdate(dt) + 检查自动转移

    StateId Current() const;         // 当前状态（未运行返回 kNoState）
    bool    IsRunning() const;
    bool    IsIn(StateId id) const;  // IsRunning() && Current()==id

private:
    struct EventTransition
    {
        StateId               from;
        EventId               event;
        StateId               to;
        std::function<bool()> guard;
    };

    struct AutoTransition
    {
        StateId               from;
        std::function<bool()> guard;
        StateId               to;
    };

    // 各生命周期回调的安全触发：未注册状态 = 无回调，静默 no-op。
    void CallEnter(StateId id);
    void CallExit(StateId id);
    void CallUpdate(StateId id, float dt);

    // 执行到 to 的转移：onExit(current) → current=to → onEnter(to)。回调里若 Fire
    // 事件，因 mInTransition==true 会进 mPending，由顶层 drain 循环链式处理（不重入
    // DoTransition）。本函数自身不设 / 清 mInTransition —— 由调用方（TriggerTransition）
    // 包裹，以便 drain 循环里的每次 DoTransition 共享同一个"转移进行中"窗口。
    void DoTransition(StateId to);

    // 用 mInTransition 包裹 DoTransition(to) 再 drain mPending：转移回调里 Fire 的
    // 事件全进 mPending，由 drain 逐个 ProcessEvent（其 DoTransition 回调若再 Fire
    // 仍入 mPending，被同一 drain 继续处理 → 链式转移不递归不重入）。自动转移路径用。
    void TriggerTransition(StateId to);

    // 排空 mPending：逐个取出转移回调里 Fire 的事件并 ProcessEvent。假定调用方已置
    // mInTransition==true（故 drain 期 DoTransition 回调再 Fire 仍入队被本循环续处理）。
    // 返回 drain 期是否发生过转移。Fire 与 TriggerTransition 共用。
    bool DrainPending();

    // 解析并执行 event 触发的一次转移（找第一条匹配 from + event + guard 通过的），
    // 返回是否转移。命中即调 DoTransition（假定调用方已置 mInTransition）。
    bool ProcessEvent(EventId event);

    std::unordered_map<StateId, StateCallbacks> mStates;
    std::vector<EventTransition>                mEventTransitions;
    std::vector<AutoTransition>                 mAutoTransitions;
    StateId                                     mCurrent{kNoState};
    bool                                        mRunning{false};
    bool                                        mInTransition{false};  // Fire/drain 抗再入标志
    bool                                        mInCallback{false};    // 是否正在执行生命周期回调（Stop 据此避免重入 CallExit）
    std::vector<EventId>                        mPending;              // 转移回调里 Fire 的事件队列
};

}  // namespace Orange::Engine::Fsm

#endif  // ORANGE_ENGINE_FSM_STATE_MACHINE_H
