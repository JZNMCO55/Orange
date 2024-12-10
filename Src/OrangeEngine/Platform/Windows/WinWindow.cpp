#include "pch.h"
#include "WinWindow.h"

#include "ApplicationEvent.h"
#include "KeyEvent.h"
#include "MouseEvent.h" 

namespace Orange
{
    static void GLFWErrorCallback(int error, const char* description)
    {
        ORANGE_LOG_ERROR("GLFW Error ({0}): {1}", error, description);
    }

    WinWindow::WinWindow(const WindowProps& data)
    {
        Init(data);
    }

    WinWindow::~WinWindow()
    {
        Shutdown();
    }

    void WinWindow::OnUpdate()
    {
        // 这里可以添加更新窗口的代码
        glfwPollEvents();
        glfwSwapBuffers(mpWindow);
    }

    void WinWindow::SetVSync(bool enabled)
    {
        if (mpWindow != nullptr)
        {
            glfwSwapInterval(enabled ? 1 : 0);
            mData.mVSync = enabled;
        }
    }

    std::unique_ptr<IWindow> WinWindow::Create(const WindowProps& props)
    {
        return std::make_unique<WinWindow>(props);
    }

    void WinWindow::Init(const WindowProps& props)
    {
        mData.mTitle = props.mTitle;
        mData.mWidth = props.mWidth;
        mData.mHeight = props.mHeight;
        static bool sbGLFWInitialized = false;

        if (!sbGLFWInitialized)
        {
            int success = glfwInit();
            ORANGE_CORE_ASSERT(success, "Failed to initialize GLFW");
            sbGLFWInitialized = true;
        }

        mpWindow = glfwCreateWindow((int)props.mWidth, (int)props.mHeight, props.mTitle.c_str(), nullptr, nullptr);
        glfwMakeContextCurrent(mpWindow);
        glfwSetWindowUserPointer(mpWindow, &mData);
        SetVSync(true);

        // Set GLFW callbacks
        // WindowResizeEvent
        glfwSetWindowSizeCallback(mpWindow, [](GLFWwindow* tpWindow, int width, int height)
        {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(tpWindow);
            data.mWidth = width;
            data.mHeight = height;

            WindowResizeEvent event(width, height);
            data.mEventCallback(event);
        });

        // WindowCloseEvent
        glfwSetWindowCloseCallback(mpWindow, [](GLFWwindow* tpWindow)
        {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(tpWindow);
            WindowCloseEvent event;
            data.mEventCallback(event);
        });

        // KeyEvent
        glfwSetKeyCallback(mpWindow, [](GLFWwindow* tpWindow, int key, int scancode, int action, int mods)
        {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(tpWindow);

            switch (action)
            {
            case GLFW_PRESS:
            {
                KeyPressedEvent event(key, 0);
                data.mEventCallback(event);
                break;
            }
            case GLFW_RELEASE:
            {
                KeyReleasedEvent event(key);
                data.mEventCallback(event);
                break;
            }
            case GLFW_REPEAT:
            {
                KeyPressedEvent event(key, 1);
                data.mEventCallback(event);
                break;
            }
            }
        });

        // MouseButtonEvent
        glfwSetMouseButtonCallback(mpWindow, [](GLFWwindow* tpWindow, int button, int action, int mods)
        {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(tpWindow);

            switch (action)
            {
            case GLFW_PRESS:
            {
                MouseButtonPressedEvent event(button);
                data.mEventCallback(event);
                break;
            }
            case GLFW_RELEASE:
            {
                MouseButtonReleasedEvent event(button);
                data.mEventCallback(event);
                break;
            }
            }
        });

        // MouseScrollEvent
        glfwSetScrollCallback(mpWindow, [](GLFWwindow* tpWindow, double xOffset, double yOffset)
        {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(tpWindow);

            MouseScrolledEvent event((float)xOffset, (float)yOffset);
            data.mEventCallback(event);
        });

        // MouseMoveEvent
        glfwSetCursorPosCallback(mpWindow, [](GLFWwindow* tpWindow, double xPos, double yPos)
        {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(tpWindow);

            MouseMoveEvent event((float)xPos, (float)yPos);
            data.mEventCallback(event);
        });
    }

    void WinWindow::Shutdown()
    {
        if (mpWindow)
        {
            glfwDestroyWindow(mpWindow);
            mpWindow = nullptr;
        }
        glfwTerminate();
    }
}