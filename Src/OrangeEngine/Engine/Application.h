#ifndef APPLICATION_H
#define APPLICATION_H

#include "OrangeExport.h"
#include "../pch.h"
namespace Orange
{
    class ORANGE_EXPORT Application
    {
        public:
            Application();
            virtual ~Application();

            void Run();
            
    };

    Application* CreateApplication();
}

#endif // APPLICATION_H