#ifndef MOUSE_EVENT_H
#define MOUSE_EVENT_H

#include "Event.h"

namespace Orange
{
    class ORANGE_EXPORT MouseMoveEvent : public Event
    {
    public:
        MouseMoveEvent(float x, float y)
            : mMouseX(x), mMouseY(y) {}

        float GetX() const { return mMouseX; }
        float GetY() const { return mMouseY; }

        std::string ToString() const override
        {
            std::stringstream ss;
            ss << "MouseMoveEvent: " << mMouseX << ", " << mMouseY;
            return ss.str();
        }

        EVENT_CLASS_TYPE(MouseMoved)
        EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)
    private:
        float mMouseX, mMouseY;
    };

    class ORANGE_EXPORT MouseButtonEvent : public Event
    {
    public:
        MouseButtonEvent(int button)
            : mButton(button) {}

        int GetButton() const { return mButton; }

        std::string ToString() const override
        {
            std::stringstream ss;
            ss << "MouseButtonEvent: " << mButton;
            return ss.str();
        }

        EVENT_CLASS_TYPE(MouseButtonPressed)
        EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput | EventCategoryMouseButton)
    protected:
        int mButton;
    };

    class ORANGE_EXPORT MouseButtonReleasedEvent : public MouseButtonEvent
    {
    public:
        MouseButtonReleasedEvent(int button)
            : MouseButtonEvent(button) {}

        std::string ToString() const override
        {
            std::stringstream ss;
            ss << "MouseButtonReleasedEvent: " << mButton;
            return ss.str();
        }

        EVENT_CLASS_TYPE(MouseButtonReleased)
    };

    class ORANGE_EXPORT MouseButtonPressedEvent : public MouseButtonEvent
    {
    public:
        MouseButtonPressedEvent(int button)
            : MouseButtonEvent(button) {}

        std::string ToString() const override
        {
            std::stringstream ss;
            ss << "MouseButtonPressedEvent: " << mButton;
            return ss.str();
        }

        EVENT_CLASS_TYPE(MouseButtonPressed)
    };
}


#endif // MOUSE_EVENT_H