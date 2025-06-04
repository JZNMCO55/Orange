#include "Application.h"
#include "Window.h"
#include "../Event/Events.h"
#include "../../Platform/Graphics/GraphicsInterface/Graphics.h"
#include <iostream>

namespace Orange
{
    namespace Core
    {
        // 单例实例定义
        Application *Application::s_instance = nullptr;

        Application::Application()
        {
            // 设置单例实例
            s_instance = this;

            // 创建窗口
            WindowProps props("Orange Engine", 1280, 720);
            m_window = std::make_unique<Window>(props);

            // 设置窗口事件回调
            m_window->SetEventCallback([this](Event &event)
                                       { this->OnEvent(event); });

            // 初始化图形系统
            if (!InitializeGraphicsSystem())
            {
                std::cerr << "Failed to initialize graphics system!" << std::endl;
                throw std::runtime_error("Graphics system initialization failed");
            }

            std::cout << "Application initialized successfully!" << std::endl;
        }

        Application::~Application()
        {
            // 图形系统会自动清理（智能指针）
            std::cout << "Application destroyed." << std::endl;
        }

        bool Application::InitializeGraphicsSystem()
        {
            // 创建图形系统
            auto graphicsSystemPtr = Graphics::GraphicsFactory::CreateGraphicsSystem(Graphics::GraphicsAPI::Vulkan);
            if (!graphicsSystemPtr)
            {
                std::cerr << "Failed to create graphics system" << std::endl;
                return false;
            }

            m_graphicsSystem = std::move(graphicsSystemPtr);

            // 初始化图形系统，传入窗口句柄
            void *windowHandle = m_window->GetNativeWindow();
            if (!m_graphicsSystem->Initialize(windowHandle))
            {
                std::cerr << "Failed to initialize graphics system" << std::endl;
                return false;
            }

            std::cout << "Graphics system initialized successfully" << std::endl;
            return true;
        }

        void Application::Run()
        {
            std::cout << "Starting application main loop..." << std::endl;

            while (m_running && !m_window->ShouldClose())
            {
                // 开始渲染帧
                m_graphicsSystem->BeginFrame();

                // 更新所有层级
                for (auto &layer : m_LayerStack)
                {
                    layer->OnUpdate();
                }

                // 结束渲染帧
                m_graphicsSystem->EndFrame();
                m_graphicsSystem->Present();

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

        void Application::OnEvent(Event &e)
        {
            EventDispatcher dispatcher(e);

            // 分发窗口关闭事件
            dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent &event) -> bool
                                                  { return OnWindowClose(event); });

            // 分发窗口大小改变事件
            dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent &event) -> bool
                                                   { return OnWindowResize(event); });

            // 将事件传递给层级（从后往前，overlay优先处理）
            for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
            {
                if (e.Handled)
                    break;
                (*it)->OnEvent(e);
            }
        }

        void Application::PushLayer(const std::shared_ptr<Layer> &layer)
        {
            m_LayerStack.PushLayer(layer);
        }

        void Application::PushOverlay(const std::shared_ptr<Layer> &overlay)
        {
            m_LayerStack.PushOverlay(overlay);
        }

        bool Application::OnWindowClose(WindowCloseEvent &e)
        {
            std::cout << "Window close event received: " << e.ToString() << std::endl;
            Close();
            return true;
        }

        bool Application::OnWindowResize(WindowResizeEvent &e)
        {
            std::cout << "Window resize event: " << e.ToString() << std::endl;
            // 这里可以处理窗口大小改变的逻辑，比如更新视口
            return false; // 不阻止事件继续传播给层级
        }
    }
}
