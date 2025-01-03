#ifndef OPENGL_VERTEX_ARRAY_H
#define OPENGL_VERTEX_ARRAY_H

#include "OrangeExport.h"
#include "Renderer/VertexArray.h"

namespace Orange
{
    class ORANGE_API OpenGLVertexArray : public VertexArray
    {
    public:
        OpenGLVertexArray();
        virtual ~OpenGLVertexArray();

        virtual void Bind() const override;
        virtual void Unbind() const override;

        virtual void AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vertexBuffer) override;
        virtual void SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer) override;
        virtual const std::vector<std::shared_ptr<VertexBuffer>>& GetVertexBuffers() const { return mpVertexBuffers; }
        virtual const std::shared_ptr<IndexBuffer>& GetIndexBuffer() const { return mpIndexBuffer; }
    private:
        uint32_t mRendererID;
        std::vector<std::shared_ptr<VertexBuffer>> mpVertexBuffers;
        std::shared_ptr<IndexBuffer> mpIndexBuffer;
    };
}

#endif // !
