/**
 * @file VulkanDevice.h
 * @brief Vulkan渲染设备实现
 */

#ifndef ORANGE_VULKAN_DEVICE_H
#define ORANGE_VULKAN_DEVICE_H

#include "../../../GraphicsInterface/RenderInterface/IRenderDevice.h"
#include "../VulkanCommon/VulkanCommon.h"
#include <vector>
#include <memory>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            /**
             * @brief Vulkan渲染设备实现
             */
            class VulkanDevice : public IRenderDevice
            {
            public:
                VulkanDevice();
                virtual ~VulkanDevice();

                // IRenderDevice接口实现
                virtual bool Initialize(const DeviceCreateInfo &createInfo) override;
                virtual void Shutdown() override;
                virtual bool SupportsFeature(RenderFeature feature) const override;
                virtual void WaitIdle() override;
                virtual RenderAPI GetRenderAPI() const override { return RenderAPI::Vulkan; }

                // 资源创建方法（核心）
                virtual IRenderBuffer *CreateBuffer(const BufferCreateInfo &createInfo) override;
                virtual IRenderTexture *CreateTexture(const TextureCreateInfo &createInfo) override;
                virtual IRenderSampler *CreateSampler(const SamplerCreateInfo &createInfo) override;
                virtual IShaderModule *CreateShaderModule(const ShaderCreateInfo &createInfo) override;
                virtual IRenderPipeline *CreateGraphicsPipeline(const GraphicsPipelineCreateInfo &createInfo) override;
                virtual IRenderPass *CreateRenderPass(const RenderPassCreateInfo &createInfo) override;
                virtual ISwapChain *CreateSwapChain(const SwapChainCreateInfo &createInfo) override;
                virtual IRenderFence*CreateFence(bool signaled = false) override;
                virtual IRenderSemaphore*CreateSemaphore() override;

                // 上下文创建
                virtual IRenderContext *CreateContext() override;

                // Vulkan特定方法
                VkDevice GetVkDevice() const { return m_device; }
                VkPhysicalDevice GetVkPhysicalDevice() const { return m_physicalDevice; }
                VkInstance GetVkInstance() const { return m_instance; }
                VkQueue GetGraphicsQueue() const { return m_graphicsQueue; }
                VkQueue GetPresentQueue() const { return m_presentQueue; }
                VkCommandPool GetCommandPool() const { return m_commandPool; }
                const QueueFamilyIndices &GetQueueFamilyIndices() const { return m_queueFamilyIndices; }

            private:
                // Vulkan对象
                VkInstance m_instance = VK_NULL_HANDLE;
                VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
                VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
                VkDevice m_device = VK_NULL_HANDLE;
                VkSurfaceKHR m_surface = VK_NULL_HANDLE;

                // 队列
                VkQueue m_graphicsQueue = VK_NULL_HANDLE;
                VkQueue m_presentQueue = VK_NULL_HANDLE;
                QueueFamilyIndices m_queueFamilyIndices;

                // 命令池
                VkCommandPool m_commandPool = VK_NULL_HANDLE;

                // 配置
                DeviceCreateInfo m_createInfo;
                bool m_enableValidationLayers = false;

                // 初始化方法
                bool CreateInstance();
                bool SetupDebugMessenger();
                bool CreateSurface();
                bool PickPhysicalDevice();
                bool CreateLogicalDevice();
                bool CreateCommandPool();

                // 工具方法
                bool IsDeviceSuitable(VkPhysicalDevice device);
                std::vector<const char *> GetRequiredExtensions();
                bool CheckValidationLayerSupport();

                // 调试回调
                static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
                    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                    VkDebugUtilsMessageTypeFlagsEXT messageType,
                    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                    void *pUserData);
            };

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_DEVICE_H