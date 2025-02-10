#ifndef INPUT_H
#define INPUT_H

#include <pch.h>
#include "OrangeExport.h"

namespace Orange
{
    class ORANGE_API Input
    {
    public:
        static bool IsKeyPressed(int keycode);

        static bool IsMouseButtonPressed(int button);

        static std::pair<float, float> GetMousePosition();

        static float GetMouseX();

        static float GetMouseY();  
    };
}


#endif // INPUT_H