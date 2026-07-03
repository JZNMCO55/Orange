#ifndef ORANGE_ENGINE_PLATFORM_WINDOW_EVENT_H
#define ORANGE_ENGINE_PLATFORM_WINDOW_EVENT_H

// ---------------------------------------------------------------------------
// Platform::WindowEvent —— 从 `Window` 回调流出的、不依赖任何第三方的
// 唯一事件载体。variant 的备选项与 GLFW 在平台层暴露的事件面对齐；更
// 高层语义（action mapping、key 名查询、按键 repeat 去重）属于未来的
// Input 模块，本层刻意不做。
//
// 关于 `key` / `rawButton`：
// * 当前直接透传 GLFW 的 key code / button 索引。文档上写明它们是
//   "原始整数代码"——需要 `KeyCode::Escape` 这种符号化常量的消费者
//   要等 Input 模块上线，那一层会拥有翻译表。Window 这一层只做管线，
//   不做语义。
// * 修饰键 mods 同样透传 GLFW 的位掩码（GLFW_MOD_*）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstdint>
#include <variant>

namespace Orange::Engine::Platform
{

    enum class KeyAction : std::int32_t
    {
        Press = 0,
        Release,
        Repeat,
    };

    enum class MouseButtonId : std::int32_t
    {
        Left = 0,
        Right,
        Middle,
        Other,
    };

    struct WindowCloseEvent
    {
    };

    struct WindowResizeEvent
    {
        std::uint32_t width{0};
        std::uint32_t height{0};
    };

    struct WindowFocusEvent
    {
        bool focused{false};
    };

    struct KeyEvent
    {
        std::int32_t key{0}; // GLFW 原始 key code（后续由 Input 模块包装）
        std::int32_t scancode{0};
        KeyAction    action{KeyAction::Press};
        std::int32_t mods{0}; // GLFW 原始 mod 位掩码（GLFW_MOD_*）
    };

    struct CharEvent
    {
        std::uint32_t codepoint{0};
    };

    struct MouseButtonEvent
    {
        MouseButtonId button{MouseButtonId::Other};
        std::int32_t  rawButton{-1};
        KeyAction     action{KeyAction::Press};
        std::int32_t  mods{0};
    };

    struct MouseMoveEvent
    {
        double x{0.0};
        double y{0.0};
    };

    struct ScrollEvent
    {
        double xOffset{0.0};
        double yOffset{0.0};
    };

    using WindowEvent = std::variant<
        WindowCloseEvent,
        WindowResizeEvent,
        WindowFocusEvent,
        KeyEvent,
        CharEvent,
        MouseButtonEvent,
        MouseMoveEvent,
        ScrollEvent>;

} // namespace Orange::Engine::Platform

#endif // ORANGE_ENGINE_PLATFORM_WINDOW_EVENT_H
