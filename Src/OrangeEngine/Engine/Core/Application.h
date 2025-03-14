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

    struct ApplicationCommandLineArgs
    {
        int Count = 0;
        char** Args = nullptr;

        const char* operator[](int index) const
        {
            ORANGE_CORE_ASSERT(index < Count, "");
            return Args[index];
        }
    };

    class ORANGE_API Application
    {
    public:
        Application(const std::string& name = "Orange App", ApplicationCommandLineArgs args = ApplicationCommandLineArgs());
        virtual ~Application();

        void Run();

        void OnEvent(Event& e);

        void PushLayer(const Ref<Layer>& layer);
        void PushOverlay(const Ref<Layer>& layer);

        inline IWindow& GetWindow() { return *mpWindow; }
        inline Ref<ImGuiLayer> GetImGuiLayer() { return mpImGuiLayer; }

        void Close();
        
        static Application* GetInstance();

        const ApplicationCommandLineArgs GetCommandLineArgs() const { return mCommandLineArgs; };
    protected:

    private:
        bool OnWindowClose(WindowCloseEvent& e);
        bool OnWindowResize(WindowResizeEvent& e);
        Application(const Application&) = delete;
        Application(const Application&&) = delete;
        Application& operator=(const Application&) = delete;

    private:
        ApplicationCommandLineArgs mCommandLineArgs;
        LayerStack mLayerStack;
        std::unique_ptr<IWindow> mpWindow{ nullptr };
        Ref<ImGuiLayer> mpImGuiLayer{ nullptr };
        
        float mLastFrameTime{ 0.0f };
        bool mbRunning{ true };
        bool mbMinimized{ false };

        static Application* spInstance;
    };

    Application* CreateApplication(ApplicationCommandLineArgs args);
}

#endif // APPLICATION_H