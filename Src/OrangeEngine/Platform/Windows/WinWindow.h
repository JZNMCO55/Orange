#ifndef WIN_WINDOW_H
#define WIN_WINDOW_H

#include "IWindow.h"
#include "GLFW/glfw3.h"
#include "OrangeExport.h"

namespace Orange
{
    class ORANGE_API WinWindow : public IWindow
    {
    public:
        WinWindow(const WindowProps& data);

        virtual ~WinWindow();

        void OnUpdate() override;

        unsigned int GetWidth() const override { return mData.mWidth; }
        unsigned int GetHeight() const override { return mData.mHeight; }

        // Window attributes
        void SetEventCallback(const EventCallbackFn& callback) override { mData.mEventCallback = callback; }
        void SetVSync(bool enabled) override;
        bool IsVSync() const override { return mData.mVSync; }

        void* GetNativeWindow() const override { return mpWindow; }

        static std::unique_ptr<IWindow> Create(const WindowProps& props = WindowProps());
    private:
        void Init(const WindowProps& props);
        void Shutdown();
    private:
        struct WindowData
        {
            std::string mTitle;
            unsigned int mWidth;
            unsigned int mHeight;
            bool mVSync;

            EventCallbackFn mEventCallback;
        };

    private:
        GLFWwindow* mpWindow;
        WindowData mData;
    };
}

#endif // WIN_WINDOW_H