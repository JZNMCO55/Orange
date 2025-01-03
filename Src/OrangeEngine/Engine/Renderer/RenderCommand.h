#ifndef RENDER_COMMAND_H
#define RENDER_COMMAND_H

#include "OrangeExport.h"
#include "RendererAPI.h"
namespace Orange
{
    class ORANGE_API RenderCommand
    {
    public:
        inline static void SetClearColor(const glm::vec4& color)
        {
            spRendererAPI->SetClearColor(color);
        }

        inline static void Clear()
        {
            spRendererAPI->Clear();
        }

        inline static void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray)
        {
            spRendererAPI->DrawIndexed(vertexArray);
        }

    private:
        static RendererAPI* spRendererAPI;
    };
}

#endif // RENDER_COMMAND_H