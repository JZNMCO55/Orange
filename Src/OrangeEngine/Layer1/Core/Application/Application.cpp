#include "Application.h"
#include "Window.h"
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
            m_window->SetEventCallback([this](void *event)
                                       {
                // 处理窗口关闭事件
                this->Close(); });

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
    }
}
