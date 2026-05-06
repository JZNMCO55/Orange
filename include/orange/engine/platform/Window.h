#ifndef ORANGE_ENGINE_PLATFORM_WINDOW_H
#define ORANGE_ENGINE_PLATFORM_WINDOW_H

// ---------------------------------------------------------------------------
// Platform::Window —— 一个薄的 GLFW wrapper。承担：
//   * 拥有 OS window 的生命周期（create / destroy）；
//   * 通过 PollEvents() 抽取平台事件；
//   * 暴露 `ShouldClose()` 给主循环判断退出；
//   * 把所有平台事件经一个 type-erased
//     `EventCallback = std::function<void(const WindowEvent&)>` 转发出去。
//
// 不属于本层的职责：
//   * Presenting / swap-chain —— 由 OrangeRender 的 RHI 拥有；
//   * Input 语义（action map、KeyCode 枚举）—— 留给后续 Input 模块；
//   * Timing / fixed-step accumulation —— 由 Core::Time 负责。
//
// 头隔离保证：
// * 公共签名中不出现任何 GLFW 类型。原生 HWND 仅以 `void*` 形式
//   通过 `GetNativeWindowHandle()` 暴露给渲染器构 swap-chain 用。
// * Window 本身被 heap-pin（move/copy 都禁用），这样 GLFW 的 user-pointer
//   slot 可以稳定地保存一个 `Window*` 用于回调分发。消费者一律持有
//   `std::unique_ptr<Window>`，不会迁移底层对象。
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

    // 这里只前向声明；完整定义在 src/platform/glfw/Window.cpp 中，是
    // 私有实现细节。之所以提到 `public`，是因为该 TU 中的 GLFW 回调
    // shim 需要在文件作用域里引用 `Window::Impl*` 来分发事件，而不必
    // 走类内方法。持有一个不完整类型的指针并不算泄漏。
    struct Impl;

    static Result<std::unique_ptr<Window>, ResultCode> Create(const WindowDesc& desc);

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&)                 = delete;
    Window& operator=(Window&&)      = delete;

    ~Window();

    // 抽干进程内每一个 Window 上累积的平台事件。语义上等同
    // glfwPollEvents()；同一帧内重复调用是幂等的。
    static void PollEvents() noexcept;

    bool ShouldClose() const noexcept;
    void RequestClose() noexcept;

    std::uint32_t      GetWidth() const noexcept;
    std::uint32_t      GetHeight() const noexcept;
    const std::string& GetTitle() const noexcept;

    // 像素单位的 framebuffer 尺寸。HiDPI 显示器上和 GetWidth/Height
    // 不一致；渲染器应按这个尺寸去开 swap-chain。
    void GetFramebufferSize(std::uint32_t& width, std::uint32_t& height) const noexcept;

    void SetTitle(std::string_view title);
    void SetEventCallback(EventCallback callback);

    // 给渲染器 surface factory 用的 native handle。
    // * `GetNativeWindowHandle()` 在 Windows 上返回 HWND 转 void*。
    // * `GetNativeDisplayHandle()` 给 X11/Wayland 预留；Win32 上恒为
    //   nullptr。
    void* GetNativeWindowHandle() const noexcept;
    void* GetNativeDisplayHandle() const noexcept;

private:
    Window();

    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine::Platform

#endif  // ORANGE_ENGINE_PLATFORM_WINDOW_H
