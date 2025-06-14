#include "orgpch.h"
#include "Texture.h"

#include "RendererAPI.h"

namespace Orange
{
    Ref<Texture2D> Texture2D::Create(const TextureSpecification &specification)
    {
        Ref<Texture2D> texture = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // texture = CreateRef<VulkanTexture2D>(specification);
            break;
        }
        }
        return texture;
    }

    Ref<Texture2D> Texture2D::Create(const TextureSpecification &specification, const std::filesystem::path &filepath)
    {
        Ref<Texture2D> texture = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // texture = CreateRef<VulkanTexture2D>(specification, filepath);
            break;
        }
        }
        return texture;
    }

    Ref<Texture2D> Texture2D::Create(const TextureSpecification &specification, Buffer imageData)
    {
        Ref<Texture2D> texture = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // texture = CreateRef<VulkanTexture2D>(specification, imageData);
        }
        }
        return texture;
    }

    Ref<Texture2D> Texture2D::CreateFromSRGB(Ref<Texture2D> texture)
    {
        Ref<Texture2D> ptexture = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // texture = CreateRef<VulkanTexture2D>(texture);
            break;
        }
        }
        return ptexture;
    }

    Ref<TextureCube> TextureCube::Create(const TextureSpecification &specification, Buffer imageData)
    {
        Ref<TextureCube> texture = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // texture = CreateRef<VulkanTextureCube>(specification, imageData);
            break;
        }
        }
        return texture;
    }
}