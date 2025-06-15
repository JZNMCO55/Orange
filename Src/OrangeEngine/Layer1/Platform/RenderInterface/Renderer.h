#ifndef RENDERER_H
#define RENDERER_H

#include "Platform/RenderInterface/RendererContext.h"
#include "Platform/RenderInterface/RenderPass.h"

namespace Orange
{
    class ShaderLibrary;

    class Renderer
    {
    public:
        typedef void (*RenderCommandFn)(void *data);

        static Ref<RendererContext> GetContext() noexcept { return nullptr; }

        static void Init() noexcept;

        static void Shutdown() noexcept;

        static RendererCapabilities &GetCapabilities();

        template <typename FuncT>
        static void Submit(FuncT &&func)
        {
        }
    }
}

#endif