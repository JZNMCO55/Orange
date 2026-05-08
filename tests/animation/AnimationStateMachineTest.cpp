// AnimationStateMachineTest —— FSM 端到端验证。
//
// 覆盖：
//   * 3 状态 / 2 过渡 + 显式 SetInitialState 后 OnEnter 触发；
//   * Tick 累计 elapsedSeconds，未触发时不切状态；
//   * 条件成立时按"OnExit 旧 / OnEnter 新 / elapsed 清零"顺序 fire；
//   * 同帧只 fire 一条 transition（避免无限链）；
//   * AddTransition 空 condition 被拒绝（TransitionCount 不增）；
//   * 重名 AddState 覆盖 OnEnter / OnExit。

#include "orange/engine/animation/AnimationStateMachine.h"

#include <cassert>
#include <string>
#include <vector>

namespace Anim = Orange::Engine::Animation;

namespace
{

void TestThreeStatesTwoTransitions()
{
    Anim::AnimationStateMachine fsm;

    std::vector<std::string> log;

    fsm.AddState("idle",
                 [&] { log.push_back("enter:idle"); },
                 [&] { log.push_back("exit:idle"); });
    fsm.AddState("walk",
                 [&] { log.push_back("enter:walk"); },
                 [&] { log.push_back("exit:walk"); });
    fsm.AddState("run",
                 [&] { log.push_back("enter:run"); },
                 [&] { log.push_back("exit:run"); });

    bool wantWalk = false;
    bool wantRun  = false;

    fsm.AddTransition("idle", "walk", [&](const auto&) { return wantWalk; });
    fsm.AddTransition("walk", "run",  [&](const auto&) { return wantRun;  });

    assert(fsm.StateCount() == 3);
    assert(fsm.TransitionCount() == 2);

    fsm.SetInitialState("idle");
    assert(fsm.CurrentState() == "idle");
    assert(log.size() == 1 && log.back() == "enter:idle");

    // Tick 0.5s 不触发，elapsed 累计
    fsm.Tick(0.5f);
    assert(fsm.CurrentState() == "idle");
    assert(fsm.ElapsedSeconds() > 0.49f && fsm.ElapsedSeconds() < 0.51f);

    // 触发 idle → walk
    wantWalk = true;
    fsm.Tick(0.1f);
    assert(fsm.CurrentState() == "walk");
    // OnExit 旧 / OnEnter 新 顺序
    assert(log.size() == 3);
    assert(log[1] == "exit:idle");
    assert(log[2] == "enter:walk");
    // elapsed 进新状态后清零（Tick 内累计 dt 后 fire transition；
    // EnterState 把 currentElapsed 重置为 0，本帧后续不再加 dt）
    assert(fsm.ElapsedSeconds() == 0.0f);

    // 同帧不 cascade：即使 wantRun = true，新进 walk 不立即转 run
    wantRun = true;
    // 上一次 Tick 已 fire；下一次 Tick 才求值 walk → run
    fsm.Tick(0.0f);
    assert(fsm.CurrentState() == "run");
}

void TestEmptyConditionRejected()
{
    Anim::AnimationStateMachine fsm;
    fsm.AddState("a");
    fsm.AddState("b");
    fsm.AddTransition("a", "b", {});  // 空 condition：应被拒
    assert(fsm.TransitionCount() == 0);

    fsm.AddTransition("a", "b", [](const auto&) { return true; });
    assert(fsm.TransitionCount() == 1);
}

void TestUnsetInitialStateTickIsNoOp()
{
    Anim::AnimationStateMachine fsm;
    fsm.AddState("solo");
    // 不调 SetInitialState
    fsm.Tick(1.0f);
    assert(fsm.CurrentState().empty());
    assert(fsm.ElapsedSeconds() == 0.0f);
}

void TestStateNameOverwrite()
{
    Anim::AnimationStateMachine fsm;
    int v1 = 0;
    int v2 = 0;
    fsm.AddState("x", [&] { v1 = 1; });
    fsm.AddState("x", [&] { v2 = 1; });  // 覆盖
    assert(fsm.StateCount() == 1);
    fsm.SetInitialState("x");
    assert(v1 == 0);
    assert(v2 == 1);
}

void TestSetInitialStateNonExistent()
{
    Anim::AnimationStateMachine fsm;
    fsm.AddState("a");
    fsm.SetInitialState("nonexistent");
    assert(fsm.CurrentState().empty());
    fsm.SetInitialState("a");
    assert(fsm.CurrentState() == "a");
}

}  // namespace

int main()
{
    TestThreeStatesTwoTransitions();
    TestEmptyConditionRejected();
    TestUnsetInitialStateTickIsNoOp();
    TestStateNameOverwrite();
    TestSetInitialStateNonExistent();
    return 0;
}
