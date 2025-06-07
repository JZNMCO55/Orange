#include "VulkanRenderDevice.h"
#include "VulkanGraphicsSystem.h"
#include "Orange.h"
#include <stdexcept>
#include <iostream>

namespace Orange::Graphics::Vulkan
{

    VulkanRenderDevice::VulkanRenderDevice(VulkanGraphicsSystem *graphicsSystem)
        : m_graphicsSystem(graphicsSystem), m_device(graphicsSystem->GetVulkanDevice()), m_physicalDevice(graphicsSystem->GetVulkanPhysicalDevice())
    {
        // 获取设备属性
        vkGetPhysicalDeviceProperties(m_physicalDevice, &m_deviceProperties);
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_memoryProperties);

        m_deviceName = m_deviceProperties.deviceName;
        ORG_LOG_INFO("VulkanRenderDevice initialized for: {}", m_deviceName);
    }

    VulkanRenderDevice::~VulkanRenderDevice()
    {
        ORG_LOG_INFO("VulkanRenderDevice destroyed");
    }

    std::unique_ptr<Buffer> VulkanRenderDevice::CreateBuffer(const BufferCreateInfo &info)
    {
        ORG_LOG_DEBUG("CreateBuffer called (not implemented yet)");
        return nullptr;
    }

    std::unique_ptr<Texture> VulkanRenderDevice::CreateTexture(const TextureCreateInfo &info)
    {
        ORG_LOG_DEBUG("CreateTexture called (not implemented yet)");
        return nullptr;
    }

    std::unique_ptr<Pipeline> VulkanRenderDevice::CreatePipeline(const PipelineCreateInfo &info)
    {
        ORG_LOG_DEBUG("CreatePipeline called (not implemented yet)");
        return nullptr;
    }

    void VulkanRenderDevice::WaitIdle()
    {
        vkDeviceWaitIdle(m_device);
    }

    std::string VulkanRenderDevice::GetDeviceName() const
    {
        return m_deviceName;
    }

    uint64_t VulkanRenderDevice::GetAvailableMemory() const
    {
        // TODO: 实现内存查询
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

        uint64_t totalMemory = 0;
        for (uint32_t i = 0; i < memProperties.memoryHeapCount; i++)
        {
            if (memProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            {
                totalMemory += memProperties.memoryHeaps[i].size;
            }
        }

        return totalMemory;
    }

    uint64_t VulkanRenderDevice::GetUsedMemory() const
    {
        // TODO: 实现已使用内存统计
        return 0;
    }

    VkDevice VulkanRenderDevice::GetVulkanDevice() const
    {
        return m_device;
    }

    VkPhysicalDevice VulkanRenderDevice::GetVulkanPhysicalDevice() const
    {
        return m_physicalDevice;
    }

    std::string VulkanRenderDevice::GetDeviceType() const
    {
        switch (m_deviceProperties.deviceType)
        {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return "Integrated GPU";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return "Discrete GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            return "Virtual GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            return "CPU";
        default:
            return "Unknown";
        }
    }

    std::string VulkanRenderDevice::GetDriverVersion() const
    {
        uint32_t version = m_deviceProperties.driverVersion;
        return std::to_string(VK_VERSION_MAJOR(version)) + "." +
               std::to_string(VK_VERSION_MINOR(version)) + "." +
               std::to_string(VK_VERSION_PATCH(version));
    }

    bool VulkanRenderDevice::SupportsFeature(const std::string &feature) const
    {
        // TODO: 实现功能支持检查
        ORG_LOG_DEBUG("SupportsFeature called for: {} (not implemented yet)", feature);
        return false;
    }

} // namespace Orange::Graphics::Vulkan