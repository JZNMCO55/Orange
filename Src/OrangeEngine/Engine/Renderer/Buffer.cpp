#include "pch.h"
#include "Buffer.h"
#include "Renderer.h"
#include "OpenGL/OpenGLBuffer.h"

namespace Orange
{
    Ref<VertexBuffer> VertexBuffer::Create(uint32_t size)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:
            {
                ORANGE_CORE_ASSERT(false, "RendererAPI::API::None is currently not supported!");
                return nullptr;
            }
            case RendererAPI::API::OpenGL:
            {
                return CreateRef<OpenGLVertexBuffer>(size);
            }
        }

        ORANGE_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    Ref<VertexBuffer> VertexBuffer::Create(float* vertices, uint32_t size)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:
            {
                ORANGE_CORE_ASSERT(false, "RendererAPI::API::None is currently not supported!");
                return nullptr;
            }
            case RendererAPI::API::OpenGL:
            {
                return CreateRef<OpenGLVertexBuffer>(vertices, size);
            }
        }
    }

    Ref<IndexBuffer> IndexBuffer::Create(uint32_t* indices, uint32_t count)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:
            {
                ORANGE_CORE_ASSERT(false, "RendererAPI::API::None is currently not supported!");
                return nullptr;
            }
            case RendererAPI::API::OpenGL:
            {
                return CreateRef<OpenGLIndexBuffer>(indices, count);
            }
        }

        ORANGE_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
}