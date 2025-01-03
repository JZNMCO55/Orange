#ifndef VERTEX_ARRAY_H
#define VERTEX_ARRAY_H

#include "OrangeExport.h"
#include "Buffer.h"

namespace Orange
{
    class ORANGE_API VertexArray
    {
    public:
        virtual ~VertexArray() {};

        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;

        virtual void AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vertexBuffer) = 0;
        virtual void SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer) = 0;
        virtual const std::vector<std::shared_ptr<VertexBuffer>>& GetVertexBuffers() const = 0;
        virtual const std::shared_ptr<IndexBuffer>& GetIndexBuffer() const = 0;
        static VertexArray* Create();
    };
}

#endif // VERTEX_ARRAY_H