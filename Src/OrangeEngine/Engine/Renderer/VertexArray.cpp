#include "pch.h"
#include "VertexArray.h"

#include "Renderer.h"
#include "OpenGL/OpenGLVertexArray.h"

namespace Orange
{
    VertexArray* VertexArray::Create()
    {
        switch (Renderer::GetAPI())
        {
        case RendererAPI::OpenGL:
        {
            return new OpenGLVertexArray();
        }
        case RendererAPI::None:
            break;
        default:
            break;
        }
        ORANGE_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
        return nullptr;
    }
}