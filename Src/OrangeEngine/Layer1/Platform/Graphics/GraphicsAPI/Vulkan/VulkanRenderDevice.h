#ifndef ORANGE_VULKAN_VULKANRENDERDEVICE_H
#define ORANGE_VULKAN_VULKANRENDERDEVICE_H

#include "../../GraphicsInterface/IRenderDevice.h"
#include "../../GraphicsInterface/IPipeline.h"
#include "../../GraphicsInterface/IBuffer.h"
#include "../../GraphicsInterface/ITexture.h"
#include <vulkan/vulkan.h>
#include <memory>

namespace Orange::Graphics::Vulkan
{

    // 前向声明
    class VulkanGraphicsSystem;

    /**
     * @brief Vulkan渲染设备实现
     */
    class VulkanRenderDevice : public IRenderDevice
    {
    public:
        explicit VulkanRenderDevice(VulkanGraphicsSystem *graphicsSystem);
        ~VulkanRenderDevice() override;

        // IRenderDevice接口实现
        std::unique_ptr<IBuffer> CreateBuffer(const BufferCreateInfo &info) override;
        std::unique_ptr<ITexture> CreateTexture(const TextureCreateInfo &info) override;
        std::unique_ptr<IPipeline> CreatePipeline(const PipelineCreateInfo &info) override;
        void WaitIdle() override;
        std::string GetDeviceName() const override;
        uint64_t GetAvailableMemory() const override;
        uint64_t GetUsedMemory() const override;
        std::string GetDeviceType() const override;
        std::string GetDriverVersion() const override;
        bool SupportsFeature(const std::string &feature) const override;

        // Vulkan特定接口
        VkDevice GetVulkanDevice() const;
        VkPhysicalDevice GetVulkanPhysicalDevice() const;

    private:
        VulkanGraphicsSystem *m_graphicsSystem;
        VkDevice m_device;
        VkPhysicalDevice m_physicalDevice;
        std::string m_deviceName;

        // 设备属性
        VkPhysicalDeviceProperties m_deviceProperties;
        VkPhysicalDeviceMemoryProperties m_memoryProperties;
    };

} // namespace Orange::Graphics::Vulkan

#endif // ORANGE_VULKAN_VULKANRENDERDEVICE_H