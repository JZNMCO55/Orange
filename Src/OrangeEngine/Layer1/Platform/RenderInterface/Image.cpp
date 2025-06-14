#include "orgpch.h"

#include "Image.h"
#include "RendererAPI.h"

namespace Orange
{
    Ref<Image2D> Image2D::Create(const ImageSpecification &spec, Buffer buffer)
    {
        Ref<Image> image = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // image = CreateRef<VulkanImage2D>(spec, buffer);
            break;
        }
        }
        return image;
    }

    Ref<ImageView> ImageView::Create(const ImageViewSpecification &spec)
    {
        Ref<ImageView> imageView = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // imageView = CreateRef<VulkanImageView>(spec);
            break;
        }
        }
        return imageView;
    }
}