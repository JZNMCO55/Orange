#ifndef OPENGL_RENDERER_API_H
#define OPENGL_RENDERER_API_H

#include "OrangeExport.h"
#include "Renderer/RendererAPI.h"
namespace Orange
{
    class ORANGE_API OpenGLRendererAPI : public RendererAPI
    {
    public:
        virtual void SetClearColor(const glm::vec4& color) override;
        virtual void Clear() override;

        virtual void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray) override;
    };
}

#endif // !