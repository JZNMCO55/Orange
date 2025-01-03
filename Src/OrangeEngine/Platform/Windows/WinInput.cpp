#include "OpenGL/OpenGLPch.h"
#include "Application.h"
#include "WinInput.h"

namespace Orange
{
    Input* Input::spInstance = new WinInput();

    bool WinInput::IsKeyPressedImpl(int keycode)
    {
        auto tpWindow = static_cast<GLFWwindow*>(Application::GetInstance()->GetWindow().GetNativeWindow());
        auto state = glfwGetKey(tpWindow, keycode);

        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool WinInput::IsMouseButtonPressedImpl(int button)
    {
        auto tpWindow = static_cast<GLFWwindow*>(Application::GetInstance()->GetWindow().GetNativeWindow());
        auto state = glfwGetMouseButton(tpWindow, button);
        return state == GLFW_PRESS;
    }

    std::pair<float, float> WinInput::GetMousePositionImpl()
    {
        auto tpWindow = static_cast<GLFWwindow*>(Application::GetInstance()->GetWindow().GetNativeWindow());
        double xpos, ypos;
        glfwGetCursorPos(tpWindow, &xpos, &ypos);

        return { (float)xpos, (float)ypos };
    }

    float WinInput::GetMouseXImpl()
    {
        auto [x, y] = GetMousePositionImpl();
        return x;
    }

    float WinInput::GetMouseYImpl()
    {
        auto [x, y] = GetMousePositionImpl();
        return y;
    }
}