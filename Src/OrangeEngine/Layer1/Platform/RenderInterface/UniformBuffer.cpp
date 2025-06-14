#include "orgpch.h"

#include "UniformBuffer.h"

#include "RendererAPI.h"

namespace Orange
{
    Ref<UniformBuffer> UniformBuffer::Create(uint32_t size)
    {
        Ref<UniformBuffer> uniformBuffer = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // uniformBuffer = CreateRef<VulkanUniformBuffer>(size);
            break;
        }
        }
        return uniformBuffer;
    }
}