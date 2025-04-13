#ifndef SCRIPT_ENGINE_H
#define SCRIPT_ENGINE_H

#include "OrangeExport.h"

namespace Orange
{
    class ORANGE_API ScriptEngine
    {
        public:
            static void Init();
            static void Shutdown();
        private:
            static void InitMono();
            static void ShutdownMono();
    };
}

#endif // SCRIPT_ENGINE_H