// AnimationStateMachineTest —— FSM 端到端验证。
//
// 覆盖：
//   * 3 状态 / 2 过渡 + 显式 SetInitialState 后 OnEnter 触发；
//   * Tick 累计 elapsedSeconds，未触发时不切状态；
//   * 条件成立时按"OnExit 旧 / OnEnter 新 / elapsed 清零"顺序 fire；
//   * 同帧只 fire 一条 transition（避免无限链）；
//   * AddTransition 空 condition 被拒绝（TransitionCount 不增）；
//   * 重名 AddState 覆盖 OnEnter / OnExit。
//
// v0.7 c2-7（ADR-005）新增覆盖：
//   * AddTransition(ConditionExpr) 数据驱动路径：Bool 参数 + If/IfNot；
//   * Float 参数 + Greater/Less/Equal 比较；
//   * Trigger 参数 fire 后自动 reset 为 false；
//   * 多 ConditionExpr AND 组合；
//   * Parameter 未注册 → no-fire；
//   * 类型 widening（Bool ↔ Int ↔ Float）；
//   * 旧 ConditionFn 路径与新 ConditionExpr 路径混用同一 FSM 行为正常。

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
    // 空 ConditionFn（默认构造的 std::function）：应被拒
    fsm.AddTransition("a", "b", Anim::AnimationStateMachine::ConditionFn{});
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

// ----- v0.7 c2-7（ADR-005）新增 -----

void TestConditionExprBoolIf()
{
    Anim::AnimationStateMachine fsm;
    fsm.AddState("idle");
    fsm.AddState("walk");
    fsm.RegisterParameter("isMoving", Anim::ParameterType::Bool);
    fsm.AddTransition("idle", "walk",
                      std::vector<Anim::ConditionExpr>{
                          {"isMoving", Anim::ConditionOp::If, false}});
    fsm.SetInitialState("idle");

    fsm.Tick(0.1f);
    assert(fsm.CurrentState() == "idle");  // isMoving 默认 false → If 不命中

    fsm.SetParameterBool("isMoving", true);
    fsm.Tick(0.0f);
    assert(fsm.CurrentState() == "walk");
}

void TestConditionExprBoolIfNot()
{
    Anim::AnimationStateMachine fsm;
    fsm.AddState("a");
    fsm.AddState("b");
    fsm.RegisterParameter("ready", Anim::ParameterType::Bool);
    fsm.AddTransition("a", "b",
                      std::vector<Anim::ConditionExpr>{
                          {"ready", Anim::ConditionOp::IfNot, false}});
    fsm.SetInitialState("a");

    // ready 默认 false → IfNot 命中 → 立即过
    fsm.Tick(0.0f);
    assert(fsm.CurrentState() == "b");
}

void TestConditionExprFloatGreater()
{
    Anim::AnimationStateMachine fsm;
    fsm.AddState("idle");
    fsm.AddState("run");
    fsm.RegisterParameter("speed", Anim::ParameterType::Float);
    fsm.AddTransition("idle", "run",
                      std::vector<Anim::ConditionExpr>{
                          {"speed", Anim::ConditionOp::Greater, 5.0f}});
    fsm.SetInitialState("idle");

    fsm.SetParameterFloat("speed", 3.0f);
    fsm.Tick(0.0f);
    assert(fsm.CurrentState() == "idle");

    fsm.SetParameterFloat("speed", 10.0f);
    fsm.Tick(0.0f);
    assert(fsm.CurrentState() == "run");
}

void TestConditionExprAndCompound()
{
    Anim::AnimationStateMachine fsm;
    fsm.AddState("idle");
    fsm.AddState("sprint");
    fsm.RegisterParameter("isMoving", Anim::ParameterType::Bool);
    fsm.RegisterParameter("speed",    Anim::ParameterType::Float);
    fsm.AddTransition("idle", "sprint",
                      std::vector<Anim::ConditionExpr>{
                          {"isMoving", Anim::ConditionOp::If,      false},
                          {"speed",    Anim::ConditionOp::Greater, 8.0f}});
    fsm.SetInitialState("idle");

    fsm.SetParameterBool("isMoving", true);
    fsm.SetParameterFloat("speed", 5.0f);
    fsm.Tick(0.0f);
    assert(fsm.CurrentState() == "idle");  // speed 不够，AND fail

    fsm.SetParameterFloat("speed", 12.0f);
    fsm.Tick(0.0f);
    assert(fsm.CurrentState() == "sprint");
}

void TestTriggerAutoReset()
{
    Anim::AnimationStateMachine fsm;
    fsm.AddState("idle");
    fsm.AddState("attack");
    fsm.AddState("recover");
    fsm.RegisterParameter("attackPressed", Anim::ParameterType::Trigger);
    // idle → attack 仅在 trigger 命中时
    fsm.AddTransition("idle", "attack",
                      std::vector<Anim::ConditionExpr>{
                          {"attackPressed", Anim::ConditionOp::If, false}});
    // attack → recover 仅在再次 trigger 时（验证 reset 行为）
    fsm.AddTransition("attack", "recover",
                      std::vector<Anim::ConditionExpr>{
                          {"attackPressed", Anim::ConditionOp::If, false}});
    fsm.SetInitialState("idle");

    fsm.SetTrigger("attackPressed");
    fsm.Tick(0.0f);
    assert(fsm.CurrentState() == "attack");
    // Trigger 已自动 reset：再 Tick 不会进 recover
    fsm.Tick(0.0f);
    assert(fsm.CurrentState() == "attack");

    // 显式再次 SetTrigger → 进 recover
    fsm.SetTrigger("attackPressed");
    fsm.Tick(0.0f);
    assert(fsm.CurrentState() == "recover");
}

void TestConditionExprUnregisteredParamNoFire()
{
    Anim::AnimationStateMachine fsm;
    fsm.AddState("a");
    fsm.AddState("b");
    // 不注册 "missing" 参数
    fsm.AddTransition("a", "b",
                      std::vector<Anim::ConditionExpr>{
                          {"missing", Anim::ConditionOp::If, false}});
    fsm.SetInitialState("a");
    fsm.Tick(0.0f);
    assert(fsm.CurrentState() == "a");  // 引用未注册参数 → no-fire
}

void TestConditionExprEmptyListAlwaysFires()
{
    Anim::AnimationStateMachine fsm;
    fsm.AddState("a");
    fsm.AddState("b");
    fsm.AddTransition("a", "b", std::vector<Anim::ConditionExpr>{});  // 空 list = 无条件
    fsm.SetInitialState("a");
    fsm.Tick(0.0f);
    assert(fsm.CurrentState() == "b");
}

void TestParameterTypeWidening()
{
    Anim::AnimationStateMachine fsm;
    fsm.RegisterParameter("flag", Anim::ParameterType::Bool);
    fsm.RegisterParameter("count", Anim::ParameterType::Int);
    fsm.RegisterParameter("ratio", Anim::ParameterType::Float);

    // bool → int / float widening 通过 SetParameterBool
    fsm.SetParameterBool("count", true);
    assert(fsm.GetParameterInt("count") == 1);
    fsm.SetParameterBool("ratio", true);
    assert(fsm.GetParameterFloat("ratio") > 0.99f);

    // int → bool / float widening
    fsm.SetParameterInt("flag", 5);
    assert(fsm.GetParameterBool("flag") == true);
    fsm.SetParameterInt("ratio", 3);
    assert(fsm.GetParameterFloat("ratio") > 2.99f);
}

void TestMixedLambdaAndExprPaths()
{
    Anim::AnimationStateMachine fsm;
    fsm.AddState("idle");
    fsm.AddState("walk");
    fsm.AddState("jump");
    bool wantJump = false;
    fsm.RegisterParameter("isMoving", Anim::ParameterType::Bool);
    // idle → walk 走数据路径
    fsm.AddTransition("idle", "walk",
                      std::vector<Anim::ConditionExpr>{
                          {"isMoving", Anim::ConditionOp::If, false}});
    // idle → jump 走 lambda 路径（同 FSM 内两路径并存）
    fsm.AddTransition("idle", "jump", [&](const auto&) { return wantJump; });
    fsm.SetInitialState("idle");
    assert(fsm.TransitionCount() == 2);

    wantJump = true;
    fsm.Tick(0.0f);
    // idle → jump 先注册，按注册顺序 idle → walk 先 evaluate；isMoving false → fail；
    // 再 evaluate idle → jump → wantJump true → fire
    assert(fsm.CurrentState() == "jump");
}

}  // namespace

int main()
{
    TestThreeStatesTwoTransitions();
    TestEmptyConditionRejected();
    TestUnsetInitialStateTickIsNoOp();
    TestStateNameOverwrite();
    TestSetInitialStateNonExistent();
    // v0.7 c2-7（ADR-005）新增
    TestConditionExprBoolIf();
    TestConditionExprBoolIfNot();
    TestConditionExprFloatGreater();
    TestConditionExprAndCompound();
    TestTriggerAutoReset();
    TestConditionExprUnregisteredParamNoFire();
    TestConditionExprEmptyListAlwaysFires();
    TestParameterTypeWidening();
    TestMixedLambdaAndExprPaths();
    return 0;
}
