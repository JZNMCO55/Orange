#ifndef APPLICATION_H
#define APPLICATION_H

#include "OrangeExport.h"
#include "IWindow.h"
#include "LayerStack.h"

namespace Orange
{
    class WindowCloseEvent;
    class Layer;
    class ImGuiLayer;
    class Shader;

    class ORANGE_API Application
    {
    public:
        virtual ~Application();

        void Run();

        void OnEvent(Event& e);

        void PushLayer(Layer* layer);
        void PushOverlay(Layer* layer);

        inline IWindow& GetWindow() { return *mpWindow; }
        
        static Application* GetInstance();
    protected:
        Application();

    private:
        bool OnWindowClose(WindowCloseEvent& e);
        Application(const Application&) = delete;
        Application(const Application&&) = delete;
        Application& operator=(const Application&) = delete;

    private:
        LayerStack mLayerStack;
        std::unique_ptr<Shader> mpShader{ nullptr };
        std::unique_ptr<IWindow> mpWindow{ nullptr };
        std::unique_ptr<ImGuiLayer> mpImGuiLayer{ nullptr };
        bool mbRunning{ true };
        static Application* spInstance;
    };

    Application* CreateApplication();
}

#endif // APPLICATION_H