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
    //public:
    //    enum class ImGuiSkin
    //    {
    //        Dark,
    //        Light
    //        // ...
    //    };
    public:
        ImGuiLayer();
        ~ImGuiLayer();

        virtual void OnAttach() override;
        virtual void OnDetach() override;
        virtual void OnEvent(Event& event) override;

        void Begin();
        void End();

        void BlockEvents(bool block) { mBlockEvents = block; }

        // Todo: Set ImGui Skin
        // void SetImGuiSkin(ImGuiSkin skin);

    private:
        void SetDarkThemeColors();
    private:
        bool mBlockEvents = true;
        float mTime = 0.0f;
    };
}


#endif // !IMGUI_LAYER_H
