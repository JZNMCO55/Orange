#ifndef ORANGE_ENGINE_INPUT_INPUT_DEVICE_H
#define ORANGE_ENGINE_INPUT_INPUT_DEVICE_H

// ---------------------------------------------------------------------------
// InputDevice —— 物理输入设备的 key / button 枚举。
//
// 设计取舍：
//   * 数值与 GLFW 对齐——Window 走 GLFW，原始 key code 直接
//     reinterpret 转 KeyCode，省一层人肉映射表；
//   * 命名走"按键功能"而非"键位"：Space / Enter / Escape，避免
//     美式 / 国际键盘差异；
//   * Mouse / Gamepad 各自独立 enum，避免一个大杂烩 enum 的 bit-packing 麻烦。
//
// 不暴露 GLFW 头：本头零 GLFW 依赖。值与 GLFW 对齐由 src/input/InputContext.cpp
// 内部 static_assert 校验。
// ---------------------------------------------------------------------------

#include <cstdint>

namespace Orange::Engine::Input
{

    // 与 GLFW 数值对齐（GLFW_KEY_*）。最小可用集——本期不做完整键盘字符
    // 集。需要时按 GLFW 数值补条目（W / A / S / D / 1-9 / F1-F12 等也已覆盖）。
    enum class KeyCode : std::int32_t
    {
        Unknown      = -1,
        Space        = 32,
        Apostrophe   = 39,
        Comma        = 44,
        Minus        = 45,
        Period       = 46,
        Slash        = 47,
        Num0         = 48,
        Num1         = 49,
        Num2         = 50,
        Num3         = 51,
        Num4         = 52,
        Num5         = 53,
        Num6         = 54,
        Num7         = 55,
        Num8         = 56,
        Num9         = 57,
        Semicolon    = 59,
        Equal        = 61,
        A            = 65,
        B            = 66,
        C            = 67,
        D            = 68,
        E            = 69,
        F            = 70,
        G            = 71,
        H            = 72,
        I            = 73,
        J            = 74,
        K            = 75,
        L            = 76,
        M            = 77,
        N            = 78,
        O            = 79,
        P            = 80,
        Q            = 81,
        R            = 82,
        S            = 83,
        T            = 84,
        U            = 85,
        V            = 86,
        W            = 87,
        X            = 88,
        Y            = 89,
        Z            = 90,
        Escape       = 256,
        Enter        = 257,
        Tab          = 258,
        Backspace    = 259,
        Right        = 262,
        Left         = 263,
        Down         = 264,
        Up           = 265,
        F1           = 290,
        F2           = 291,
        F3           = 292,
        F4           = 293,
        F5           = 294,
        F6           = 295,
        F7           = 296,
        F8           = 297,
        F9           = 298,
        F10          = 299,
        F11          = 300,
        F12          = 301,
        LeftShift    = 340,
        LeftControl  = 341,
        LeftAlt      = 342,
        RightShift   = 344,
        RightControl = 345,
        RightAlt     = 346,
    };

    enum class MouseButton : std::int32_t
    {
        Unknown = -1,
        Left    = 0,
        Right   = 1,
        Middle  = 2,
    };

    // gamepad 暂占位——当前不接 GLFW gamepad polling，仅为
    // JSON loader 端在解析时不抛"unknown device" 即可。真正接通 polling
    // 待后续综合 demo 上线后再加。
    enum class GamepadButton : std::int32_t
    {
        Unknown = -1,
        South   = 0, // A on Xbox / Cross on PS
        East    = 1, // B / Circle
        West    = 2, // X / Square
        North   = 3, // Y / Triangle
        LB      = 4,
        RB      = 5,
        Back    = 6,
        Start   = 7,
    };

} // namespace Orange::Engine::Input

#endif // ORANGE_ENGINE_INPUT_INPUT_DEVICE_H
