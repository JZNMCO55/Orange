#ifndef MOUSE_EVENT_H
#define MOUSE_EVENT_H

#include "Event.h"
#include <sstream>

namespace Orange
{
    namespace Core
    {
        // 鼠标移动事件
        class MouseMovedEvent : public Event
        {
        public:
            MouseMovedEvent(const float x, const float y)
                : m_MouseX(x), m_MouseY(y) {}

            float GetX() const { return m_MouseX; }
            float GetY() const { return m_MouseY; }

            std::string ToString() const override
            {
                std::stringstream ss;
                ss << "MouseMovedEvent: " << m_MouseX << ", " << m_MouseY;
                return ss.str();
            }

            EVENT_CLASS_TYPE(MouseMoved)
            EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)
        private:
            float m_MouseX, m_MouseY;
        };

        // 鼠标滚轮事件
        class MouseScrolledEvent : public Event
        {
        public:
            MouseScrolledEvent(const float xOffset, const float yOffset)
                : m_XOffset(xOffset), m_YOffset(yOffset) {}

            float GetXOffset() const { return m_XOffset; }
            float GetYOffset() const { return m_YOffset; }

            std::string ToString() const override
            {
                std::stringstream ss;
                ss << "MouseScrolledEvent: " << GetXOffset() << ", " << GetYOffset();
                return ss.str();
            }

            EVENT_CLASS_TYPE(MouseScrolled)
            EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)
        private:
            float m_XOffset, m_YOffset;
        };

        // 鼠标按钮事件基类
        class MouseButtonEvent : public Event
        {
        public:
            int GetMouseButton() const { return m_Button; }

            EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput | EventCategoryMouseButton)
        protected:
            MouseButtonEvent(const int button)
                : m_Button(button) {}

            int m_Button;
        };

        // 鼠标按钮按下事件
        class MouseButtonPressedEvent : public MouseButtonEvent
        {
        public:
            MouseButtonPressedEvent(const int button)
                : MouseButtonEvent(button) {}

            std::string ToString() const override
            {
                std::stringstream ss;
                ss << "MouseButtonPressedEvent: " << m_Button;
                return ss.str();
            }

            EVENT_CLASS_TYPE(MouseButtonPressed)
        };

        // 鼠标按钮释放事件
        class MouseButtonReleasedEvent : public MouseButtonEvent
        {
        public:
            MouseButtonReleasedEvent(const int button)
                : MouseButtonEvent(button) {}

            std::string ToString() const override
            {
                std::stringstream ss;
                ss << "MouseButtonReleasedEvent: " << m_Button;
                return ss.str();
            }

            EVENT_CLASS_TYPE(MouseButtonReleased)
        };
    }
}

#endif // MOUSE_EVENT_H 