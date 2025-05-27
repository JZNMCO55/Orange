#include "Window.h"
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
                        // 这里可以定义具体的事件结构体，暂时传递nullptr
                        windowImp->eventCallback(nullptr);
                    } });

                // 窗口大小改变回调
                glfwSetWindowSizeCallback(window, [](GLFWwindow *window, int width, int height)
                                          {
                    WindowImp* windowImp = static_cast<WindowImp*>(glfwGetWindowUserPointer(window));
                    if (windowImp && windowImp->eventCallback)
                    {
                        // 这里可以定义具体的事件结构体，暂时传递nullptr
                        windowImp->eventCallback(nullptr);
                    } });

                // 键盘输入回调
                glfwSetKeyCallback(window, [](GLFWwindow *window, int key, int scancode, int action, int mods)
                                   {
                    WindowImp* windowImp = static_cast<WindowImp*>(glfwGetWindowUserPointer(window));
                    if (windowImp && windowImp->eventCallback)
                    {
                        // 这里可以定义具体的事件结构体，暂时传递nullptr
                        windowImp->eventCallback(nullptr);
                    } });

                // 鼠标按钮回调
                glfwSetMouseButtonCallback(window, [](GLFWwindow *window, int button, int action, int mods)
                                           {
                    WindowImp* windowImp = static_cast<WindowImp*>(glfwGetWindowUserPointer(window));
                    if (windowImp && windowImp->eventCallback)
                    {
                        // 这里可以定义具体的事件结构体，暂时传递nullptr
                        windowImp->eventCallback(nullptr);
                    } });

                // 鼠标移动回调
                glfwSetCursorPosCallback(window, [](GLFWwindow *window, double xpos, double ypos)
                                         {
                    WindowImp* windowImp = static_cast<WindowImp*>(glfwGetWindowUserPointer(window));
                    if (windowImp && windowImp->eventCallback)
                    {
                        // 这里可以定义具体的事件结构体，暂时传递nullptr
                        windowImp->eventCallback(nullptr);
                    } });

                // 鼠标滚轮回调
                glfwSetScrollCallback(window, [](GLFWwindow *window, double xoffset, double yoffset)
                                      {
                    WindowImp* windowImp = static_cast<WindowImp*>(glfwGetWindowUserPointer(window));
                    if (windowImp && windowImp->eventCallback)
                    {
                        // 这里可以定义具体的事件结构体，暂时传递nullptr
                        windowImp->eventCallback(nullptr);
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
