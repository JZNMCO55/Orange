#ifndef ORANGE_ENGINE_PLATFORM_WINDOW_EVENT_H
#define ORANGE_ENGINE_PLATFORM_WINDOW_EVENT_H

// ---------------------------------------------------------------------------
// Phase 1 / Task 06 — Platform::WindowEvent
//
// `WindowEvent` is the single, third-party-free shape that flows out of
// `Window`'s callback. The variant alternatives mirror the event surface
// GLFW gives us at the platform layer; richer semantics (action mapping,
// key-name lookup, repeat de-duplication) belong to a future Input module
// task and are deliberately NOT done here.
//
// Notes on `key` / `rawButton`:
// * Phase 1 passes through GLFW key codes / button indices verbatim. They
//   are documented as "raw integer codes" — consumers that need symbolic
//   `KeyCode::Escape` must wait for the Input module, which will own the
//   translation table. The Window layer's job is plumbing, not semantics.
// * Modifier bitmasks ARE passed through similarly (GLFW_MOD_* values).
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
    std::int32_t key{0};       // raw GLFW key code (Input module wraps later)
    std::int32_t scancode{0};
    KeyAction    action{KeyAction::Press};
    std::int32_t mods{0};      // raw GLFW mod bitmask (GLFW_MOD_*)
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

}  // namespace Orange::Engine::Platform

#endif  // ORANGE_ENGINE_PLATFORM_WINDOW_EVENT_H
