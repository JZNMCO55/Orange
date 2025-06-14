#include "orgpch.h"

#include "Material.h"
#include "RendererAPI.h"

namespace Orange
{
    Ref<Material> Material::Create(const Ref<Shader> &shader, const std::string &name)
    {
        Ref<Material> material = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // material = CreateRef<VulkanMaterial>(shader, name);
        }
        }
        return material;
    }

    Ref<Material> Material::Copy(const Ref<Material> &other, const std::string &name)
    {
        Ref<Material> material = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // material = CreateRef<VulkanMaterial>(other, name);
        }
        }
    }
}