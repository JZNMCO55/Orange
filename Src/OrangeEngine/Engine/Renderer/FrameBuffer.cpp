#include "pch.h"

#include "FrameBuffer.h"

#include "Renderer/Renderer.h"
#include "OpenGL/OpenGLFrameBuffer.h"

namespace Orange
{
    Ref<FrameBuffer> FrameBuffer::Create(const FrameBufferSpecification& spec)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:
            {
                ORANGE_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
                return nullptr;
            }
            case RendererAPI::API::OpenGL:
            {
                return CreateRef<OpenGLFrameBuffer>(spec);
            }
        }

        ORANGE_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
}