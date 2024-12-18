#ifndef INPUT_H
#define INPUT_H

#include <pch.h>
#include "OrangeExport.h"

namespace Orange
{
    class ORANGE_API Input
    {
    public:
        inline static bool IsKeyPressed(int keycode) {return spInstance->IsKeyPressedImpl(keycode); }

        inline static bool IsMouseButtonPressed(int button) {return spInstance->IsMouseButtonPressedImpl(button); }

        inline static std::pair<float, float> GetMousePosition() {return spInstance->GetMousePositionImpl(); }

        inline static float GetMouseX() {return spInstance->GetMouseXImpl(); }

        inline static float GetMouseY() {return spInstance->GetMouseYImpl(); }

    protected:
        virtual bool IsKeyPressedImpl(int keycode) = 0;

        virtual bool IsMouseButtonPressedImpl(int button) = 0;

        virtual std::pair<float, float> GetMousePositionImpl() = 0;

        virtual float GetMouseXImpl() = 0;

        virtual float GetMouseYImpl() = 0;
    private:
        static Input* spInstance;
    };
}

#endif // INPUT_H