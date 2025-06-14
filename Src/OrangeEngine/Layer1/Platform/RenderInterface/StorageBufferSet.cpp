#include "orgpch.h"

#include "StorageBufferSet.h"

#include "RendererAPI.h"

namespace Orange
{
    Ref<StorageBufferSet> StorageBufferSet::Create(const StorageBufferSpecification &specification, uint32_t size, uint32_t framesInFlight)
    {
        Ref<StorageBufferSet> storageBufferSet = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // storageBufferSet = CreateRef<VulkanStorageBufferSet>(specification, size, framesInFlight);
            break;
        }
        }
        return storageBufferSet;
    }
}