#include "OpenGLPch.h"
#include "Renderer/UniformBuffer.h"
#include "OpenGLUniformBuffer.h"

namespace Orange
{
    OpenGLUniformBuffer::OpenGLUniformBuffer(uint32_t size, uint32_t binding)
    {
        glCreateBuffers(1, &mRendererID);
        // Todo : investigate usage hint
        glNamedBufferData(mRendererID, size, nullptr, GL_DYNAMIC_DRAW); 
        glBindBufferBase(GL_UNIFORM_BUFFER, binding, mRendererID);
    }

    OpenGLUniformBuffer::~OpenGLUniformBuffer()
    {
        glDeleteBuffers(1, &mRendererID);
    }

    void OpenGLUniformBuffer::SetData(const void* data, uint32_t size, uint32_t offset)
    {
        glNamedBufferSubData(mRendererID, offset, size, data);
    }
}