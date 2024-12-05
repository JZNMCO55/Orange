#ifndef APPLICATION_H
#define APPLICATION_H

#include "OrangeExport.h"
#include <string>
#include <functional>
#include <sstream>
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