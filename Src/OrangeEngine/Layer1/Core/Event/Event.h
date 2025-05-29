#ifndef EVENT_H
#define EVENT_H

#include <string>
#include <functional>
#include <iostream>

namespace Orange
{
    namespace Core
    {
        // 事件类型枚举
        enum class EventType
        {
            None = 0,
            WindowClose, WindowResize, WindowFocus, WindowLostFocus, WindowMoved,
            AppTick, AppUpdate, AppRender,
            KeyPressed, KeyReleased, KeyTyped,
            MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled
        };

        // 事件分类枚举（用于过滤）
        enum EventCategory
        {
            None = 0,
            EventCategoryApplication    = 1 << 0,
            EventCategoryInput          = 1 << 1,
            EventCategoryKeyboard       = 1 << 2,
            EventCategoryMouse          = 1 << 3,
            EventCategoryMouseButton    = 1 << 4
        };

        // 事件基类
        class Event
        {
        public:
            virtual ~Event() = default;

            bool Handled = false;

            virtual EventType GetEventType() const = 0;
            virtual const char* GetName() const = 0;
            virtual int GetCategoryFlags() const = 0;
            virtual std::string ToString() const { return GetName(); }

            inline bool IsInCategory(EventCategory category)
            {
                return GetCategoryFlags() & category;
            }
        };

        // 事件分发器
        class EventDispatcher
        {
        public:
            EventDispatcher(Event& event)
                : m_Event(event)
            {
            }

            // F will be deduced by the compiler
            template<typename T, typename F>
            bool Dispatch(const F& func)
            {
                if (m_Event.GetEventType() == T::GetStaticType())
                {
                    m_Event.Handled |= func(static_cast<T&>(m_Event));
                    return true;
                }
                return false;
            }
        private:
            Event& m_Event;
        };

        inline std::ostream& operator<<(std::ostream& os, const Event& e)
        {
            return os << e.ToString();
        }
    }
}

// 宏定义，用于简化事件类的实现
#define EVENT_CLASS_TYPE(type) static EventType GetStaticType() { return EventType::type; }\
                               virtual EventType GetEventType() const override { return GetStaticType(); }\
                               virtual const char* GetName() const override { return #type; }

#define EVENT_CLASS_CATEGORY(category) virtual int GetCategoryFlags() const override { return category; }

#endif // EVENT_H
