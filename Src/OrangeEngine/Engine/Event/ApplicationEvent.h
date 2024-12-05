#ifndef APPLICATION_EVENT_H
#define APPLICATION_EVENT_H

#include "Event.h"
#include "../pch.h"
namespace Orange
{
    class ORANGE_EXPORT WindowResizeEvent : public Event
    {
    public:
        WindowResizeEvent(unsigned int width, unsigned int height)
            : mWidth(width), mHeight(height) {}

        inline unsigned int GetWidth() const { return mWidth; }
        inline unsigned int GetHeight() const { return mHeight; }

        std::string ToString() const override
        {
            std::stringstream ss;
            ss << "WindowResizeEvent: " << mWidth << ", " << mHeight;
            return ss.str();
        }
        EVENT_CLASS_TYPE(WindowResize)
        EVENT_CLASS_CATEGORY(EventCategoryApplication)
    private:
        unsigned int mWidth, mHeight;
    };

    class ORANGE_EXPORT WindowCloseEvent : public Event
    {
    public: 
        WindowCloseEvent() = default;

        EVENT_CLASS_TYPE(WindowClose)
        EVENT_CLASS_CATEGORY(EventCategoryApplication)
    };

    class ORANGE_EXPORT AppTickEvent : public Event
    {
    public:
        AppTickEvent() = default;

        EVENT_CLASS_TYPE(AppTick)
        EVENT_CLASS_CATEGORY(EventCategoryApplication)
    };

    class ORANGE_EXPORT AppUpdateEvent : public Event
    {
    public:
        AppUpdateEvent() = default;

        EVENT_CLASS_TYPE(AppUpdate)
        EVENT_CLASS_CATEGORY(EventCategoryApplication)
    };

    class ORANGE_EXPORT AppRenderEvent : public Event
    {
    public:
        AppRenderEvent() = default;

        EVENT_CLASS_TYPE(AppRender)
        EVENT_CLASS_CATEGORY(EventCategoryApplication)
    };
}

#endif // APPLICATION_EVENT_H