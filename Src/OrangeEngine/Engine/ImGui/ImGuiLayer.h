#ifndef IMGUI_LAYER_H
#define IMGUI_LAYER_H

#include "OrangeExport.h"
#include "Layer.h"

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
        float mTime = 0.0f;
    };
}


#endif // !
