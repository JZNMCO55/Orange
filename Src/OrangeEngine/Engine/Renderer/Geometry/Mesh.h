#ifndef MESH_H
#define MESH_H

#include "OrangeExport.h"
#include "Renderer/Buffer.h"

namespace Orange
{
    class ORANGE_API Mesh
    {
    public:
        Mesh();
        Mesh(const Ref<VertexBuffer>& vertexBuffer, const Ref<IndexBuffer>& indexBuffer);
        Mesh(const std::vector<float>& vertices, const std::vector<uint32_t>& indices);
        ~Mesh();

        void SetVertices(const std::vector<float>& vertices);
        void SetIndices(const std::vector<uint32_t>& indices);
        void SetVerticesLayout(const BufferLayout& layout);

        const std::vector<float>& GetVertices();
        const std::vector<uint32_t>& GetIndices();

        const Ref<VertexBuffer>& GetVertexBuffer();
        const Ref<IndexBuffer>& GetIndexBuffer();

    private:
        void CreateBuffer();
        void MarkBuffersDirty();
        void ClearBuffersDirty();
        void ClearBuffers();
    private:
        std::vector<float> mVertices;
        std::vector<uint32_t> mIndices;
        Ref<VertexBuffer> mpVertexBuffer;
        Ref<IndexBuffer> mpIndexBuffer;
        BufferLayout mVerticesLayout;
        bool mbBuffersDirty;
    };
}

#endif // MESH_H