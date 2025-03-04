#include "OpenGLPch.h"
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

        ORANGE_CORE_ASSERT(false, "Unknown EShaderDataType!");
        return 0;
    }

    OpenGLVertexArray::OpenGLVertexArray()
    {
        ORG_PROFILE_FUNCTION();

        glCreateVertexArrays(1, &mRendererID);
    }

    OpenGLVertexArray::~OpenGLVertexArray()
    {
        ORG_PROFILE_FUNCTION();

        glDeleteVertexArrays(1, &mRendererID);
    }

    void OpenGLVertexArray::Bind() const
    {
        ORG_PROFILE_FUNCTION();

        glBindVertexArray(mRendererID);
    }

    void OpenGLVertexArray::Unbind() const
    {
        ORG_PROFILE_FUNCTION();

        glBindVertexArray(0);
    }

    void OpenGLVertexArray::AddVertexBuffer(const std::shared_ptr<VertexBuffer>& tpVertexBuffer)
    {
        ORG_PROFILE_FUNCTION();

        ORANGE_CORE_ASSERT(tpVertexBuffer->GetLayout().GetElements().size(), "Vertex Buffer has no layout!");

        glBindVertexArray(mRendererID);

        tpVertexBuffer->Bind();

        uint32_t index = 0;

        const auto& layout = tpVertexBuffer->GetLayout();
        for (const auto& element : layout)
        {
            switch (element.Type)
            {
            case EShaderDataType::Float:
            case EShaderDataType::Float2:
            case EShaderDataType::Float3:
            case EShaderDataType::Float4:
            {
                glEnableVertexAttribArray(mVertexBufferIndex);
                glVertexAttribPointer(mVertexBufferIndex,
                    element.GetComponentCount(),
                    ShaderDataTypeToOpenGLBaseType(element.Type),
                    element.Normalized ? GL_TRUE : GL_FALSE,
                    layout.GetStride(),
                    (const void*)element.Offset);
                mVertexBufferIndex++;
                break;
            }
            case EShaderDataType::Int:
            case EShaderDataType::Int2:
            case EShaderDataType::Int3:
            case EShaderDataType::Int4:
            case EShaderDataType::Bool:
            {
                glEnableVertexAttribArray(mVertexBufferIndex);
                glVertexAttribIPointer(mVertexBufferIndex,
                    element.GetComponentCount(),
                    ShaderDataTypeToOpenGLBaseType(element.Type),
                    layout.GetStride(),
                    (const void*)element.Offset);
                mVertexBufferIndex++;
                break;
            }
            case EShaderDataType::Mat3:
            case EShaderDataType::Mat4:
            {
                uint8_t count = element.GetComponentCount();
                for (uint8_t i = 0; i < count; i++)
                {
                    glEnableVertexAttribArray(mVertexBufferIndex);
                    glVertexAttribPointer(mVertexBufferIndex,
                        count,
                        ShaderDataTypeToOpenGLBaseType(element.Type),
                        element.Normalized ? GL_TRUE : GL_FALSE,
                        layout.GetStride(),
                        (const void*)(element.Offset + sizeof(float) * count * i));
                    glVertexAttribDivisor(mVertexBufferIndex, 1);
                    mVertexBufferIndex++;
                }
                break;
            }
            default:
                ORANGE_CORE_ASSERT(false, "Unknown EShaderDataType!");
            }
        }

        mpVertexBuffers.push_back(tpVertexBuffer);
    }

    void OpenGLVertexArray::SetIndexBuffer(const std::shared_ptr<IndexBuffer>& tpIndexBuffer)
    {
        ORG_PROFILE_FUNCTION();

        glBindVertexArray(mRendererID);
        tpIndexBuffer->Bind();

        mpIndexBuffer = tpIndexBuffer;
    }


}