#include "orgpch.h"

#include "Framebuffer.h"

#include "RendererAPI.h"

namespace Orange
{
    Ref<Framebuffer> Framebuffer::Create(const FramebufferSpecification &spec)
    {
        Ref<Framebuffer> framebuffer = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // framebuffer = CreateRef<VulkanFramebuffer>(spec);
            break;
        }
        }
        return framebuffer;
    }
}