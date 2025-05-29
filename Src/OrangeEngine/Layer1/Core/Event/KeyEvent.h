#ifndef KEY_EVENT_H
#define KEY_EVENT_H

#include "Event.h"
#include <sstream>

namespace Orange
{
    namespace Core
    {
        // 键盘事件基类
        class KeyEvent : public Event
        {
        public:
            int GetKeyCode() const { return m_KeyCode; }

            EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)
        protected:
            KeyEvent(int keycode)
                : m_KeyCode(keycode) {}

            int m_KeyCode;
        };

        // 按键按下事件
        class KeyPressedEvent : public KeyEvent
        {
        public:
            KeyPressedEvent(int keycode, bool isRepeat = false)
                : KeyEvent(keycode), m_IsRepeat(isRepeat) {}

            bool IsRepeat() const { return m_IsRepeat; }

            std::string ToString() const override
            {
                std::stringstream ss;
                ss << "KeyPressedEvent: " << m_KeyCode << " (repeat = " << m_IsRepeat << ")";
                return ss.str();
            }

            EVENT_CLASS_TYPE(KeyPressed)
        private:
            bool m_IsRepeat;
        };

        // 按键释放事件
        class KeyReleasedEvent : public KeyEvent
        {
        public:
            KeyReleasedEvent(int keycode)
                : KeyEvent(keycode) {}

            std::string ToString() const override
            {
                std::stringstream ss;
                ss << "KeyReleasedEvent: " << m_KeyCode;
                return ss.str();
            }

            EVENT_CLASS_TYPE(KeyReleased)
        };

        // 字符输入事件
        class KeyTypedEvent : public KeyEvent
        {
        public:
            KeyTypedEvent(int keycode)
                : KeyEvent(keycode) {}

            std::string ToString() const override
            {
                std::stringstream ss;
                ss << "KeyTypedEvent: " << m_KeyCode;
                return ss.str();
            }

            EVENT_CLASS_TYPE(KeyTyped)
        };
    }
}

#endif // KEY_EVENT_H 