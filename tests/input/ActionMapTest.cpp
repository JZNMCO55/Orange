// ActionMapTest —— Phase 4 / Task 08 验收（一）：
// ActionMap 状态机 + 物理事件分发 + JSON 加载。
//
// 覆盖：
//   1. AddAction / Find / GetState 基础 CRUD；
//   2. PostKeyEvent → Idle → Pressed → Held → Released → Idle 完整状态机；
//   3. 同一 key 绑多个 Action：所有命中条目独立计状态；
//   4. JSON 加载 default.actions.json（fixture by CMake macro）+ 模拟
//      key event 触发 IsTriggered / IsHeld / IsReleased 三谓词正确。

#include "orange/engine/input/Action.h"
#include "orange/engine/input/ActionMap.h"
#include "orange/engine/input/InputContext.h"
#include "orange/engine/input/InputDevice.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace In = Orange::Engine::Input;

#ifndef ORANGE_ENGINE_INPUT_TEST_DATA_DIR
#error "ORANGE_ENGINE_INPUT_TEST_DATA_DIR must be defined by CMake"
#endif

namespace
{

    const std::string kDefaultActionsPath =
        std::string(ORANGE_ENGINE_INPUT_TEST_DATA_DIR) + "/default.actions.json";

    In::Action MakeAction(std::string name, In::ActionBinding binding)
    {
        In::Action a;
        a.name = std::move(name);
        a.type = In::ActionType::Button;
        a.bindings.push_back(binding);
        return a;
    }

    In::ActionBinding KeyBind(In::KeyCode k)
    {
        return In::ActionBinding{In::ActionBinding::Source::Key, static_cast<std::int32_t>(k)};
    }

    void TestStateMachine()
    {
        In::ActionMap map;
        map.AddAction(MakeAction("jump", KeyBind(In::KeyCode::Space)));

        // 初始 Idle
        assert(map.GetState("jump") == In::ActionState::Idle);
        assert(map.Size() == 1);

        // 按下 → Pressed（"刚 trigger 这一帧"）
        map.PostKeyEvent(In::KeyCode::Space, true);
        assert(map.GetState("jump") == In::ActionState::Pressed);
        assert(In::IsTriggered(map.GetState("jump")));
        assert(In::IsHeld(map.GetState("jump"))); // Pressed 也算 Held（同一帧两种语义）
        assert(!In::IsReleased(map.GetState("jump")));

        // 下一帧：BeginFrame 把 Pressed → Held
        map.BeginFrame();
        assert(map.GetState("jump") == In::ActionState::Held);
        assert(!In::IsTriggered(map.GetState("jump")));
        assert(In::IsHeld(map.GetState("jump")));

        // 抬起 → Released
        map.PostKeyEvent(In::KeyCode::Space, false);
        assert(map.GetState("jump") == In::ActionState::Released);
        assert(In::IsReleased(map.GetState("jump")));
        assert(!In::IsHeld(map.GetState("jump")));

        // 再下一帧：Released → Idle
        map.BeginFrame();
        assert(map.GetState("jump") == In::ActionState::Idle);
    }

    void TestDuplicateBindingHitsMultipleActions()
    {
        In::ActionMap map;
        // 两条 Action 同绑 Space，应同时进 Pressed。
        map.AddAction(MakeAction("a", KeyBind(In::KeyCode::Space)));
        map.AddAction(MakeAction("b", KeyBind(In::KeyCode::Space)));

        map.PostKeyEvent(In::KeyCode::Space, true);
        assert(map.GetState("a") == In::ActionState::Pressed);
        assert(map.GetState("b") == In::ActionState::Pressed);

        map.PostKeyEvent(In::KeyCode::Space, false);
        assert(map.GetState("a") == In::ActionState::Released);
        assert(map.GetState("b") == In::ActionState::Released);
    }

    void TestUnboundKeyIgnored()
    {
        In::ActionMap map;
        map.AddAction(MakeAction("jump", KeyBind(In::KeyCode::Space)));
        map.PostKeyEvent(In::KeyCode::W, true); // 未绑定 → 不触发
        assert(map.GetState("jump") == In::ActionState::Idle);
        assert(map.GetState("anything") == In::ActionState::Idle); // 不存在的 action
    }

    void TestRepeatedDownDoesNotRetriggerAfterHeld()
    {
        // 长按场景：键盘自动 repeat 会再发 key down，但 ActionMap 的状态机
        // 在 Held 时收到第二次 down 应当**保持** Held，不再变 Pressed。
        // （IsTriggered 只在"按下那一帧"为真。）
        In::ActionMap map;
        map.AddAction(MakeAction("jump", KeyBind(In::KeyCode::Space)));
        map.PostKeyEvent(In::KeyCode::Space, true);
        map.BeginFrame(); // Pressed → Held
        assert(map.GetState("jump") == In::ActionState::Held);
        map.PostKeyEvent(In::KeyCode::Space, true); // 重复 down
        assert(map.GetState("jump") == In::ActionState::Held);
        assert(!In::IsTriggered(map.GetState("jump")));
    }

    void TestJsonLoad()
    {
        auto r = In::LoadActionMapFromFile(kDefaultActionsPath);
        if (r.IsErr())
        {
            std::fprintf(stderr, "[ActionMapTest] LoadActionMapFromFile failed: %d\n",
                         static_cast<int>(r.Error()));
        }
        assert(r.IsOk());
        auto& map = r.Value();

        // default.actions.json 含 5 条 action；逐条 sanity-check 关键 binding。
        assert(map.Size() == 5);
        assert(map.Find("move_left") != nullptr);
        assert(map.Find("move_right") != nullptr);
        assert(map.Find("jump") != nullptr);
        assert(map.Find("attack") != nullptr);
        assert(map.Find("pause") != nullptr);

        // jump 对应 Space + gamepad:South（未消费）
        map.PostKeyEvent(In::KeyCode::Space, true);
        assert(In::IsTriggered(map.GetState("jump")));

        // move_left 同时绑 A + Left；按 Left 也命中。
        map.PostKeyEvent(In::KeyCode::Left, true);
        assert(In::IsTriggered(map.GetState("move_left")));

        // attack 绑 mouse:Left + key:J；用 mouse 触发。
        map.PostMouseButton(In::MouseButton::Left, true);
        assert(In::IsTriggered(map.GetState("attack")));
    }

    void TestJsonRejectsMissingSchemaVersion()
    {
        // 直接构造一个不带 schema_version 的 reader——loader 应拒。
        auto rd = Orange::Engine::JsonReader::FromString(R"({ "actions": [] })");
        assert(rd.IsOk());
        auto r = In::LoadActionMapFromJson(rd.Value());
        assert(r.IsErr());
        assert(r.Error() == Orange::Engine::ResultCode::InvalidArgument);
    }

} // namespace

int main()
{
    TestStateMachine();
    TestDuplicateBindingHitsMultipleActions();
    TestUnboundKeyIgnored();
    TestRepeatedDownDoesNotRetriggerAfterHeld();
    TestJsonLoad();
    TestJsonRejectsMissingSchemaVersion();
    return 0;
}
