#ifndef LAYER_H
#define LAYER_H

#include "OrangeExport.h"
#include "Event.h"
#include "Timestep.h"

namespace Orange
{
    class ORANGE_API Layer
    {
    public:
        Layer(const std::string& name = "Layer");
        virtual ~Layer();

        virtual void OnAttach() {}
        virtual void OnDetach() {}
        virtual void OnUpdate(Timestep ts) {}
        virtual void OnEvent(Event& event) {}

        virtual void OnImGuiRender() {}

        inline const std::string& GetName() const { return mDebugName; }
    protected:
        //virtual void OnMouseButtonPressedEvent(MouseButtonPressedEvent& event) {}
    protected:
        std::string mDebugName;
    };
}

#endif