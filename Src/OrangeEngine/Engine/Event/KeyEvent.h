#ifndef KEY_EVENT_H
#define KEY_EVENT_H

#include "Event.h"
#include <string>
#include <functional>
#include <sstream>
namespace Orange
{
    class ORANGE_EXPORT KeyEvent : public Event
    {
    public:
        inline int GetKeyCode() const { return mKeyCode; }

        EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)
    protected:
        KeyEvent(int keyCode)
            : mKeyCode(keyCode) {}
    protected:
        int mKeyCode;
    };

    class ORANGE_EXPORT KeyPressedEvent : public KeyEvent
    {
    public:
        KeyPressedEvent(int keyCode, int repeatCount)
            : KeyEvent(keyCode), mRepeatCount(repeatCount) {}

        inline int GetRepeatCount() const { return mRepeatCount; }

        std::string ToString() const override
        {
            std::stringstream ss;
            ss << "KeyPressedEvent: " << mKeyCode << " (" << mRepeatCount << " repeats)";
            return ss.str();
        }

        EVENT_CLASS_TYPE(KeyPressed)
    private:
        int mRepeatCount;
    };

    class ORANGE_EXPORT KeyReleasedEvent : public KeyEvent
    {
    public:
        KeyReleasedEvent(int keyCode)
            : KeyEvent(keyCode) {}

        std::string ToString() const override
        {
            std::stringstream ss;
            ss << "KeyReleasedEvent: " << mKeyCode;
            return ss.str();
        }

        EVENT_CLASS_TYPE(KeyReleased)
    };

    class ORANGE_EXPORT KeyTypedEvent : public KeyEvent
    {
    public: 
        KeyTypedEvent(int keyCode)
            : KeyEvent(keyCode) {}

        std::string ToString() const override
        {
            std::stringstream ss;
            ss << "KeyTypedEvent: " << mKeyCode;
            return ss.str();
        }

        EVENT_CLASS_TYPE(KeyTyped)
    };
} 

#endif // KEY_EVENT_H