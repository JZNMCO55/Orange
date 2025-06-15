#include "orgpch.h"
#include "VulkanAPI.h"

#include "VulkanContext.h"

#ifdef TODO
#include "Platform/RenderInterface/RendererStats.h"
#endif

namespace Orange::Vulkan
{

    VkDescriptorSetAllocateInfo DescriptorSetAllocInfo(const VkDescriptorSetLayout *layouts, uint32_t count, VkDescriptorPool pool)
    {
        VkDescriptorSetAllocateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        info.pSetLayouts = layouts;
        info.descriptorSetCount = count;
        info.descriptorPool = pool;
        return info;
    }

    VkSampler CreateSampler(VkSamplerCreateInfo samplerCreateInfo)
    {
        auto device = VulkanContext::GetCurrentDevice();
        VkDevice vulkanDevice = device->GetVulkanDevice();

        VkSampler sampler;
        VK_CHECK_RESULT(vkCreateSampler(vulkanDevice, &samplerCreateInfo, nullptr, &sampler));

#ifdef TODO
        RendererUtils::GetResourceAllocationCounts().Samplers++;
#endif

        return sampler;
    }

    void DestroySampler(VkSampler sampler)
    {
        auto device = VulkanContext::GetCurrentDevice();
        VkDevice vulkanDevice = device->GetVulkanDevice();
        vkDestroySampler(vulkanDevice, sampler, nullptr);

#ifdef TODO
        RendererUtils::GetResourceAllocationCounts().Samplers--;
#endif
    }

}
