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
    class VertexBuffer;
    class IndexBuffer;
    class VertexArray;

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
        std::unique_ptr<IWindow> mpWindow{ nullptr };
        std::unique_ptr<ImGuiLayer> mpImGuiLayer{ nullptr };
        
        std::shared_ptr<Shader> mpShader{ nullptr };
        std::shared_ptr<VertexArray> mpVertexArray{ nullptr };
        std::shared_ptr<Shader> mBlueShader { nullptr };
        std::shared_ptr<VertexArray> mBlueVertexArray { nullptr };
        
        bool mbRunning{ true };
        unsigned int mVertexArray;

        static Application* spInstance;
    };

    Application* CreateApplication();
}

#endif // APPLICATION_H