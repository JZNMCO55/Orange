#include "orgpch.h"
#include "RenderCommandBuffer.h"

#include "RendererAPI.h"

namespace Orange
{
    Ref<RenderCommandBuffer> RenderCommandBuffer::Create(uint32_t count, const std::string &debugName)
    {
        Ref<RenderCommandBuffer> commandBuffer = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // commandBuffer = CreateRef<VulkanRenderCommandBuffer>(count, debugName);
        }
        }
        return commandBuffer;
    }

    Ref<RenderCommandBuffer> RenderCommandBuffer::CreateFromSwapChain(const std::string &debugName)
    {
        Ref<RenderCommandBuffer> commandBuffer = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // commandBuffer = CreateRef<VulkanRenderCommandBuffer>(debugName);
        }
        }
        return commandBuffer;
    }
}
