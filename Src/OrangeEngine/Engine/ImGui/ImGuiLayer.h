#ifndef IMGUI_LAYER_H
#define IMGUI_LAYER_H

#include "OrangeExport.h"
#include "Layer.h"
#include "ApplicationEvent.h"
#include "KeyEvent.h"
#include "MouseEvent.h"

namespace Orange
{
    class ORANGE_API ImGuiLayer : public Layer
    {
    public:
        ImGuiLayer();
        ~ImGuiLayer();

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate() override;
        void OnEvent(Event& event) override;
    private:
        bool OnMouseButtonPressEvent(MouseButtonPressedEvent& e);

        bool OnMouseButtonReleaseEvent(MouseButtonReleasedEvent& e);

        bool OnMouseMovedEvent(MouseMoveEvent& e);

        bool OnMouseScrolledEvent(MouseScrolledEvent& e);

        bool OnKeyPressedEvent(KeyPressedEvent& e);

        bool OnKeyReleasedEvent(KeyReleasedEvent& e);

        bool OnKeyTypedEvent(KeyTypedEvent& e);

        bool OnWindowResizeEvent(WindowResizeEvent& e);

    private:
        float mTime = 0.0f;
    };
}


#endif // !
