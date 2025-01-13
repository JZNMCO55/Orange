#ifndef RENDER_COMMAND_H
#define RENDER_COMMAND_H

#include "OrangeExport.h"
#include "RendererAPI.h"
namespace Orange
{
    class ORANGE_API RenderCommand
    {
    public:
        inline static void Init()
        {
            spRendererAPI->Init();
        }

        inline static void SetClearColor(const glm::vec4& color)
        {
            spRendererAPI->SetClearColor(color);
        }

        inline static void Clear()
        {
            spRendererAPI->Clear();
        }

        inline static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
        {
            spRendererAPI->SetViewport(x, y, width, height);
        }

        inline static void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray, uint32_t count = 0)
        {
            spRendererAPI->DrawIndexed(vertexArray, count);
        }

    private:
        static RendererAPI* spRendererAPI;
    };
}

#endif // RENDER_COMMAND_H