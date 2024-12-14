#include "pch.h"
#include "ImGuiLayer.h"
#include "OpenGL/ImGuiOpenGLRender.h"
#include "Application.h"

static ImGuiKey MapFromGLFWKey(int key)
{
    switch (key)
    {
        case GLFW_KEY_SPACE:          return ImGuiKey_Space;
        case GLFW_KEY_APOSTROPHE:     return ImGuiKey_Apostrophe;
        case GLFW_KEY_COMMA:          return ImGuiKey_Comma;
        case GLFW_KEY_MINUS:          return ImGuiKey_Minus;
        case GLFW_KEY_PERIOD:         return ImGuiKey_Period;
        case GLFW_KEY_SLASH:          return ImGuiKey_Slash;
        case GLFW_KEY_0:              return ImGuiKey_0;
        case GLFW_KEY_1:              return ImGuiKey_1;
        case GLFW_KEY_2:              return ImGuiKey_2;
        case GLFW_KEY_3:              return ImGuiKey_3;
        case GLFW_KEY_4:              return ImGuiKey_4;
        case GLFW_KEY_5:              return ImGuiKey_5;
        case GLFW_KEY_6:              return ImGuiKey_6;
        case GLFW_KEY_7:              return ImGuiKey_7;
        case GLFW_KEY_8:              return ImGuiKey_8;
        case GLFW_KEY_9:              return ImGuiKey_9;
        case GLFW_KEY_SEMICOLON:      return ImGuiKey_Semicolon;
        case GLFW_KEY_EQUAL:          return ImGuiKey_Equal;
        case GLFW_KEY_A:              return ImGuiKey_A;
        case GLFW_KEY_B:              return ImGuiKey_B;
        case GLFW_KEY_C:              return ImGuiKey_C;
        case GLFW_KEY_D:              return ImGuiKey_D;
        case GLFW_KEY_E:              return ImGuiKey_E;
        case GLFW_KEY_F:              return ImGuiKey_F;
        case GLFW_KEY_G:              return ImGuiKey_G;
        case GLFW_KEY_H:              return ImGuiKey_H;
        case GLFW_KEY_I:              return ImGuiKey_I;
        case GLFW_KEY_J:              return ImGuiKey_J;
        case GLFW_KEY_K:              return ImGuiKey_K;
        case GLFW_KEY_L:              return ImGuiKey_L;
        case GLFW_KEY_M:              return ImGuiKey_M;
        case GLFW_KEY_N:              return ImGuiKey_N;
        case GLFW_KEY_O:              return ImGuiKey_O;
        case GLFW_KEY_P:              return ImGuiKey_P;
        case GLFW_KEY_Q:              return ImGuiKey_Q;
        case GLFW_KEY_R:              return ImGuiKey_R;
        case GLFW_KEY_S:              return ImGuiKey_S;
        case GLFW_KEY_T:              return ImGuiKey_T;
        case GLFW_KEY_U:              return ImGuiKey_U;
        case GLFW_KEY_V:              return ImGuiKey_V;
        case GLFW_KEY_W:              return ImGuiKey_W;
        case GLFW_KEY_X:              return ImGuiKey_X;
        case GLFW_KEY_Y:              return ImGuiKey_Y;
        case GLFW_KEY_Z:              return ImGuiKey_Z;
        case GLFW_KEY_LEFT_BRACKET:   return ImGuiKey_LeftBracket;
        case GLFW_KEY_BACKSLASH:      return ImGuiKey_Backslash;
        case GLFW_KEY_RIGHT_BRACKET:  return ImGuiKey_RightBracket;
        case GLFW_KEY_GRAVE_ACCENT:   return ImGuiKey_GraveAccent;


            // Function keys
        case GLFW_KEY_ESCAPE:         return ImGuiKey_Escape;
        case GLFW_KEY_ENTER:          return ImGuiKey_Enter;
        case GLFW_KEY_TAB:            return ImGuiKey_Tab;
        case GLFW_KEY_BACKSPACE:      return ImGuiKey_Backspace;
        case GLFW_KEY_INSERT:         return ImGuiKey_Insert;
        case GLFW_KEY_DELETE:         return ImGuiKey_Delete;
        case GLFW_KEY_RIGHT:          return ImGuiKey_RightArrow;
        case GLFW_KEY_LEFT:           return ImGuiKey_LeftArrow;
        case GLFW_KEY_DOWN:           return ImGuiKey_DownArrow;
        case GLFW_KEY_UP:             return ImGuiKey_UpArrow;
        case GLFW_KEY_PAGE_UP:        return ImGuiKey_PageUp;
        case GLFW_KEY_PAGE_DOWN:      return ImGuiKey_PageDown;
        case GLFW_KEY_HOME:           return ImGuiKey_Home;
        case GLFW_KEY_END:            return ImGuiKey_End;

            // Modifier keys
        case GLFW_KEY_LEFT_SHIFT:     return ImGuiKey_LeftShift;
        case GLFW_KEY_LEFT_CONTROL:   return ImGuiKey_LeftCtrl;
        case GLFW_KEY_LEFT_ALT:       return ImGuiKey_LeftAlt;
        case GLFW_KEY_LEFT_SUPER:     return ImGuiKey_LeftSuper;
        case GLFW_KEY_RIGHT_SHIFT:    return ImGuiKey_RightShift;
        case GLFW_KEY_RIGHT_CONTROL:  return ImGuiKey_RightCtrl;
        case GLFW_KEY_RIGHT_ALT:      return ImGuiKey_RightAlt;
        case GLFW_KEY_RIGHT_SUPER:    return ImGuiKey_RightSuper;

            // Other special keys
        case GLFW_KEY_CAPS_LOCK:      return ImGuiKey_CapsLock;
        case GLFW_KEY_SCROLL_LOCK:    return ImGuiKey_ScrollLock;
        case GLFW_KEY_NUM_LOCK:       return ImGuiKey_NumLock;
        case GLFW_KEY_PRINT_SCREEN:   return ImGuiKey_PrintScreen;
        case GLFW_KEY_PAUSE:          return ImGuiKey_Pause;

            // Mouse buttons (if needed)
        case GLFW_MOUSE_BUTTON_1:     return ImGuiKey_MouseLeft;
        case GLFW_MOUSE_BUTTON_2:     return ImGuiKey_MouseRight;
        case GLFW_MOUSE_BUTTON_3:     return ImGuiKey_MouseMiddle;


            // Default return
        default:                      return ImGuiKey_None;
    }
}

namespace Orange
{
    ImGuiLayer::ImGuiLayer()
        : Layer("ImGuiLayer")
    {
    }

    ImGuiLayer::~ImGuiLayer()
    {
    }

    void ImGuiLayer::OnAttach()
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
        io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

        ImGui_ImplOpenGL3_Init("#version 410");
    }

    void ImGuiLayer::OnDetach()
    {
    }

    void ImGuiLayer::OnUpdate()
    {
        ImGuiIO& io = ImGui::GetIO();
        auto tpApp = Application::GetInstance();
        io.DisplaySize = ImVec2((float)tpApp->GetWindow().GetWidth(), (float)tpApp->GetWindow().GetHeight());
        float time = (float)glfwGetTime();
        io.DeltaTime = mTime > 0.0f ? (time - mTime) : (1.0f / 60.0f);
        mTime = time;


        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();


        static bool show = true;
        ImGui::ShowDemoWindow(&show);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void ImGuiLayer::OnEvent(Event& event)
    {
        EventDispatcher dispatcher(event);
        dispatcher.Dispatch<WindowResizeEvent>(ORANGE_BIND_EVENT_FN(ImGuiLayer::OnWindowResizeEvent));
        dispatcher.Dispatch<MouseButtonPressedEvent>(ORANGE_BIND_EVENT_FN(ImGuiLayer::OnMouseButtonPressEvent));
        dispatcher.Dispatch<MouseButtonReleasedEvent>(ORANGE_BIND_EVENT_FN(ImGuiLayer::OnMouseButtonReleaseEvent));
        dispatcher.Dispatch<MouseMoveEvent>(ORANGE_BIND_EVENT_FN(ImGuiLayer::OnMouseMovedEvent));
        dispatcher.Dispatch<MouseScrolledEvent>(ORANGE_BIND_EVENT_FN(ImGuiLayer::OnMouseScrolledEvent));
        dispatcher.Dispatch<KeyPressedEvent>(ORANGE_BIND_EVENT_FN(ImGuiLayer::OnKeyPressedEvent));
        dispatcher.Dispatch<KeyReleasedEvent>(ORANGE_BIND_EVENT_FN(ImGuiLayer::OnKeyReleasedEvent));
        dispatcher.Dispatch<KeyTypedEvent>(ORANGE_BIND_EVENT_FN(ImGuiLayer::OnKeyTypedEvent));

    }

    bool ImGuiLayer::OnMouseButtonPressEvent(MouseButtonPressedEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.MouseDown[e.GetButton()] = true;

        return false;
    }

    bool ImGuiLayer::OnMouseButtonReleaseEvent(MouseButtonReleasedEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.MouseDown[e.GetButton()] = false;

        return false;
    }

    bool ImGuiLayer::OnMouseMovedEvent(MouseMoveEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.MousePos = ImVec2(e.GetX(), e.GetY());

        return false;
    }

    bool ImGuiLayer::OnMouseScrolledEvent(MouseScrolledEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.MouseWheelH += e.GetXOffset();
        io.MouseWheel += e.GetYOffset();

        return false;
    }

    bool ImGuiLayer::OnKeyPressedEvent(KeyPressedEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(MapFromGLFWKey(e.GetKeyCode()), true);

        return false;
    }

    bool ImGuiLayer::OnKeyReleasedEvent(KeyReleasedEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(MapFromGLFWKey(e.GetKeyCode()), false);

        return false;
    }

    bool ImGuiLayer::OnKeyTypedEvent(KeyTypedEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (e.GetKeyCode() > 0 && e.GetKeyCode() < 0x10000)
        {
            io.AddInputCharacter(e.GetKeyCode());
        }

        return false;
    }

    bool ImGuiLayer::OnWindowResizeEvent(WindowResizeEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)e.GetWidth(), (float)e.GetHeight());
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        glViewport(0, 0, e.GetWidth(), e.GetHeight());

        return false;
    }
}