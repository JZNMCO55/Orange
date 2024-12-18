#ifndef WIN_INPUT_H
#define WIN_INPUT_H

#include "input.h"

namespace Orange
{
    class  WinInput : public Input
    {
    protected:
        virtual bool IsKeyPressedImpl(int keycode) override;

        virtual bool IsMouseButtonPressedImpl(int button) override;

        virtual std::pair<float, float> GetMousePositionImpl() override;

        virtual float GetMouseXImpl() override;

        virtual float GetMouseYImpl() override;

    };
}

#endif // WIN_INPUT_H