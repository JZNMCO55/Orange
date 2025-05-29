#include "Window.h"
#include "../Event/Events.h"
#include <GLFW/glfw3.h>
#include <iostream>

namespace Orange
{
    namespace Core
    {
        struct Window::WindowImp
        {
            GLFWwindow *window = nullptr;
            EventCallbackFn eventCallback;

            WindowImp(const WindowProps &props)
            {
                // 初始化GLFW
                if (!glfwInit())
                {
                    std::cerr << "Failed to initialize GLFW" << std::endl;
                    return;
                }

                // 设置GLFW窗口提示
                glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // 不使用OpenGL上下文
                glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

                // 创建窗口
                window = glfwCreateWindow(
                    static_cast<int>(props.width),
                    static_cast<int>(props.height),
                    props.title.c_str(),
                    nullptr,
                    nullptr);

                if (!window)
                {
                    std::cerr << "Failed to create GLFW window" << std::endl;
                    glfwTerminate();
                    return;
                }

                // 设置用户指针，用于回调函数中访问WindowImp
                glfwSetWindowUserPointer(window, this);

                // 设置窗口回调函数
                SetupCallbacks();
            }

            virtual ~WindowImp()
            {
                if (window)
                {
                    glfwDestroyWindow(window);
                    window = nullptr;
                }
                glfwTerminate();
            }

            void SetupCallbacks()
            {
                // 窗口关闭回调
                glfwSetWindowCloseCallback(window, [](GLFWwindow *window)
                                           {
                    WindowImp* windowImp = static_cast<WindowImp*>(glfwGetWindowUserPointer(window));
                    if (windowImp && windowImp->eventCallback)
                    {
                        WindowCloseEvent event;
                        windowImp->eventCallback(event);
                    } });

                // 窗口大小改变回调
                glfwSetWindowSizeCallback(window, [](GLFWwindow *window, int width, int height)
                                          {
                    WindowImp* windowImp = static_cast<WindowImp*>(glfwGetWindowUserPointer(window));
                    if (windowImp && windowImp->eventCallback)
                    {
                        WindowResizeEvent event(width, height);
                        windowImp->eventCallback(event);
                    } });

                // 键盘输入回调
                glfwSetKeyCallback(window, [](GLFWwindow *window, int key, int scancode, int action, int mods)
                                   {
                    WindowImp* windowImp = static_cast<WindowImp*>(glfwGetWindowUserPointer(window));
                    if (windowImp && windowImp->eventCallback)
                    {
                        switch (action)
                        {
                            case GLFW_PRESS:
                            {
                                KeyPressedEvent event(key, false);
                                windowImp->eventCallback(event);
                                break;
                            }
                            case GLFW_RELEASE:
                            {
                                KeyReleasedEvent event(key);
                                windowImp->eventCallback(event);
                                break;
                            }
                            case GLFW_REPEAT:
                            {
                                KeyPressedEvent event(key, true);
                                windowImp->eventCallback(event);
                                break;
                            }
                        }
                    } });

                // 字符输入回调
                glfwSetCharCallback(window, [](GLFWwindow* window, unsigned int keycode)
                                   {
                    WindowImp* windowImp = static_cast<WindowImp*>(glfwGetWindowUserPointer(window));
                    if (windowImp && windowImp->eventCallback)
                    {
                        KeyTypedEvent event(keycode);
                        windowImp->eventCallback(event);
                    } });

                // 鼠标按钮回调
                glfwSetMouseButtonCallback(window, [](GLFWwindow *window, int button, int action, int mods)
                                           {
                    WindowImp* windowImp = static_cast<WindowImp*>(glfwGetWindowUserPointer(window));
                    if (windowImp && windowImp->eventCallback)
                    {
                        switch (action)
                        {
                            case GLFW_PRESS:
                            {
                                MouseButtonPressedEvent event(button);
                                windowImp->eventCallback(event);
                                break;
                            }
                            case GLFW_RELEASE:
                            {
                                MouseButtonReleasedEvent event(button);
                                windowImp->eventCallback(event);
                                break;
                            }
                        }
                    } });

                // 鼠标移动回调
                glfwSetCursorPosCallback(window, [](GLFWwindow *window, double xpos, double ypos)
                                         {
                    WindowImp* windowImp = static_cast<WindowImp*>(glfwGetWindowUserPointer(window));
                    if (windowImp && windowImp->eventCallback)
                    {
                        MouseMovedEvent event(static_cast<float>(xpos), static_cast<float>(ypos));
                        windowImp->eventCallback(event);
                    } });

                // 鼠标滚轮回调
                glfwSetScrollCallback(window, [](GLFWwindow *window, double xoffset, double yoffset)
                                      {
                    WindowImp* windowImp = static_cast<WindowImp*>(glfwGetWindowUserPointer(window));
                    if (windowImp && windowImp->eventCallback)
                    {
                        MouseScrolledEvent event(static_cast<float>(xoffset), static_cast<float>(yoffset));
                        windowImp->eventCallback(event);
                    } });

                // 窗口焦点回调
                glfwSetWindowFocusCallback(window, [](GLFWwindow* window, int focused)
                                          {
                    WindowImp* windowImp = static_cast<WindowImp*>(glfwGetWindowUserPointer(window));
                    if (windowImp && windowImp->eventCallback)
                    {
                        if (focused)
                        {
                            WindowFocusEvent event;
                            windowImp->eventCallback(event);
                        }
                        else
                        {
                            WindowLostFocusEvent event;
                            windowImp->eventCallback(event);
                        }
                    } });

                // 窗口位置回调
                glfwSetWindowPosCallback(window, [](GLFWwindow* window, int xpos, int ypos)
                                        {
                    WindowImp* windowImp = static_cast<WindowImp*>(glfwGetWindowUserPointer(window));
                    if (windowImp && windowImp->eventCallback)
                    {
                        WindowMovedEvent event(xpos, ypos);
                        windowImp->eventCallback(event);
                    } });
            }
        };

        Window::Window(const WindowProps &props)
            : m_windowImp(std::make_unique<WindowImp>(props)), m_props(props)
        {
        }

        Window::~Window() = default;

        void Window::OnUpdate()
        {
            if (m_windowImp && m_windowImp->window)
            {
                glfwPollEvents();
                glfwSwapBuffers(m_windowImp->window);
            }
        }

        void Window::SetEventCallback(const EventCallbackFn &callback)
        {
            if (m_windowImp)
            {
                m_windowImp->eventCallback = callback;
            }
        }

        void *Window::GetNativeWindow() const
        {
            return m_windowImp ? m_windowImp->window : nullptr;
        }

        bool Window::ShouldClose() const
        {
            return m_windowImp && m_windowImp->window ? glfwWindowShouldClose(m_windowImp->window) : true;
        }
    }
}
