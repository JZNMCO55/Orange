#include "orgpch.h"

#include "VertexBuffer.h"

#include "RendererAPI.h"

namespace Orange
{
    Ref<VertexBuffer> VertexBuffer::Create(void *data, uint64_t size, VertexBufferUsage usage)
    {
        Ref<VertexBuffer> vertexBuffer = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // vertexBuffer = CreateRef<VulkanVertexBuffer>(data, size, usage);
        }
        }
        return vertexBuffer;
    }
    Ref<VertexBuffer> VertexBuffer::Create(uint64_t size, VertexBufferUsage usage)
    {
        Ref<VertexBuffer> vertexBuffer = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // vertexBuffer = CreateRef<VulkanVertexBuffer>(size, usage);
            break;
        }
        }
        return vertexBuffer;
    }
}