#include "orgpch.h"
#include "IndexBuffer.h"

#include "RendererAPI.h"

namespace Orange
{
    Ref<IndexBuffer> IndexBuffer::Create(uint64_t size)
    {
        Ref<IndexBuffer> indexBuffer = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // indexBuffer = CreateRef<VulkanIndexBuffer>(size);
        }
        }
        return indexBuffer;
    }

    Ref<IndexBuffer> IndexBuffer::Create(void *data, uint64_t size)
    {
        Ref<IndexBuffer> indexBuffer = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // indexBuffer = CreateRef<VulkanIndexBuffer>(data, size);
        }
        }
        return indexBuffer;
    }
}