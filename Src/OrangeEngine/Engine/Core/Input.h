#ifndef INPUT_H
#define INPUT_H

#include <pch.h>
#include "OrangeExport.h"

namespace Orange
{
    class ORANGE_API Input
    {
    public:
        static bool IsKeyPressed(int keycode) {return spInstance->IsKeyPressedImpl(keycode); }

    protected:
        virtual bool IsKeyPressedImpl(int keycode) = 0;
    private:
        static Input* spInstance;
    };

    Input* Input::spInstance = nullptr;
}

#endif // INPUT_H