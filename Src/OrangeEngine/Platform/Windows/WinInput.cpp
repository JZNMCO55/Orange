#include "pch.h"
#include "WinInput.h"

#include "Application.h"

namespace Orange
{
    bool WinInput::IsKeyPressedImpl(int keycode)
    {
        auto tpWindow = static_cast<GLFWwindow*>(Application::GetInstance()->GetWindow().GetNativeWindow());
        auto state = glfwGetKey(tpWindow, keycode);

        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }
}