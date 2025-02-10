#include "OpenGL/OpenGLPch.h"
#include "Input.h"
#include "Application.h"

namespace Orange
{
    bool Input::IsKeyPressed(int keycode)
    {
        auto tpWindow = static_cast<GLFWwindow*>(Application::GetInstance()->GetWindow().GetNativeWindow());
        auto state = glfwGetKey(tpWindow, keycode);

        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool Input::IsMouseButtonPressed(int button)
    {
        auto tpWindow = static_cast<GLFWwindow*>(Application::GetInstance()->GetWindow().GetNativeWindow());
        auto state = glfwGetMouseButton(tpWindow, button);
        return state == GLFW_PRESS;
    }

    std::pair<float, float> Input::GetMousePosition()
    {
        auto tpWindow = static_cast<GLFWwindow*>(Application::GetInstance()->GetWindow().GetNativeWindow());
        double xpos, ypos;
        glfwGetCursorPos(tpWindow, &xpos, &ypos);

        return { (float)xpos, (float)ypos };
    }

    float Input::GetMouseX()
    {
        auto [x, y] = GetMousePosition();
        return x;
    }

    float Input::GetMouseY()
    {
        auto [x, y] = GetMousePosition();
        return y;
    }
}