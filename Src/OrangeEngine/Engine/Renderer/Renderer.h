#ifndef RENDERER_H
#define RENDERER_H

#include "OrangeExport.h"
#include "RenderCommand.h"
namespace Orange
{

    class ORANGE_API Renderer
    {
    public:
        static void BeginScene();
        static void EndScene();

        static void Submit(const std::shared_ptr<VertexArray>& vertexArray);

        inline static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }
    };
}

#endif // RENDERER_H