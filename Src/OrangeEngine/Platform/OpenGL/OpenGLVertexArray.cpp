#include "pch.h"
#include "OpenGLVertexArray.h"

namespace Orange
{
    static GLenum ShaderDataTypeToOpenGLBaseType(EShaderDataType type)
    {
        switch (type)
        {
        case EShaderDataType::Float:    return GL_FLOAT;
        case EShaderDataType::Float2:   return GL_FLOAT;
        case EShaderDataType::Float3:   return GL_FLOAT;
        case EShaderDataType::Float4:   return GL_FLOAT;
        case EShaderDataType::Mat3:     return GL_FLOAT;
        case EShaderDataType::Mat4:     return GL_FLOAT;
        case EShaderDataType::Int:      return GL_INT;
        case EShaderDataType::Int2:     return GL_INT;
        case EShaderDataType::Int3:     return GL_INT;
        case EShaderDataType::Int4:     return GL_INT;
        case EShaderDataType::Bool:     return GL_BOOL;
        }

        ORANGE_CORE_ASSERT(false, "Unknown ShaderDataType!");
        return 0;
    }

    OpenGLVertexArray::OpenGLVertexArray()
    {
        glCreateVertexArrays(1, &mRendererID);
    }

    OpenGLVertexArray::~OpenGLVertexArray()
    {
        glDeleteVertexArrays(1, &mRendererID);
    }

    void OpenGLVertexArray::Bind() const
    {
        glBindVertexArray(mRendererID);
    }

    void OpenGLVertexArray::Unbind() const
    {
        glBindVertexArray(0);
    }

    void OpenGLVertexArray::AddVertexBuffer(const std::shared_ptr<VertexBuffer>& tpVertexBuffer)
    {
        ORANGE_CORE_ASSERT(tpVertexBuffer->GetLayout().GetElements().size(), "Vertex Buffer has no layout!");

        glBindVertexArray(mRendererID);

        tpVertexBuffer->Bind();

        uint32_t index = 0;

        const auto& layout = tpVertexBuffer->GetLayout();
        for (const auto& element : layout)
        {
            glEnableVertexAttribArray(index);
            glVertexAttribPointer(index,
                element.GetComponentCount(),
                ShaderDataTypeToOpenGLBaseType(element.Type),
                GL_FALSE,
                layout.GetStride(),
                (const void*)element.Offset);
            index++;
        }

        mpVertexBuffers.push_back(tpVertexBuffer);
    }

    void OpenGLVertexArray::SetIndexBuffer(const std::shared_ptr<IndexBuffer>& tpIndexBuffer)
    {
        glBindVertexArray(mRendererID);
        tpIndexBuffer->Bind();

        mpIndexBuffer = tpIndexBuffer;
    }


}