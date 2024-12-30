#ifndef RENDERER_H
#define RENDERER_H

#include "OrangeExport.h"

namespace Orange
{
    enum class RendererAPI
    {
        None = 0,
        OpenGL = 1
    };

    class ORANGE_API Renderer
    {
    public:
        inline static RendererAPI GetAPI() { return sRendererAPI; }
    private:
        static RendererAPI sRendererAPI;
    };
}

#endif // RENDERER_H