#ifndef APPLICATION_EVENT_H
#define APPLICATION_EVENT_H

#include "Event.h"
#include <sstream>

namespace Orange
{
    namespace Core
    {
        // 窗口大小改变事件
        class WindowResizeEvent : public Event
        {
        public:
            WindowResizeEvent(unsigned int width, unsigned int height)
                : m_Width(width), m_Height(height) {}

            unsigned int GetWidth() const { return m_Width; }
            unsigned int GetHeight() const { return m_Height; }

            std::string ToString() const override
            {
                std::stringstream ss;
                ss << "WindowResizeEvent: " << m_Width << ", " << m_Height;
                return ss.str();
            }

            EVENT_CLASS_TYPE(WindowResize)
            EVENT_CLASS_CATEGORY(EventCategoryApplication)
        private:
            unsigned int m_Width, m_Height;
        };

        // 窗口关闭事件
        class WindowCloseEvent : public Event
        {
        public:
            WindowCloseEvent() = default;

            EVENT_CLASS_TYPE(WindowClose)
            EVENT_CLASS_CATEGORY(EventCategoryApplication)
        };

        // 窗口获得焦点事件
        class WindowFocusEvent : public Event
        {
        public:
            WindowFocusEvent() = default;

            EVENT_CLASS_TYPE(WindowFocus)
            EVENT_CLASS_CATEGORY(EventCategoryApplication)
        };

        // 窗口失去焦点事件
        class WindowLostFocusEvent : public Event
        {
        public:
            WindowLostFocusEvent() = default;

            EVENT_CLASS_TYPE(WindowLostFocus)
            EVENT_CLASS_CATEGORY(EventCategoryApplication)
        };

        // 窗口移动事件
        class WindowMovedEvent : public Event
        {
        public:
            WindowMovedEvent(int x, int y)
                : m_X(x), m_Y(y) {}

            int GetX() const { return m_X; }
            int GetY() const { return m_Y; }

            std::string ToString() const override
            {
                std::stringstream ss;
                ss << "WindowMovedEvent: " << m_X << ", " << m_Y;
                return ss.str();
            }

            EVENT_CLASS_TYPE(WindowMoved)
            EVENT_CLASS_CATEGORY(EventCategoryApplication)
        private:
            int m_X, m_Y;
        };

        // 应用程序Tick事件
        class AppTickEvent : public Event
        {
        public:
            AppTickEvent() = default;

            EVENT_CLASS_TYPE(AppTick)
            EVENT_CLASS_CATEGORY(EventCategoryApplication)
        };

        // 应用程序更新事件
        class AppUpdateEvent : public Event
        {
        public:
            AppUpdateEvent() = default;

            EVENT_CLASS_TYPE(AppUpdate)
            EVENT_CLASS_CATEGORY(EventCategoryApplication)
        };

        // 应用程序渲染事件
        class AppRenderEvent : public Event
        {
        public:
            AppRenderEvent() = default;

            EVENT_CLASS_TYPE(AppRender)
            EVENT_CLASS_CATEGORY(EventCategoryApplication)
        };
    }
}

#endif // APPLICATION_EVENT_H 