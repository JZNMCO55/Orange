#ifndef ORANGE_APPLICATION_H
#define ORANGE_APPLICATION_H

#include <memory>

namespace Orange
{
    namespace Core
    {
        class Window;
        class Application
        {
        public:
            Application();
            virtual ~Application();

            void Run();

            void Close();

        private:
            std::unique_ptr<Window> m_window;
            bool m_running = true;
        };
    }
}

#endif