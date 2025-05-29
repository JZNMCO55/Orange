#include "Application.h"
#include "Window.h"
#include "../Event/Events.h"
#include <iostream>

namespace Orange
{
    namespace Core
    {
        Application::Application()
        {
            // 创建窗口
            WindowProps props("Orange Engine", 1280, 720);
            m_window = std::make_unique<Window>(props);

            // 设置窗口事件回调
            m_window->SetEventCallback([this](Event& event)
                                       {
                this->OnEvent(event); });

            std::cout << "Application initialized successfully!" << std::endl;
        }

        Application::~Application()
        {
            std::cout << "Application destroyed." << std::endl;
        }

        void Application::Run()
        {
            std::cout << "Starting application main loop..." << std::endl;

            while (m_running && !m_window->ShouldClose())
            {
                // 更新所有层级
                for (auto& layer : m_LayerStack)
                {
                    layer->OnUpdate();
                }

                // 更新窗口
                m_window->OnUpdate();
            }

            std::cout << "Application main loop ended." << std::endl;
        }

        void Application::Close()
        {
            m_running = false;
            std::cout << "Application close requested." << std::endl;
        }

        void Application::OnEvent(Event& e)
        {
            EventDispatcher dispatcher(e);
            
            // 分发窗口关闭事件
            dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& event) -> bool
            {
                return OnWindowClose(event);
            });

            // 分发窗口大小改变事件
            dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& event) -> bool
            {
                return OnWindowResize(event);
            });

            // 将事件传递给层级（从后往前，overlay优先处理）
            for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
            {
                if (e.Handled)
                    break;
                (*it)->OnEvent(e);
            }
        }

        void Application::PushLayer(const std::shared_ptr<Layer>& layer)
        {
            m_LayerStack.PushLayer(layer);
        }

        void Application::PushOverlay(const std::shared_ptr<Layer>& overlay)
        {
            m_LayerStack.PushOverlay(overlay);
        }

        bool Application::OnWindowClose(WindowCloseEvent& e)
        {
            std::cout << "Window close event received: " << e.ToString() << std::endl;
            Close();
            return true;
        }

        bool Application::OnWindowResize(WindowResizeEvent& e)
        {
            std::cout << "Window resize event: " << e.ToString() << std::endl;
            // 这里可以处理窗口大小改变的逻辑，比如更新视口
            return false; // 不阻止事件继续传播给层级
        }
    }
}
