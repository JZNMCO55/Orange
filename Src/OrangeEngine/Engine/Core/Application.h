#ifndef APPLICATION_H
#define APPLICATION_H

#include "OrangeExport.h"
#include "IWindow.h"
namespace Orange
{
    class ORANGE_API Application
    {
    public:
        Application();
        virtual ~Application();

        void Run();

    private:
        std::unique_ptr<IWindow> mpWindow{ nullptr };
    };

    Application* CreateApplication();
}

#endif // APPLICATION_H