#ifndef ORANGE_WINDOW_H
#define ORANGE_WINDOW_H

#include <string>
#include <memory>
#include <functional>

// 事件回调函数类型定义
using EventCallbackFn = std::function<void(void *)>;

namespace Orange
{
    namespace Core
    {
        struct WindowProps
        {
            std::string title;
            unsigned int width;
            unsigned int height;

            WindowProps(const std::string &title = "Orange Engine", unsigned int width = 1280, unsigned int height = 720)
                : title(title), width(width), height(height) {}
        };

        class Window
        {
        public:
            Window(const WindowProps &props);
            virtual ~Window();

            virtual void OnUpdate();
            virtual uint32_t GetWidth() const noexcept { return m_props.width; }
            virtual uint32_t GetHeight() const noexcept { return m_props.height; }
            virtual std::string GetTitle() const noexcept { return m_props.title; }

            virtual void SetEventCallback(const EventCallbackFn &callback);

            virtual void *GetNativeWindow() const;
            // temp function
            virtual bool ShouldClose() const;

        private:
            struct WindowImp;
            std::unique_ptr<WindowImp> m_windowImp;
            WindowProps m_props;
        };
    }
}

#endif