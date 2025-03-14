#include "pch.h"
#include "UniformBuffer.h"

#include "OpenGL/OpenGLUniformBuffer.h"
#include "Renderer.h"

namespace Orange
{
    Ref<UniformBuffer> UniformBuffer::Create(uint32_t size, uint32_t binding)
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
                return CreateRef<OpenGLUniformBuffer>(size, binding);
            }
        }

        ORANGE_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
}