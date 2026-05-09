#ifndef ORANGE_ENGINE_INPUT_ACTION_H
#define ORANGE_ENGINE_INPUT_ACTION_H

// ---------------------------------------------------------------------------
// Action —— 输入语义层"动作"描述。
//
// 一条 Action 把游戏意义上的"跳跃 / 攻击 / 暂停" 与若干物理 binding（按
// 键 / 鼠标按钮 / 手柄按键）解耦。Action 的运行时状态由 InputContext 在
// 每帧根据物理事件推进，调用方按名查 GetState(actionName)。
//
// 状态机（Phase 4 / Task 08 范围）：
//
//     Idle ──key down──> Pressed ──next frame──> Held
//      ▲                                          │
//      │                                       key up
//      │                                          │
//      └────── next frame ────── Released <───────┘
//
//   * Idle：按键松开、动作未触发；
//   * Pressed：按键按下的"那一帧"——"刚 trigger" 信号；
//   * Held：按下后续帧——长按；
//   * Released：松开的"那一帧"——"刚 release" 信号。
//
// 默认 Trigger 类型只有 Button；Axis / Vector2 留 Phase 5（手柄摇杆 / 鼠
// 标移动需要时再扩）。Phase 4 范围内 Action 只承载 Button 语义即够用——
// "加载 default.actions.json + 模拟 key event 触发 Action::Triggered 状态正确"
// 的验收要求。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/input/InputDevice.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Orange::Engine::Input
{

enum class ActionType : std::uint8_t
{
    Button = 0,  // 唯一支持的类型——Phase 4 / Task 08
};

enum class ActionState : std::uint8_t
{
    Idle     = 0,
    Pressed  = 1,
    Held     = 2,
    Released = 3,
};

// 单条物理 binding——指向某个具体输入源 + 该源的具体 button id。
// type 决定 codeOrButton 的解读方式：
//   * Key      → codeOrButton 与 KeyCode 等价
//   * Mouse    → 与 MouseButton 等价
//   * Gamepad  → 与 GamepadButton 等价（Phase 4 内不真消费）
struct ORANGE_ENGINE_API ActionBinding
{
    enum class Source : std::uint8_t
    {
        Key     = 0,
        Mouse   = 1,
        Gamepad = 2,
    };

    Source       source{Source::Key};
    std::int32_t codeOrButton{0};
};

struct ORANGE_ENGINE_API Action
{
    std::string                name;
    ActionType                 type{ActionType::Button};
    std::vector<ActionBinding> bindings;

    // 当前帧状态——由 InputContext::Tick 推进，调用方读。
    ActionState                state{ActionState::Idle};
};

// 便利谓词：状态是否处于 "刚刚发生" 类（Pressed / Released），
// 或处于 "持续" 类（Held）。游戏代码通常按这两类语义查询。
inline bool IsTriggered(ActionState s) noexcept { return s == ActionState::Pressed; }
inline bool IsHeld     (ActionState s) noexcept { return s == ActionState::Pressed || s == ActionState::Held; }
inline bool IsReleased (ActionState s) noexcept { return s == ActionState::Released; }

}  // namespace Orange::Engine::Input

#endif  // ORANGE_ENGINE_INPUT_ACTION_H
