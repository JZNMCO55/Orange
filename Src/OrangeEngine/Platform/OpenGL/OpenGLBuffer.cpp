#include "OpenGLPch.h"
#include "OpenGLBuffer.h"

namespace Orange
{
#pragma region VertexBuffer

    OpenGLVertexBuffer::OpenGLVertexBuffer(uint32_t size)
    {
        ORG_PROFILE_FUNCTION();

        glCreateBuffers(1, &mRendererID);
        glBindBuffer(GL_ARRAY_BUFFER, mRendererID);
        glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
    }

    /////////////////////////////////////////////////////////////////////////////
    // VertexBuffer /////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////
    OpenGLVertexBuffer::OpenGLVertexBuffer(float* vertices, uint32_t size)
    {
        ORG_PROFILE_FUNCTION();

        glCreateBuffers(1, &mRendererID);
        glBindBuffer(GL_ARRAY_BUFFER, mRendererID);
        glBufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW);
    }

    OpenGLVertexBuffer::~OpenGLVertexBuffer()
    {
        ORG_PROFILE_FUNCTION();

        glDeleteBuffers(1, &mRendererID);
    }

    void OpenGLVertexBuffer::Bind() const
    {
        ORG_PROFILE_FUNCTION();

        glBindBuffer(GL_ARRAY_BUFFER, mRendererID);
    }

    void OpenGLVertexBuffer::Unbind() const
    {
        ORG_PROFILE_FUNCTION();

        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void OpenGLVertexBuffer::SetData(const void* data, uint32_t size)
    {
        glBindBuffer(GL_ARRAY_BUFFER, mRendererID);
        glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);
    }
#pragma endregion

#pragma region IndexBuffer
    /////////////////////////////////////////////////////////////////////////////
    // IndexBuffer //////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////
    OpenGLIndexBuffer::OpenGLIndexBuffer(uint32_t* indices, uint32_t count)
        : mCount(count)
    {
        ORG_PROFILE_FUNCTION();

        glCreateBuffers(1, &mRendererID);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mRendererID);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(uint32_t), indices, GL_STATIC_DRAW);
    }
    OpenGLIndexBuffer::~OpenGLIndexBuffer()
    {
        ORG_PROFILE_FUNCTION();

        glDeleteBuffers(1, &mRendererID);
    }
    void OpenGLIndexBuffer::Bind() const
    {
        ORG_PROFILE_FUNCTION();

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mRendererID);
    }
    void OpenGLIndexBuffer::Unbind() const
    {
        ORG_PROFILE_FUNCTION();

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
#pragma endregion
}