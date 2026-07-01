// StateMachine（通用逻辑 FSM）headless 单元测试。裸 main() + <cassert>，
// 进程 exit code 0 = 通过。覆盖：事件转移 + 回调序、guard、自动转移、onUpdate
// 触发 Fire、抗再入链式转移、Start/Stop、IsIn/IsRunning/Current、未注册状态安全、
// 编译期不可拷贝/移动断言。

#include <orange/engine/fsm/StateMachine.h>

#include <cassert>
#include <string>
#include <type_traits>
#include <vector>

using Orange::Engine::Fsm::StateCallbacks;
using Orange::Engine::Fsm::StateId;
using Orange::Engine::Fsm::StateMachine;

namespace
{

// 用 int 常量当 state / event id（用户把自己的 enum cast 成 int 的典型用法）。
enum States : StateId
{
    SIdle = 0,
    SRun  = 1,
    SJump = 2,
};

enum Events
{
    EGo   = 100,
    EStop = 101,
    EJmp  = 102,
};

// 回调调用序记录器（全局，便于 lambda 捕获引用）。
std::vector<std::string> gTrace;

// 装配一个记录 enter/exit/update 的回调组。
StateCallbacks TracingCallbacks(const std::string& name)
{
    StateCallbacks cb;
    cb.onEnter  = [name]() { gTrace.push_back("enter" + name); };
    cb.onExit   = [name]() { gTrace.push_back("exit" + name); };
    cb.onUpdate = [name](float) { gTrace.push_back("update" + name); };
    return cb;
}

// ---------------------------------------------------------------------------
// 1. 基本事件转移 + 回调序
// ---------------------------------------------------------------------------
void TestBasicEventTransitionsAndCallbackOrder()
{
    gTrace.clear();
    StateMachine sm;
    sm.AddState(SIdle, TracingCallbacks("A"));
    sm.AddState(SRun, TracingCallbacks("B"));
    sm.AddState(SJump, TracingCallbacks("C"));
    sm.AddTransition(SIdle, EGo, SRun);
    sm.AddTransition(SRun, EGo, SJump);

    sm.Start(SIdle);
    assert(sm.IsRunning());
    assert(sm.Current() == SIdle);
    assert(gTrace.size() == 1 && gTrace[0] == "enterA");  // Start 调 onEnter(A)

    // Fire(EGo)：onExit(A) 必须先于 onEnter(B)，落到 B。
    const bool moved = sm.Fire(EGo);
    assert(moved);
    assert(sm.Current() == SRun);
    assert(gTrace.size() == 3);
    assert(gTrace[1] == "exitA");
    assert(gTrace[2] == "enterB");

    // 再 Fire 到 C。
    const bool moved2 = sm.Fire(EGo);
    assert(moved2);
    assert(sm.Current() == SJump);
    assert(gTrace.size() == 5);
    assert(gTrace[3] == "exitB");
    assert(gTrace[4] == "enterC");

    // Fire 未知事件（C 无 EGo 转移）返回 false、状态不变、无回调。
    const bool moved3 = sm.Fire(EStop);
    assert(!moved3);
    assert(sm.Current() == SJump);
    assert(gTrace.size() == 5);
}

// ---------------------------------------------------------------------------
// 2. guard：为假不转移；为真转移；多条按顺序取第一条通过的
// ---------------------------------------------------------------------------
void TestGuardedTransitions()
{
    StateMachine sm;
    sm.AddState(SIdle);
    sm.AddState(SRun);
    sm.AddState(SJump);

    bool gateOpen = false;
    sm.AddTransition(SIdle, EGo, SRun, [&]() { return gateOpen; });

    sm.Start(SIdle);
    // guard 为 false → 不转移。
    assert(!sm.Fire(EGo));
    assert(sm.Current() == SIdle);
    // guard 为 true → 转移。
    gateOpen = true;
    assert(sm.Fire(EGo));
    assert(sm.Current() == SRun);

    // 同 (from,event) 多条：按添加顺序取第一条 guard 通过的。
    StateMachine sm2;
    sm2.AddState(SIdle);
    sm2.AddState(SRun);
    sm2.AddState(SJump);
    bool first  = false;
    bool second = true;
    sm2.AddTransition(SIdle, EGo, SRun, [&]() { return first; });   // 第一条：guard=false
    sm2.AddTransition(SIdle, EGo, SJump, [&]() { return second; });  // 第二条：guard=true
    sm2.Start(SIdle);
    assert(sm2.Fire(EGo));
    assert(sm2.Current() == SJump);  // 第一条不过 → 落第二条

    // 若第一条也过，取第一条。
    StateMachine sm3;
    sm3.AddState(SIdle);
    sm3.AddState(SRun);
    sm3.AddState(SJump);
    sm3.AddTransition(SIdle, EGo, SRun, [&]() { return true; });
    sm3.AddTransition(SIdle, EGo, SJump, [&]() { return true; });
    sm3.Start(SIdle);
    assert(sm3.Fire(EGo));
    assert(sm3.Current() == SRun);  // 第一条先命中
}

// ---------------------------------------------------------------------------
// 3. 自动转移：guard 假不转移、真转移（含 onExit/onEnter）；onUpdate 每次被调
// ---------------------------------------------------------------------------
void TestAutoTransitions()
{
    gTrace.clear();
    StateMachine sm;
    sm.AddState(SIdle, TracingCallbacks("A"));
    sm.AddState(SRun, TracingCallbacks("B"));

    bool trigger = false;
    sm.AddAutoTransition(SIdle, [&]() { return trigger; }, SRun);

    sm.Start(SIdle);
    assert(gTrace.size() == 1 && gTrace[0] == "enterA");

    // guard 为 false → Update 停在 A，但 onUpdate(A) 被调。
    sm.Update(0.016f);
    assert(sm.Current() == SIdle);
    assert(gTrace.size() == 2 && gTrace[1] == "updateA");

    // 再 Update 一次 → 仍停 A、再调一次 onUpdate(A)（每次 Update 被调）。
    sm.Update(0.016f);
    assert(sm.Current() == SIdle);
    assert(gTrace.size() == 3 && gTrace[2] == "updateA");

    // guard 为 true → 本次 Update 触发自动转移到 B（onUpdate(A) → exitA → enterB）。
    trigger = true;
    sm.Update(0.016f);
    assert(sm.Current() == SRun);
    assert(gTrace.size() == 6);
    assert(gTrace[3] == "updateA");  // onUpdate 先跑（基于旧态 A）
    assert(gTrace[4] == "exitA");
    assert(gTrace[5] == "enterB");

    // 到 B 后 B 无自动转移 → Update 只调 onUpdate(B)。
    sm.Update(0.016f);
    assert(sm.Current() == SRun);
    assert(gTrace.size() == 7 && gTrace[6] == "updateB");
}

// ---------------------------------------------------------------------------
// 4. onUpdate 里 Fire 事件 → 正常转移
// ---------------------------------------------------------------------------
void TestFireFromOnUpdate()
{
    gTrace.clear();
    StateMachine sm;

    StateCallbacks a = TracingCallbacks("A");
    // A 的 onUpdate 里 Fire(EGo) —— 用指针在 lambda 里持有 sm。
    StateMachine* pSm = &sm;
    a.onUpdate        = [pSm](float) {
        gTrace.push_back("updateA");
        pSm->Fire(EGo);
    };
    sm.AddState(SIdle, a);
    sm.AddState(SRun, TracingCallbacks("B"));
    sm.AddTransition(SIdle, EGo, SRun);

    sm.Start(SIdle);
    gTrace.clear();  // 丢弃 Start 的 enterA，聚焦 Update 行为
    sm.Update(0.016f);  // onUpdate(A) 里 Fire(EGo) → 转到 B
    assert(sm.Current() == SRun);
    // 序：updateA（含内部 Fire 触发的 exitA → enterB）。
    assert(gTrace.size() == 3);
    assert(gTrace[0] == "updateA");
    assert(gTrace[1] == "exitA");
    assert(gTrace[2] == "enterB");
}

// ---------------------------------------------------------------------------
// 5. 抗再入链式：onEnter(B) 里 Fire 使 B→C → 最终落 C，回调序正确（非递归）
// ---------------------------------------------------------------------------
void TestReentrantChainedTransition()
{
    gTrace.clear();
    StateMachine sm;
    StateMachine* pSm = &sm;

    sm.AddState(SIdle, TracingCallbacks("A"));

    // B 的 onEnter 里 Fire(EJmp)（使 B→C）。关键：onEnter(B) 必须完整跑完
    // （push enterB）后才轮到 exitB → enterC，不能递归中断。
    StateCallbacks b;
    b.onEnter = [pSm]() {
        gTrace.push_back("enterB-begin");
        pSm->Fire(EJmp);              // 转移进行中 → 入 mPending，不递归
        gTrace.push_back("enterB-end");  // 必须在 exitB 之前
    };
    b.onExit = []() { gTrace.push_back("exitB"); };
    sm.AddState(SRun, b);
    sm.AddState(SJump, TracingCallbacks("C"));

    sm.AddTransition(SIdle, EGo, SRun);
    sm.AddTransition(SRun, EJmp, SJump);

    sm.Start(SIdle);
    gTrace.clear();  // 丢弃 Start 的 enterA，聚焦 Fire 链式转移的回调序
    const bool moved = sm.Fire(EGo);  // A→B，onEnter(B) 链式触发 B→C
    assert(moved);
    assert(sm.Current() == SJump);  // 最终落 C

    // 期望序：exitA, enterB-begin, enterB-end, exitB, enterC
    assert(gTrace.size() == 5);
    assert(gTrace[0] == "exitA");
    assert(gTrace[1] == "enterB-begin");
    assert(gTrace[2] == "enterB-end");  // onEnter(B) 完整跑完（非递归中断）
    assert(gTrace[3] == "exitB");
    assert(gTrace[4] == "enterC");
}

// ---------------------------------------------------------------------------
// 6. Start / Stop 语义
// ---------------------------------------------------------------------------
void TestStartStop()
{
    gTrace.clear();
    StateMachine sm;
    sm.AddState(SIdle, TracingCallbacks("A"));
    sm.AddState(SRun, TracingCallbacks("B"));

    // 未运行时 Fire / Update no-op。
    assert(!sm.Fire(EGo));
    sm.Update(0.016f);
    assert(gTrace.empty());
    assert(!sm.IsRunning());
    assert(sm.Current() == StateMachine::kNoState);

    // Start 调 onEnter。
    sm.Start(SIdle);
    assert(sm.IsRunning());
    assert(gTrace.size() == 1 && gTrace[0] == "enterA");

    // Stop 调当前 onExit；之后 IsRunning false、Current==kNoState。
    sm.Stop();
    assert(!sm.IsRunning());
    assert(sm.Current() == StateMachine::kNoState);
    assert(gTrace.size() == 2 && gTrace[1] == "exitA");

    // 重复 Stop no-op。
    sm.Stop();
    assert(gTrace.size() == 2);

    // 重复 Start 先 Stop 旧态（调旧 onExit）再起新态。
    gTrace.clear();
    sm.Start(SIdle);
    assert(gTrace.size() == 1 && gTrace[0] == "enterA");
    sm.Start(SRun);  // 先 exitA 再 enterB
    assert(sm.Current() == SRun);
    assert(gTrace.size() == 3);
    assert(gTrace[1] == "exitA");
    assert(gTrace[2] == "enterB");
}

// ---------------------------------------------------------------------------
// 7. IsIn / IsRunning / Current + 未注册状态转移不崩（无回调）
// ---------------------------------------------------------------------------
void TestQueriesAndUnregisteredStates()
{
    StateMachine sm;
    // 只注册一部分状态：SRun 故意不 AddState（转移到它应安全、无回调）。
    sm.AddState(SIdle, TracingCallbacks("A"));
    sm.AddTransition(SIdle, EGo, SRun);  // SRun 未注册

    assert(!sm.IsRunning());
    assert(!sm.IsIn(SIdle));

    sm.Start(SIdle);
    assert(sm.IsRunning());
    assert(sm.IsIn(SIdle));
    assert(!sm.IsIn(SRun));
    assert(sm.Current() == SIdle);

    // 转移到未注册状态：不崩，mCurrent 更新，无 onEnter/onExit 回调可调。
    const bool moved = sm.Fire(EGo);
    assert(moved);
    assert(sm.Current() == SRun);
    assert(sm.IsIn(SRun));

    // 未注册态上 Update：onUpdate 无 → no-op、不崩。
    sm.Update(0.016f);
    assert(sm.Current() == SRun);

    // Stop 未注册态：无 onExit → no-op、不崩。
    sm.Stop();
    assert(!sm.IsRunning());
    assert(sm.Current() == StateMachine::kNoState);
    assert(!sm.IsIn(SRun));
}

// ---------------------------------------------------------------------------
// 8. 重复 AddState 覆盖回调
// ---------------------------------------------------------------------------
void TestReAddStateOverwrites()
{
    gTrace.clear();
    StateMachine sm;
    sm.AddState(SIdle, TracingCallbacks("A"));
    // 覆盖 SIdle 的回调为记录 "A2"。
    StateCallbacks a2;
    a2.onEnter = []() { gTrace.push_back("enterA2"); };
    sm.AddState(SIdle, a2);

    sm.Start(SIdle);
    assert(gTrace.size() == 1 && gTrace[0] == "enterA2");  // 用覆盖后的回调
}

// ---------------------------------------------------------------------------
// 9. onExit 里调 Stop() —— 不无限递归、干净停机（对抗式复核逮到的栈溢出修复回归）
// ---------------------------------------------------------------------------
void TestStopFromOnExit()
{
    gTrace.clear();
    StateMachine   sm;
    StateCallbacks a;
    a.onEnter = []() { gTrace.push_back("enterA"); };
    a.onExit  = [&sm]() {
        gTrace.push_back("exitA");
        sm.Stop();  // 退出时自停 —— 修复前会无限递归 CallExit → 栈溢出
    };
    sm.AddState(SIdle, a);
    sm.Start(SIdle);
    sm.Stop();  // 顶层 Stop → onExit(A) 里再 Stop() 不应无限递归
    // onExit 恰一次（非递归多次），机器干净停机。
    assert(gTrace.size() == 2 && gTrace[0] == "enterA" && gTrace[1] == "exitA");
    assert(!sm.IsRunning() && sm.Current() == StateMachine::kNoState);
    std::printf("  [ok] TestStopFromOnExit（onExit 里 Stop 不递归）\n");
}

// ---------------------------------------------------------------------------
// 10. 转移 onExit 里 Stop() —— 中止转移，不误触发目标 onEnter、无内部腐坏（复核逮到）
// ---------------------------------------------------------------------------
void TestStopDuringTransition()
{
    gTrace.clear();
    StateMachine   sm;
    StateCallbacks a;
    a.onEnter = []() { gTrace.push_back("enterA"); };
    a.onExit  = [&sm]() {
        gTrace.push_back("exitA");
        sm.Stop();  // 转移中途自停
    };
    StateCallbacks b;
    b.onEnter = []() { gTrace.push_back("enterB"); };  // 不应被调
    b.onExit  = []() { gTrace.push_back("exitB"); };
    sm.AddState(SIdle, a);
    sm.AddState(SRun, b);
    sm.AddTransition(SIdle, EGo, SRun);
    sm.Start(SIdle);
    sm.Fire(EGo);  // DoTransition: exitA → onExit 调 Stop → 应中止，不 enterB
    assert(gTrace.size() == 2 && gTrace[0] == "enterA" && gTrace[1] == "exitA");
    for (const std::string& s : gTrace)
    {
        assert(s != "enterB" && "Stop 后不应误触发目标 onEnter");
    }
    // 机器停机且内部一致（非 mRunning=false 却 mCurrent=SRun 的腐坏）。
    assert(!sm.IsRunning() && sm.Current() == StateMachine::kNoState);
    std::printf("  [ok] TestStopDuringTransition（转移中 Stop 中止不腐坏）\n");
}

// ---------------------------------------------------------------------------
// 11. guard 副作用 AddTransition —— 遍历中 realloc 转移表不 UAF（复核 ASan 逮到）
// ---------------------------------------------------------------------------
void TestGuardMutatesTransitions()
{
    gTrace.clear();
    StateMachine sm;
    sm.AddState(SIdle);
    sm.AddState(SRun);
    sm.AddState(SJump);
    // 第一条转移的 guard 里疯狂 AddTransition（触发 mEventTransitions 多次 realloc），返回 false。
    sm.AddTransition(SIdle, EGo, SRun, [&sm]() {
        for (int i = 0; i < 64; ++i)
        {
            sm.AddTransition(SIdle, EJmp, SJump, []() { return false; });
        }
        return false;  // 本条不通过
    });
    // 第二条（真正生效的）转移：无 guard → 通过 → 转 SJump。
    sm.AddTransition(SIdle, EGo, SJump);
    sm.Start(SIdle);
    // Fire 遍历时第一条 guard realloc 了转移表——修复前正执行的 guard/std::function 与 t.to
    // 在旧存储上被释放 → UAF；修复后（拷贝 to/guard）安全，最终按第二条转移到 SJump。
    const bool moved = sm.Fire(EGo);
    assert(moved && sm.IsIn(SJump));
    std::printf("  [ok] TestGuardMutatesTransitions（guard 内 realloc 不 UAF）\n");
}

}  // namespace

// 编译期：不可拷贝、不可移动（单例式服务，回调捕获外部状态）。
static_assert(!std::is_copy_constructible<StateMachine>::value &&
                  !std::is_move_constructible<StateMachine>::value,
              "StateMachine 必须不可拷贝 / 移动（回调可能捕获外部状态，单例式服务）");
static_assert(!std::is_copy_assignable<StateMachine>::value &&
                  !std::is_move_assignable<StateMachine>::value,
              "StateMachine 必须不可拷贝赋值 / 移动赋值");

int main()
{
    TestBasicEventTransitionsAndCallbackOrder();
    TestGuardedTransitions();
    TestAutoTransitions();
    TestFireFromOnUpdate();
    TestReentrantChainedTransition();
    TestStartStop();
    TestQueriesAndUnregisteredStates();
    TestReAddStateOverwrites();
    TestStopFromOnExit();
    TestStopDuringTransition();
    TestGuardMutatesTransitions();
    return 0;
}
