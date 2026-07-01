#include <orange/engine/fsm/StateMachine.h>

#include <utility>  // std::move

namespace Orange::Engine::Fsm
{

void StateMachine::AddState(StateId id, StateCallbacks callbacks)
{
    // 重复注册同 id 覆盖其回调（operator[] 命中即替换、未命中即插入）。
    mStates[id] = std::move(callbacks);
}

void StateMachine::AddTransition(StateId from, EventId event, StateId to, std::function<bool()> guard)
{
    // 同一 (from,event) 可加多条：按 push_back 顺序存，ProcessEvent 取第一条 guard 通过的。
    mEventTransitions.push_back(EventTransition{from, event, to, std::move(guard)});
}

void StateMachine::AddAutoTransition(StateId from, std::function<bool()> guard, StateId to)
{
    mAutoTransitions.push_back(AutoTransition{from, std::move(guard), to});
}

void StateMachine::CallEnter(StateId id)
{
    const auto it = mStates.find(id);
    if (it == mStates.end() || !it->second.onEnter)
    {
        return;  // 未注册状态 / 无 onEnter = no-op
    }
    // 拷贝出回调再调用：onEnter 里若 AddState 触发 mStates rehash，正执行的 std::function
    // 若还存活在容器里会被销毁 → UAF。先拷贝隔离生命周期（同 Tween / EventBus 快照精神）。
    // mInCallback 置位（save/restore 支持嵌套）：回调里若调 Stop() 据此避免重入 CallExit。
    const std::function<void()> cb            = it->second.onEnter;
    const bool                  prevInCallback = mInCallback;
    mInCallback                                = true;
    cb();
    mInCallback = prevInCallback;
}

void StateMachine::CallExit(StateId id)
{
    const auto it = mStates.find(id);
    if (it == mStates.end() || !it->second.onExit)
    {
        return;
    }
    const std::function<void()> cb            = it->second.onExit;
    const bool                  prevInCallback = mInCallback;
    mInCallback                                = true;
    cb();
    mInCallback = prevInCallback;
}

void StateMachine::CallUpdate(StateId id, float dt)
{
    const auto it = mStates.find(id);
    if (it == mStates.end() || !it->second.onUpdate)
    {
        return;
    }
    const std::function<void(float)> cb            = it->second.onUpdate;
    const bool                       prevInCallback = mInCallback;
    mInCallback                                     = true;
    cb(dt);
    mInCallback = prevInCallback;
}

void StateMachine::DoTransition(StateId to)
{
    // 确定顺序：先 onExit(旧态) → 切 mCurrent → onEnter(新态)。回调里若 Fire 事件，因
    // 调用方已置 mInTransition==true 会进 mPending，由顶层 drain 链式处理（不重入本函数）。
    CallExit(mCurrent);
    // onExit 回调里若调 Stop()（mRunning=false）→ 中止本次转移：不切态、不 onEnter，
    // 避免遗留 mRunning==false 却 mCurrent==to 的内部不一致。
    if (!mRunning)
    {
        return;
    }
    mCurrent = to;
    CallEnter(to);
}

bool StateMachine::ProcessEvent(EventId event)
{
    // 找第一条 from==当前态 && event 匹配 && guard（若有）通过 的转移执行。
    // **索引式遍历 + 调 guard 前把 to / guard 拷进局部**：guard 谓词若有副作用调用
    // AddTransition 触发 mEventTransitions realloc，则正执行的 guard std::function 与 t.to
    // 都在旧（已释放）存储上 → UAF；拷贝出来后调用与后续 DoTransition 都用局部副本，安全
    // （镜像 CallEnter/Exit 拷贝 std::function 的抗再入手法）。每轮重取 size 与 [i]，不持
    // 跨 guard() 的引用。
    for (std::size_t i = 0; i < mEventTransitions.size(); ++i)
    {
        if (mEventTransitions[i].from != mCurrent || mEventTransitions[i].event != event)
        {
            continue;
        }
        const StateId               to    = mEventTransitions[i].to;
        const std::function<bool()> guard = mEventTransitions[i].guard;
        if (!guard || guard())
        {
            DoTransition(to);
            return true;
        }
    }
    return false;
}

bool StateMachine::DrainPending()
{
    bool any = false;
    // 从队首逐个取出（FIFO）。ProcessEvent 内 DoTransition 的回调若再 Fire，因 mInTransition
    // 仍为 true 会继续入 mPending，被本 while 后续迭代处理 → 链式转移；队列排空即止。
    while (!mPending.empty())
    {
        const EventId e = mPending.front();
        mPending.erase(mPending.begin());
        if (ProcessEvent(e))
        {
            any = true;
        }
    }
    return any;
}

void StateMachine::TriggerTransition(StateId to)
{
    mInTransition = true;
    DoTransition(to);
    DrainPending();
    mInTransition = false;
}

void StateMachine::Start(StateId initial)
{
    if (mRunning)
    {
        Stop();  // 已运行则先停旧态（调旧态 onExit），再起新态
    }
    mCurrent = initial;
    mRunning = true;
    CallEnter(initial);
}

void StateMachine::Stop()
{
    if (!mRunning)
    {
        return;  // 未运行 no-op（也让回调里的嵌套 Stop() 早返回，杜绝无限递归）
    }
    // **先标停再 CallExit**：否则 onExit 回调里调 Stop() 时 mRunning 仍 true 会再次 CallExit
    // → 无限递归栈溢出。先置 mRunning=false 使嵌套 Stop() 命中上面的 no-op。
    const StateId prev = mCurrent;
    mRunning           = false;
    mCurrent           = kNoState;
    mPending.clear();  // 停机丢弃未处理的链式事件（避免残留跨越下一次 Start）

    // 仅在**非回调上下文**（顶层 Stop）里补调 onExit：若正处于某生命周期回调内（onExit/
    // onEnter/onUpdate 里调的 Stop），该状态的退出由外层转移逻辑负责，此处再 CallExit 会
    // 重复触发 onExit / 在正执行的回调上重入。
    if (!mInCallback)
    {
        CallExit(prev);
    }
}

bool StateMachine::Fire(EventId event)
{
    if (!mRunning)
    {
        return false;  // 未运行 no-op
    }
    if (mInTransition)
    {
        // 处于转移回调中（onEnter/onExit 里 Fire）→ 入队，由顶层 drain 链式处理，不重入。
        mPending.push_back(event);
        return true;
    }
    // 顶层 Fire：整段（初始事件 + 后续链式 drain）共享同一个 mInTransition 窗口，故转移
    // 回调里的 Fire 全走上面的入队分支，DoTransition 绝不递归重入。
    mInTransition = true;
    bool any      = ProcessEvent(event);
    if (DrainPending())
    {
        any = true;
    }
    mInTransition = false;
    return any;
}

void StateMachine::Update(float dt)
{
    if (!mRunning)
    {
        return;  // 未运行 no-op
    }
    // 先跑当前态 onUpdate（其内若 Fire 事件，因此刻 mInTransition==false 走正常 Fire 即时
    // 处理 + drain，可能已改 mCurrent）。onUpdate 里若 Stop 则不再检查自动转移。
    CallUpdate(mCurrent, dt);
    if (!mRunning)
    {
        return;
    }
    // 自动转移：基于（onUpdate 可能已更新的）最新 mCurrent，找第一条 guard 通过的执行。
    // 每次 Update 至多触发一次自动转移（命中即 break），避免同帧无限跳；drain 只处理回调
    // Fire 的事件、不重复扫 auto。**索引式 + 调 guard 前拷贝 to/guard**：guard 副作用调
    // AddAutoTransition 触发 realloc 时不 UAF（同 ProcessEvent）。
    for (std::size_t i = 0; i < mAutoTransitions.size(); ++i)
    {
        if (mAutoTransitions[i].from != mCurrent)
        {
            continue;
        }
        const StateId               to    = mAutoTransitions[i].to;
        const std::function<bool()> guard = mAutoTransitions[i].guard;
        if (guard && guard())
        {
            TriggerTransition(to);
            break;
        }
    }
}

StateId StateMachine::Current() const
{
    return mRunning ? mCurrent : kNoState;
}

bool StateMachine::IsRunning() const
{
    return mRunning;
}

bool StateMachine::IsIn(StateId id) const
{
    return mRunning && mCurrent == id;
}

}  // namespace Orange::Engine::Fsm
