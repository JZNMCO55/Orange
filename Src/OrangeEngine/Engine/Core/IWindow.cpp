#include "pch.h"
#include "IWindow.h"
#include "Platform/Windows/WinWindow.h"

namespace Orange
{
    Scope<IWindow> IWindow::Create(const WindowProps& props)
    {
#ifdef PLATFORM_WINDOWS
        return CreateScope<WinWindow>(props);
#else
        ORANGE_CORE_ASSERT(false, "Unknown platform!");
        return nullptr;
#endif
    }
}