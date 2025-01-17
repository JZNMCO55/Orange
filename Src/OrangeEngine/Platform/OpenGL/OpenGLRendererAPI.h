#ifndef OPENGL_RENDERER_API_H
#define OPENGL_RENDERER_API_H

#include "OrangeExport.h"
#include "Renderer/RendererAPI.h"
namespace Orange
{
    class ORANGE_API OpenGLRendererAPI : public RendererAPI
    {
    public:
        virtual void Init() override;
        virtual void SetClearColor(const glm::vec4& color) override;
        virtual void Clear() override;
        virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;

        virtual void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray, uint32_t indexCount = 0) override;
    };
}

#endif // !