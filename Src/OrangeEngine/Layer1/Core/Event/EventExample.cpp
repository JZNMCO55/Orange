// 这是一个事件系统使用示例文件
// 展示如何创建自定义的事件处理器

#include "Events.h"
#include <iostream>

namespace Orange
{
    namespace Core
    {
        // 示例：自定义事件处理器类
        class ExampleEventHandler
        {
        public:
            void OnEvent(Event& e)
            {
                EventDispatcher dispatcher(e);

                // 处理窗口事件
                dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& event) -> bool
                {
                    std::cout << "[ExampleHandler] " << event.ToString() << std::endl;
                    return OnWindowClose(event);
                });

                dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& event) -> bool
                {
                    std::cout << "[ExampleHandler] " << event.ToString() << std::endl;
                    return OnWindowResize(event);
                });

                // 处理键盘事件
                dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& event) -> bool
                {
                    std::cout << "[ExampleHandler] " << event.ToString() << std::endl;
                    return OnKeyPressed(event);
                });

                dispatcher.Dispatch<KeyReleasedEvent>([this](KeyReleasedEvent& event) -> bool
                {
                    std::cout << "[ExampleHandler] " << event.ToString() << std::endl;
                    return OnKeyReleased(event);
                });

                // 处理鼠标事件
                dispatcher.Dispatch<MouseButtonPressedEvent>([this](MouseButtonPressedEvent& event) -> bool
                {
                    std::cout << "[ExampleHandler] " << event.ToString() << std::endl;
                    return OnMouseButtonPressed(event);
                });

                dispatcher.Dispatch<MouseMovedEvent>([this](MouseMovedEvent& event) -> bool
                {
                    // 鼠标移动事件太频繁，通常不打印
                    return OnMouseMoved(event);
                });

                dispatcher.Dispatch<MouseScrolledEvent>([this](MouseScrolledEvent& event) -> bool
                {
                    std::cout << "[ExampleHandler] " << event.ToString() << std::endl;
                    return OnMouseScrolled(event);
                });
            }

        private:
            bool OnWindowClose(WindowCloseEvent& e)
            {
                std::cout << "Custom window close handling" << std::endl;
                return false; // 不阻止事件传播
            }

            bool OnWindowResize(WindowResizeEvent& e)
            {
                std::cout << "Window resized to: " << e.GetWidth() << "x" << e.GetHeight() << std::endl;
                return false;
            }

            bool OnKeyPressed(KeyPressedEvent& e)
            {
                // 示例：处理WASD移动
                switch (e.GetKeyCode())
                {
                    case static_cast<int>(KeyCode::W):
                        std::cout << "Move Forward" << std::endl;
                        break;
                    case static_cast<int>(KeyCode::A):
                        std::cout << "Move Left" << std::endl;
                        break;
                    case static_cast<int>(KeyCode::S):
                        std::cout << "Move Backward" << std::endl;
                        break;
                    case static_cast<int>(KeyCode::D):
                        std::cout << "Move Right" << std::endl;
                        break;
                    case static_cast<int>(KeyCode::Space):
                        std::cout << "Jump" << std::endl;
                        break;
                }
                return false;
            }

            bool OnKeyReleased(KeyReleasedEvent& e)
            {
                std::cout << "Key released: " << e.GetKeyCode() << std::endl;
                return false;
            }

            bool OnMouseButtonPressed(MouseButtonPressedEvent& e)
            {
                switch (e.GetMouseButton())
                {
                    case static_cast<int>(MouseCode::ButtonLeft):
                        std::cout << "Left mouse button action" << std::endl;
                        break;
                    case static_cast<int>(MouseCode::ButtonRight):
                        std::cout << "Right mouse button action" << std::endl;
                        break;
                    case static_cast<int>(MouseCode::ButtonMiddle):
                        std::cout << "Middle mouse button action" << std::endl;
                        break;
                }
                return false;
            }

            bool OnMouseMoved(MouseMovedEvent& e)
            {
                // 可以在这里处理鼠标移动逻辑，比如相机控制
                // std::cout << "Mouse position: " << e.GetX() << ", " << e.GetY() << std::endl;
                return false;
            }

            bool OnMouseScrolled(MouseScrolledEvent& e)
            {
                std::cout << "Mouse scroll: " << e.GetXOffset() << ", " << e.GetYOffset() << std::endl;
                // 可以用于缩放功能
                return false;
            }
        };

        // 示例：如何在Application中使用多个事件处理器
        class ExampleApplication
        {
        public:
            ExampleApplication()
            {
                m_eventHandler = std::make_unique<ExampleEventHandler>();
            }

            void OnEvent(Event& e)
            {
                // 首先让自定义处理器处理事件
                m_eventHandler->OnEvent(e);

                // 如果事件没有被处理，可以添加默认处理逻辑
                if (!e.Handled)
                {
                    EventDispatcher dispatcher(e);
                    
                    // 应用程序级别的事件处理
                    dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& event) -> bool
                    {
                        std::cout << "[Application] Shutting down..." << std::endl;
                        // 执行清理工作
                        return true; // 标记事件已处理
                    });
                }
            }

        private:
            std::unique_ptr<ExampleEventHandler> m_eventHandler;
        };
    }
}

/*
使用示例：

// 在你的应用程序中
ExampleApplication app;

// 设置窗口事件回调
window->SetEventCallback([&app](Event& event)
{
    app.OnEvent(event);
});

// 或者直接使用事件处理器
ExampleEventHandler handler;
window->SetEventCallback([&handler](Event& event)
{
    handler.OnEvent(event);
});
*/ 