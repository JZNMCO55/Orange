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
            virtual void OnEvent(Event& e);

            void PushLayer(const std::shared_ptr<Layer>& layer);
            void PushOverlay(const std::shared_ptr<Layer>& overlay);

        private:
            bool OnWindowClose(WindowCloseEvent& e);
            bool OnWindowResize(WindowResizeEvent& e);

        private:
            LayerStack m_LayerStack;
            std::unique_ptr<Window> m_window;
            bool m_running = true;
        };
    }
}

#endif