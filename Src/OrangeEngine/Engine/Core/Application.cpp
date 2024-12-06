#include "Application.h"
#include "Windows/WinWindow.h"
#include "GLFW/glfw3.h"
namespace Orange
{
    Application::Application()
    {
        mpWindow = WinWindow::Create();
    }

    Application::~Application()
    {
    }

    void Application::Run()
    {
        while (mbRunning)
        {
            glClearColor(1.0f, 0.f, 0.f, 1.0f);
            mpWindow->OnUpdate();
        }
    }
}