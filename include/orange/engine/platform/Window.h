#ifndef ORANGE_ENGINE_PLATFORM_WINDOW_H
#define ORANGE_ENGINE_PLATFORM_WINDOW_H

// ---------------------------------------------------------------------------
// Phase 1 / Task 06 — Platform::Window
//
// Thin GLFW wrapper. Responsibilities:
//   * own the OS window lifetime (create / destroy);
//   * pump platform events via PollEvents();
//   * expose `ShouldClose()` so the main loop can break;
//   * forward platform events to a single, type-erased
//     `EventCallback = std::function<void(const WindowEvent&)>`.
//
// Explicitly NOT this layer's job:
//   * presenting / swap-chain — owned by OrangeRender's RHI.
//   * input semantics (action maps, KeyCode enum) — owned by the future
//     Input module.
//   * timing / fixed-step accumulation — owned by Core::Time.
//
// Header isolation guarantee:
// * No GLFW types in any signature here. The native HWND is exposed only
//   as `void*` via `GetNativeWindowHandle()` for the renderer to feed into
//   its swap-chain factory.
// * The Window itself is heap-pinned (move/copy deleted) so that GLFW's
//   user-pointer slot can hold a stable `Window*` for callback dispatch.
//   Consumers always own a `std::unique_ptr<Window>` and never relocate
//   the underlying object.
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Result.h>
#include <orange/engine/platform/WindowEvent.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace Orange::Engine::Platform
{

struct WindowDesc
{
    std::string   title{"OrangeEngine"};
    std::uint32_t width{1280};
    std::uint32_t height{720};
    bool          resizable{true};
    bool          visible{true};
};

class ORANGE_ENGINE_API Window
{
public:
    using EventCallback = std::function<void(const WindowEvent&)>;

    // Forward declaration only — full definition lives in
    // src/platform/glfw/Window.cpp and stays a private implementation
    // detail. Promoted to `public` for one practical reason: the GLFW
    // callback shims in that TU need to refer to `Window::Impl*` from
    // file scope to dispatch events without going through the class.
    // Holding a pointer to an opaque incomplete type is not a leak.
    struct Impl;

    static Result<std::unique_ptr<Window>, ResultCode> Create(const WindowDesc& desc);

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&)                 = delete;
    Window& operator=(Window&&)      = delete;

    ~Window();

    // Drains every platform event for every Window in the process.
    // Maps directly to glfwPollEvents(); idempotent within a frame.
    static void PollEvents() noexcept;

    bool ShouldClose() const noexcept;
    void RequestClose() noexcept;

    std::uint32_t      GetWidth() const noexcept;
    std::uint32_t      GetHeight() const noexcept;
    const std::string& GetTitle() const noexcept;

    // Framebuffer dimensions in pixels. Differs from GetWidth/Height on
    // HiDPI displays; the renderer should size its swap-chain to this.
    void GetFramebufferSize(std::uint32_t& width, std::uint32_t& height) const noexcept;

    void SetTitle(std::string_view title);
    void SetEventCallback(EventCallback callback);

    // Native handles for the renderer's surface factory.
    // * `GetNativeWindowHandle()` returns HWND on Windows, cast to void*.
    // * `GetNativeDisplayHandle()` is reserved for X11/Wayland; on Win32
    //   it is always nullptr.
    void* GetNativeWindowHandle() const noexcept;
    void* GetNativeDisplayHandle() const noexcept;

private:
    Window();

    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine::Platform

#endif  // ORANGE_ENGINE_PLATFORM_WINDOW_H
