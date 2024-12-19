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

        virtual void OnAttach() override;
        virtual void OnDetach() override;
        virtual void OnImGuiRender() override;

        void Begin();
        void End();
    private:
        float mTime = 0.0f;
    };
}


#endif // !
