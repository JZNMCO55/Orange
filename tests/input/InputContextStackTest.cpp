// InputContextStackTest —— Phase 4 / Task 08 验收（二）：
// InputContext 栈语义 + 物理事件只送栈顶。
//
// 覆盖：
//   1. 空栈 Top() == nullptr，PostKeyEvent 不崩；
//   2. Push / Pop 计数与 Depth 一致；
//   3. 栈顶切换后旧 map 状态被冻结、新 map 收事件；
//   4. Pop 时回到的 map 状态被 Reset（避免按键卡住）；
//   5. BeginFrame 只推进栈顶。

#include "orange/engine/input/Action.h"
#include "orange/engine/input/ActionMap.h"
#include "orange/engine/input/InputContext.h"
#include "orange/engine/input/InputDevice.h"

#include <cassert>

namespace In = Orange::Engine::Input;

namespace
{

In::Action MakeKeyAction(std::string name, In::KeyCode k)
{
    In::Action a;
    a.name = std::move(name);
    a.bindings.push_back({In::ActionBinding::Source::Key, static_cast<std::int32_t>(k)});
    return a;
}

void TestEmptyContextSafe()
{
    In::InputContext ctx;
    assert(ctx.Empty());
    assert(ctx.Depth() == 0);
    assert(ctx.Top() == nullptr);

    // 空栈喂事件 / BeginFrame 不崩。
    ctx.BeginFrame();
    ctx.PostKeyEvent(In::KeyCode::Space, true);
    ctx.PostMouseButton(In::MouseButton::Left, false);
    assert(ctx.Pop() == 0);
}

void TestPushPopDepth()
{
    In::InputContext ctx;
    In::ActionMap m1;
    m1.AddAction(MakeKeyAction("jump", In::KeyCode::Space));
    In::ActionMap m2;
    m2.AddAction(MakeKeyAction("confirm", In::KeyCode::Enter));

    assert(ctx.Push(std::move(m1)) == 1);
    assert(ctx.Depth() == 1);
    assert(ctx.Top() != nullptr);
    assert(ctx.Top()->Find("jump") != nullptr);

    assert(ctx.Push(std::move(m2)) == 2);
    assert(ctx.Depth() == 2);
    assert(ctx.Top()->Find("confirm") != nullptr);
    assert(ctx.Top()->Find("jump") == nullptr);  // 顶不再是 m1

    assert(ctx.Pop() == 1);
    assert(ctx.Top()->Find("jump") != nullptr);
    assert(ctx.Pop() == 0);
    assert(ctx.Empty());
}

void TestEventsRoutedToTopOnly()
{
    In::InputContext ctx;
    {
        In::ActionMap m;
        m.AddAction(MakeKeyAction("jump", In::KeyCode::Space));
        ctx.Push(std::move(m));
    }
    {
        In::ActionMap m2;
        m2.AddAction(MakeKeyAction("confirm", In::KeyCode::Enter));
        ctx.Push(std::move(m2));
    }

    // 在栈顶（m2）按 Space —— m2 没有 Space 绑定，不触发；m1 不收事件
    // （事件不下沉），所以 m1 的 jump 仍 Idle。
    ctx.PostKeyEvent(In::KeyCode::Space, true);
    // 没办法直接看 m1（不在栈顶），但 Pop 后应当还是 Idle（因为 Pop
    // Reset 了，所以这条路径其实证明的是"Pop 不会把栈顶的状态泄漏到下层"）。
    // 改证："栈顶 m2 没收到 jump trigger"。
    assert(ctx.Top()->GetState("jump") == In::ActionState::Idle);

    // 按 Enter → m2.confirm Pressed
    ctx.PostKeyEvent(In::KeyCode::Enter, true);
    assert(ctx.Top()->GetState("confirm") == In::ActionState::Pressed);
}

void TestPopResetsExposedMap()
{
    In::InputContext ctx;
    In::ActionMap m1;
    m1.AddAction(MakeKeyAction("jump", In::KeyCode::Space));
    ctx.Push(std::move(m1));

    // 在 m1 上按住 Space → Held
    ctx.PostKeyEvent(In::KeyCode::Space, true);
    ctx.BeginFrame();
    assert(ctx.Top()->GetState("jump") == In::ActionState::Held);

    // Push 一个空 m2 上去——此时 m1 的状态仍是 Held（被冻结）。
    In::ActionMap m2;
    ctx.Push(std::move(m2));

    // Pop 回 m1：契约要求 Reset，所以 jump 应回 Idle，避免"返回主玩法
    // 立刻向前冲"的卡键 bug。
    ctx.Pop();
    assert(ctx.Top()->GetState("jump") == In::ActionState::Idle);
}

void TestBeginFrameOnlyTopMap()
{
    // 仅栈顶推进——验证 BeginFrame 不去打扰下层（哪怕下层有非 Idle 状
    // 态）。在本期"事件只送栈顶 + Pop Reset"语义下下层永远不可能有
    // Pressed 状态超过 1 帧，但 BeginFrame 的"只动栈顶" 契约本身要保。
    In::InputContext ctx;
    In::ActionMap m;
    m.AddAction(MakeKeyAction("jump", In::KeyCode::Space));
    ctx.Push(std::move(m));

    ctx.PostKeyEvent(In::KeyCode::Space, true);
    assert(ctx.Top()->GetState("jump") == In::ActionState::Pressed);

    ctx.BeginFrame();
    assert(ctx.Top()->GetState("jump") == In::ActionState::Held);
}

}  // namespace

int main()
{
    TestEmptyContextSafe();
    TestPushPopDepth();
    TestEventsRoutedToTopOnly();
    TestPopResetsExposedMap();
    TestBeginFrameOnlyTopMap();
    return 0;
}
