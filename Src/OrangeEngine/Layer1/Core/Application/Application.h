#ifndef ORANGE_APPLICATION_H
#define ORANGE_APPLICATION_H

#include <memory>
#include "LayerStack.h"

// 前向声明
namespace Orange
{
    namespace Core
    {
        class Event;
        class Window;
        class WindowCloseEvent;
        class WindowResizeEvent;
    }

    namespace Graphics
    {
        class IGraphicsSystem;
    }
}

namespace Orange
{
    namespace Core
    {
        class Application
        {
        public:
            Application();
            virtual ~Application();

            void Run();
            void Close();

            // 事件处理函数
            virtual void OnEvent(Event &e);

            void PushLayer(const std::shared_ptr<Layer> &layer);
            void PushOverlay(const std::shared_ptr<Layer> &overlay);

            // 访问器接口
            Graphics::IGraphicsSystem *GetGraphicsSystem() const { return m_graphicsSystem.get(); }
            Window *GetWindow() const { return m_window.get(); }

            // 单例访问
            static Application &GetInstance() { return *s_instance; }

        private:
            bool OnWindowClose(WindowCloseEvent &e);
            bool OnWindowResize(WindowResizeEvent &e);
            bool InitializeGraphicsSystem();

        private:
            LayerStack m_LayerStack;
            std::unique_ptr<Window> m_window;
            std::unique_ptr<Graphics::IGraphicsSystem> m_graphicsSystem;
            bool m_running = true;

            // 单例实例
            static Application *s_instance;
        };
    }
}

#endif