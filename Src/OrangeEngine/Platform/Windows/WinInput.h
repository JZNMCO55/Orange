#ifndef WIN_INPUT_H
#define WIN_INPUT_H

#include "input.h"

namespace Orange
{
    class  WinInput : public Input
    {
    protected:
        virtual bool IsKeyPressedImpl(int keycode) override;

    };
}

#endif // WIN_INPUT_H