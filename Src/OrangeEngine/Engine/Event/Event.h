#ifndef EVENT_H
#define EVENT_H

#include "OrangeExport.h"
#include "../pch.h"

namespace Orange
{
    // Events in Orange Engine are currently blocking, meaning when an event occurs,it
    // immediately gets dispatched ande must be dealt with right then and there.
    // For the future, it may be worth considering a more flexible event system that buffers
    // events in an event queue and processes them in a separate thread.

    enum class EEventType
    {
        None = 0,
        
        // Window events
        WindowClose, WindowResize, WindowFocus, WindowLostFocus, WindowMoved,

        // Application events
        AppTick, AppUpdate, AppRender,

        // Keyboard events
        KeyPressed, KeyReleased, KeyTyped,

        // Mouse events
        MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled
    };

    enum EEventCategory
    {
        None = 0,
        EventCategoryApplication = BIT(0),
        EventCategoryInput = BIT(1),
        EventCategoryKeyboard = BIT(2),
        EventCategoryMouse = BIT(3),
        EventCategoryMouseButton = BIT(4)
    };

#define EVENT_CLASS_TYPE(type) static EEventType GetStaticType() { return EEventType::##type; }\
                                virtual EEventType GetEventType() const override { return GetStaticType(); }\
                                virtual const char* GetName() const override { return #type; }

#define EVENT_CLASS_CATEGORY(category) virtual int GetCategoryFlags() const override { return category; }

    class ORANGE_API Event
    {
        friend class EventDispatcher;
    public:
        virtual EEventType GetEventType() const = 0;
        virtual const char* GetName() const = 0;
        virtual int GetCategoryFlags() const = 0;
        virtual std::string ToString() const { return GetName(); }
        virtual bool IsHandled() const { return mbHandled; }

        inline bool IsInCategory(EEventCategory category) const
        {
            return GetCategoryFlags() & category;
        }

    protected:
        bool mbHandled{ false };
    };

    class EventDispatcher
    {
        template<typename T>
        using EventFn = std::function<bool(T&)>;

    public:
        EventDispatcher(Event& event)
            : mEvent(event)
        {
        }

        template<typename T>
        bool Dispatch(EventFn<T> func)
        {
            if (mEvent.GetEventType() == T::GetStaticType())
            {
                mEvent.mbHandled = func(*(T*)&mEvent);
                return true;
            }
            return false;
        }

    private:
        Event& mEvent;
    };
}
#endif // EVENT_H