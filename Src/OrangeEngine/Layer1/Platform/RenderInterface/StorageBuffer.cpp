#include "orgpch.h"

#include "StorageBuffer.h"

#include "RendererAPI.h"

namespace Orange
{
    Ref<StorageBuffer> StorageBuffer::Create(uint32_t size, const StorageBufferSpecification &specification)
    {
        Ref<StorageBuffer> storageBuffer = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // storageBuffer = CreateRef<VulkanStorageBuffer>(size, specification);
            break;
        }
        }
        return storageBuffer;
    }
}