#ifndef IWINDOW_H
#define IWINDOW_H

#include "OrangeExport.h"
#include "Event.h"

namespace Orange
{
    struct WindowProps
    {
        std::string mTitle;
        uint32_t mWidth;
        uint32_t mHeight;

        WindowProps(const std::string& title = "Orange Engine",
                    uint32_t width = 1280,
                    uint32_t height = 720)
            : mTitle(title), mWidth(width), mHeight(height)
        {
        }

    };

    // Window interface to support diffrent window libraries in different platforms i
    class ORANGE_API IWindow
    {
    public:
        using EventCallbackFn = std::function<void(Event&)>;

        virtual ~IWindow() = default;

        virtual void OnUpdate() = 0;
        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;

        // Window attributes
        virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
        virtual void SetVSync(bool enabled) = 0;
        virtual bool IsVSync() const = 0;

        virtual void* GetNativeWindow() const = 0;

        static std::unique_ptr<IWindow> Create(const WindowProps& props = WindowProps());
    };
}

#endif // IWINDOW_H