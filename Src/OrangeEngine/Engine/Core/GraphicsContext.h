#ifndef GRAPHICS_CONTEXT_H
#define GRAPHICS_CONTEXT_H

#include "OrangeExport.h"

namespace Orange
{
    class ORANGE_API GraphicsContext
    {
        virtual void Init() = 0;
        virtual void SwapBuffers() = 0;
    };
}

#endif // GRAPHICS_CONTEXT_H