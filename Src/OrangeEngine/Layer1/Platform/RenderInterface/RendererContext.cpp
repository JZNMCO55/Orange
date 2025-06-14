#include "orgpch.h"

#include "RendererContext.h"

#include "RendererAPI.h"

namespace Orange
{
    Ref<RendererContext> RendererContext::Create()
    {
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // return CreateRef<VulkanRendererContext>();
        }
        }
        return nullptr;
    }
}