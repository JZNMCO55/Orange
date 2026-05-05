// ---------------------------------------------------------------------------
// Phase 1 / Task 06 — Platform::Window GLFW backend
//
// The single TU permitted to consume <GLFW/glfw3.h> in this engine.
// Other modules go through the public Platform::Window API.
//
// Lifecycle: a process-wide reference count gates glfwInit / glfwTerminate.
// Each successful Window::Create increments it; ~Window decrements. The
// counter sits in an anonymous namespace to keep symbol scope local to
// this TU.
// ---------------------------------------------------------------------------

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

// Process-wide GLFW init refcount. The plan calls for a "sInitialized
// counter" in an anonymous namespace; this is it. Single-threaded boot is
// the assumed contract for Phase 1.
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
    Window*               pOwner{nullptr};  // back-pointer for callback dispatch
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

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // Vulkan rendering owns the surface
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
    return nullptr;  // Win32 has no separate display handle.
}

}  // namespace Orange::Engine::Platform
