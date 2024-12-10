#ifndef APPLICATION_H
#define APPLICATION_H

#include "OrangeExport.h"
#include "IWindow.h"
#include "LayerStack.h"

class WindowCloseEvent;
class Layer;

namespace Orange
{
    class ORANGE_API Application
    {
    public:
        Application();
        virtual ~Application();

        void Run();

        void OnEvent(Event& e);

        void PushLayer(Layer* layer);
        void PopLayer(Layer* layer);

    private:
        bool OnWindowClose(WindowCloseEvent& e);

    private:
        LayerStack mLayerStack;
        std::unique_ptr<IWindow> mpWindow{ nullptr };
        bool mbRunning{ true };
    };

    Application* CreateApplication();
}

#endif // APPLICATION_H