#include "pch.h"
#include "WinWindow.h"

namespace Orange
{
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
        SetVSync(true);
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