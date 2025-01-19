#include "pch.h"

#include "Mesh.h"

namespace Orange
{
    Mesh::Mesh()
    {

    }

    Mesh::Mesh(const Ref<VertexBuffer>& vertexBuffer, const Ref<IndexBuffer>& indexBuffer)
        : mpVertexBuffer(vertexBuffer), mpIndexBuffer(indexBuffer), mbBuffersDirty(false)
    {
    }

    Mesh::Mesh(const std::vector<float>& vertices, const std::vector<uint32_t>& indices)
        : mVertices(vertices), mIndices(indices), mbBuffersDirty(true)
    {
        mVerticesLayout = {
            {EShaderDataType::Float3, "a_Position"},
            {EShaderDataType::Float4, "a_Color"},
        };
    }

    Mesh::~Mesh()
    {
        ClearBuffers();
        mpVertexBuffer = nullptr;
        mpIndexBuffer = nullptr;
    }
    void Mesh::SetVertices(const std::vector<float>& vertices)
    {
        mVertices = vertices;
        MarkBuffersDirty();
    }

    void Mesh::SetIndices(const std::vector<uint32_t>& indices)
    {
        mIndices = indices;
        MarkBuffersDirty();
    }

    void Mesh::SetVerticesLayout(const BufferLayout& layout)
    {
        mVerticesLayout = layout;
    }

    const std::vector<float>& Mesh::GetVertices()
    {
        return mVertices;
    }

    const std::vector<uint32_t>& Mesh::GetIndices()
    {
        return mIndices;
    }

    const Ref<VertexBuffer>& Mesh::GetVertexBuffer()
    {
        CreateBuffer();
        return mpVertexBuffer;
    }

    const Ref<IndexBuffer>& Mesh::GetIndexBuffer()
    {
        CreateBuffer();
        return mpIndexBuffer;
    }

    // Private
    void Mesh::CreateBuffer()
    {
        if (mbBuffersDirty)
        {
            mpVertexBuffer = VertexBuffer::Create(mVertices.data(), mVertices.size());
            mpIndexBuffer = IndexBuffer::Create(mIndices.data(), mIndices.size());
            mpVertexBuffer->SetLayout(mVerticesLayout);
            ClearBuffersDirty();
        }
    }


    void Mesh::MarkBuffersDirty()
    {
        mbBuffersDirty = true;
    }

    void Mesh::ClearBuffersDirty()
    {
        mbBuffersDirty = false;
    }

    void Mesh::ClearBuffers()
    {
        mVertices.clear();
        mIndices.clear();
    }
}