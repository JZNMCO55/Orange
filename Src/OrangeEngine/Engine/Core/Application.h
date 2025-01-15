#ifndef APPLICATION_H
#define APPLICATION_H

#include "OrangeExport.h"
#include "IWindow.h"
#include "LayerStack.h"

namespace Orange
{
    class WindowCloseEvent;
    class WindowResizeEvent;
    class Layer;
    class ImGuiLayer;
    class Shader;
    class VertexBuffer;
    class IndexBuffer;
    class VertexArray;
    class OrthographicCamera;

    class ORANGE_API Application
    {
    public:
        virtual ~Application();

        void Run();

        void OnEvent(Event& e);

        void PushLayer(Layer* layer);
        void PushOverlay(Layer* layer);

        inline IWindow& GetWindow() { return *mpWindow; }

        void Close();
        
        static Application* GetInstance();
    protected:
        Application();

    private:
        bool OnWindowClose(WindowCloseEvent& e);
        bool OnWindowResize(WindowResizeEvent& e);
        Application(const Application&) = delete;
        Application(const Application&&) = delete;
        Application& operator=(const Application&) = delete;

    private:
        LayerStack mLayerStack;
        std::unique_ptr<IWindow> mpWindow{ nullptr };
        std::unique_ptr<ImGuiLayer> mpImGuiLayer{ nullptr };
        
        float mLastFrameTime{ 0.0f };
        bool mbRunning{ true };
        bool mbMinimized{ false };

        static Application* spInstance;
    };

    Application* CreateApplication();
}

#endif // APPLICATION_H