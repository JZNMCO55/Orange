#include "pch.h"
#include "VertexArray.h"

#include "Renderer.h"
#include "OpenGL/OpenGLVertexArray.h"

namespace Orange
{
    Ref<VertexArray> VertexArray::Create()
    {
        switch (Renderer::GetAPI())
        {
        case RendererAPI::API::OpenGL:
        {
            return std::make_shared<OpenGLVertexArray>();
        }
        case RendererAPI::API::None:
            break;
        default:
            break;
        }
        ORANGE_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
        return nullptr;
    }
}