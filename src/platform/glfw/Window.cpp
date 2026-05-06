// Platform::Window —— GLFW backend
//
// 整个引擎中唯一允许 include <GLFW/glfw3.h> 的 TU。其他模块走 public
// Platform::Window API，看不到任何 GLFW 类型。
//
// 生命周期：进程级的 init 计数器控制 glfwInit / glfwTerminate。每次
// Window::Create 成功 +1，~Window -1。计数器放在匿名 namespace 里，
// 把符号作用域限制在本 TU 内。当前的契约假设引擎启动是单线程的。

#define GLFW_INCLUDE_NONE

#include "orange/engine/platform/Window.h"
#include "orange/engine/core/Log.h"

#include <GLFW/glfw3.h>

#if defined(_WIN32)
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>
#endif

#include <atomic>
#include <utility>

namespace Orange::Engine::Platform
{
namespace
{

// 进程级 GLFW init 引用计数。设计文档要求一个匿名 namespace 中的
// "sInitialized 计数器"，就是这里。当前实现假设启动过程是单线程的。
int sGlfwInitCount = 0;

bool EnsureGlfwInit() noexcept
{
    if (sGlfwInitCount == 0)
    {
        glfwSetErrorCallback([](int code, const char* description) {
            ORANGE_LOG_ERROR("GLFW error {}: {}", code, description ? description : "(null)");
        });
        if (glfwInit() != GLFW_TRUE)
        {
            return false;
        }
    }
    ++sGlfwInitCount;
    return true;
}

void ReleaseGlfwInit() noexcept
{
    if (sGlfwInitCount == 0)
    {
        return;
    }
    --sGlfwInitCount;
    if (sGlfwInitCount == 0)
    {
        glfwTerminate();
    }
}

KeyAction TranslateAction(int action) noexcept
{
    switch (action)
    {
        case GLFW_PRESS:   return KeyAction::Press;
        case GLFW_RELEASE: return KeyAction::Release;
        case GLFW_REPEAT:  return KeyAction::Repeat;
    }
    return KeyAction::Press;
}

MouseButtonId TranslateMouseButton(int button) noexcept
{
    switch (button)
    {
        case GLFW_MOUSE_BUTTON_LEFT:   return MouseButtonId::Left;
        case GLFW_MOUSE_BUTTON_RIGHT:  return MouseButtonId::Right;
        case GLFW_MOUSE_BUTTON_MIDDLE: return MouseButtonId::Middle;
    }
    return MouseButtonId::Other;
}

}  // namespace

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct Window::Impl
{
    GLFWwindow*           pHandle{nullptr};
    std::string           title;
    std::uint32_t         width{0};
    std::uint32_t         height{0};
    Window::EventCallback callback;
    Window*               pOwner{nullptr};  // 回调分发用的反向指针
};

namespace
{

Window::Impl* ImplFrom(GLFWwindow* handle) noexcept
{
    return static_cast<Window::Impl*>(glfwGetWindowUserPointer(handle));
}

void Dispatch(Window::Impl* impl, const WindowEvent& event)
{
    if (impl && impl->callback)
    {
        impl->callback(event);
    }
}

void OnClose(GLFWwindow* handle)
{
    Dispatch(ImplFrom(handle), WindowCloseEvent{});
}

void OnSize(GLFWwindow* handle, int width, int height)
{
    auto* impl = ImplFrom(handle);
    if (!impl)
    {
        return;
    }
    impl->width  = (width  > 0) ? static_cast<std::uint32_t>(width)  : 0u;
    impl->height = (height > 0) ? static_cast<std::uint32_t>(height) : 0u;
    Dispatch(impl, WindowResizeEvent{impl->width, impl->height});
}

void OnFocus(GLFWwindow* handle, int focused)
{
    Dispatch(ImplFrom(handle), WindowFocusEvent{focused == GLFW_TRUE});
}

void OnKey(GLFWwindow* handle, int key, int scancode, int action, int mods)
{
    Dispatch(ImplFrom(handle),
             KeyEvent{key, scancode, TranslateAction(action), mods});
}

void OnChar(GLFWwindow* handle, unsigned int codepoint)
{
    Dispatch(ImplFrom(handle), CharEvent{codepoint});
}

void OnMouseButton(GLFWwindow* handle, int button, int action, int mods)
{
    Dispatch(ImplFrom(handle),
             MouseButtonEvent{TranslateMouseButton(button), button,
                              TranslateAction(action), mods});
}

void OnMouseMove(GLFWwindow* handle, double x, double y)
{
    Dispatch(ImplFrom(handle), MouseMoveEvent{x, y});
}

void OnScroll(GLFWwindow* handle, double xOffset, double yOffset)
{
    Dispatch(ImplFrom(handle), ScrollEvent{xOffset, yOffset});
}

}  // namespace

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

Window::Window() : mpImpl(std::make_unique<Impl>())
{
    mpImpl->pOwner = this;
}

Window::~Window()
{
    if (mpImpl && mpImpl->pHandle)
    {
        glfwDestroyWindow(mpImpl->pHandle);
        mpImpl->pHandle = nullptr;
        ReleaseGlfwInit();
    }
}

Result<std::unique_ptr<Window>, ResultCode> Window::Create(const WindowDesc& desc)
{
    if (desc.width == 0 || desc.height == 0)
    {
        ORANGE_LOG_ERROR("Window::Create: width and height must be non-zero (got {}x{})",
                         desc.width, desc.height);
        return ResultCode::InvalidArgument;
    }

    if (!EnsureGlfwInit())
    {
        ORANGE_LOG_ERROR("Window::Create: glfwInit failed");
        return ResultCode::NotInitialized;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // Vulkan 由渲染器自己拿 surface
    glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE,   desc.visible   ? GLFW_TRUE : GLFW_FALSE);

    auto window = std::unique_ptr<Window>(new Window{});
    auto& impl  = *window->mpImpl;
    impl.title  = desc.title;
    impl.width  = desc.width;
    impl.height = desc.height;
    impl.pHandle = glfwCreateWindow(static_cast<int>(desc.width),
                                    static_cast<int>(desc.height),
                                    impl.title.c_str(),
                                    nullptr,
                                    nullptr);
    if (!impl.pHandle)
    {
        ORANGE_LOG_ERROR("Window::Create: glfwCreateWindow returned null");
        ReleaseGlfwInit();
        return ResultCode::InternalError;
    }

    glfwSetWindowUserPointer(impl.pHandle, &impl);

    glfwSetWindowCloseCallback(impl.pHandle,    &OnClose);
    glfwSetWindowSizeCallback(impl.pHandle,     &OnSize);
    glfwSetWindowFocusCallback(impl.pHandle,    &OnFocus);
    glfwSetKeyCallback(impl.pHandle,            &OnKey);
    glfwSetCharCallback(impl.pHandle,           &OnChar);
    glfwSetMouseButtonCallback(impl.pHandle,    &OnMouseButton);
    glfwSetCursorPosCallback(impl.pHandle,      &OnMouseMove);
    glfwSetScrollCallback(impl.pHandle,         &OnScroll);

    return window;
}

void Window::PollEvents() noexcept
{
    if (sGlfwInitCount > 0)
    {
        glfwPollEvents();
    }
}

bool Window::ShouldClose() const noexcept
{
    return mpImpl->pHandle != nullptr
        && glfwWindowShouldClose(mpImpl->pHandle) == GLFW_TRUE;
}

void Window::RequestClose() noexcept
{
    if (mpImpl->pHandle)
    {
        glfwSetWindowShouldClose(mpImpl->pHandle, GLFW_TRUE);
    }
}

std::uint32_t Window::GetWidth() const noexcept
{
    return mpImpl->width;
}

std::uint32_t Window::GetHeight() const noexcept
{
    return mpImpl->height;
}

const std::string& Window::GetTitle() const noexcept
{
    return mpImpl->title;
}

void Window::GetFramebufferSize(std::uint32_t& width, std::uint32_t& height) const noexcept
{
    width  = 0;
    height = 0;
    if (!mpImpl->pHandle)
    {
        return;
    }
    int fbWidth  = 0;
    int fbHeight = 0;
    glfwGetFramebufferSize(mpImpl->pHandle, &fbWidth, &fbHeight);
    width  = (fbWidth  > 0) ? static_cast<std::uint32_t>(fbWidth)  : 0u;
    height = (fbHeight > 0) ? static_cast<std::uint32_t>(fbHeight) : 0u;
}

void Window::SetTitle(std::string_view title)
{
    mpImpl->title = std::string{title};
    if (mpImpl->pHandle)
    {
        glfwSetWindowTitle(mpImpl->pHandle, mpImpl->title.c_str());
    }
}

void Window::SetEventCallback(EventCallback callback)
{
    mpImpl->callback = std::move(callback);
}

void* Window::GetNativeWindowHandle() const noexcept
{
#if defined(_WIN32)
    return mpImpl->pHandle ? static_cast<void*>(glfwGetWin32Window(mpImpl->pHandle)) : nullptr;
#else
    return nullptr;
#endif
}

void* Window::GetNativeDisplayHandle() const noexcept
{
    return nullptr;  // Win32 平台没有独立的 display handle
}

}  // namespace Orange::Engine::Platform
