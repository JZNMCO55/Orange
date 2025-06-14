#include "orgpch.h"

#include "UniformBufferSet.h"

#include "RendererAPI.h"

namespace Orange
{
    Ref<UniformBufferSet> UniformBufferSet::Create(uint32_t size, uint32_t framesInFlight)
    {
        Ref<UniformBufferSet> uniformBufferSet = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // uniformBufferSet = CreateRef<VulkanUniformBufferSet>(size, framesInFlight);
            break;
        }
        }
        return uniformBufferSet;
    }
}