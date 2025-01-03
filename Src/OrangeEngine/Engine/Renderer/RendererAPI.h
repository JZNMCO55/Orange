#ifndef RENDERER_API_H
#define RENDERER_API_H

#include "OrangeExport.h"
#include "VertexArray.h"

namespace Orange
{
    class ORANGE_API RendererAPI
    {
    public:
        enum class API
        {
            None = 0,
            OpenGL = 1
        };

    public:
        virtual void SetClearColor(const glm::vec4& color) = 0;
        virtual void Clear() = 0;

        virtual void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray) = 0;

        inline static API GetAPI() { return sAPI; }

    private:
        static API sAPI;
    };
}

#endif // RENDERER_API_H