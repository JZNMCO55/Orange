/**
 * @file VulkanSwapChain.h
 * @brief Vulkan交换链实现
 */

#ifndef ORANGE_VULKAN_SWAP_CHAIN_H
#define ORANGE_VULKAN_SWAP_CHAIN_H

#include "../../../GraphicsInterface/RenderInterface/ISwapChain.h"
#include "../VulkanCommon/VulkanCommon.h"
#include <vector>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            class VulkanDevice;

            /**
             * @brief Vulkan交换链实现
             */
            class VulkanSwapChain : public ISwapChain
            {
            public:
                VulkanSwapChain(VulkanDevice *device);
                virtual ~VulkanSwapChain();

                // ISwapChain接口实现
                virtual uint32_t GetImageCount() const override { return static_cast<uint32_t>(m_swapChainImages.size()); }
                virtual uint32_t GetCurrentImageIndex() const override { return m_currentImageIndex; }
                virtual uint32_t GetWidth() const override { return m_extent.width; }
                virtual uint32_t GetHeight() const override { return m_extent.height; }
                virtual PixelFormat GetFormat() const override { return m_format; }
                virtual IRenderTexture *GetImage(uint32_t index) const override;
                virtual uint32_t AcquireNextImage(ISemaphore *signalSemaphore) override;
                virtual bool Present(const std::vector<ISemaphore *> &waitSemaphores = {}) override;
                virtual bool Resize(uint32_t width, uint32_t height) override;
                virtual bool IsVSyncEnabled() const override { return m_vsyncEnabled; }
                virtual void SetVSyncEnabled(bool enabled) override;

                // Vulkan特定方法
                VkSwapchainKHR GetVkSwapChain() const { return m_swapChain; }
                VkSurfaceKHR GetVkSurface() const { return m_surface; }
                const VkExtent2D &GetExtent() const { return m_extent; }

                // 初始化方法
                bool Initialize(const SwapChainCreateInfo &createInfo);
                void Shutdown();

            private:
                VulkanDevice *m_device;
                VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
                VkSurfaceKHR m_surface = VK_NULL_HANDLE;

                std::vector<VkImage> m_swapChainImages;
                std::vector<VkImageView> m_swapChainImageViews;
                std::vector<IRenderTexture *> m_renderTextures;

                VkFormat m_swapChainImageFormat;
                VkExtent2D m_extent;
                PixelFormat m_format;
                uint32_t m_currentImageIndex = 0;
                bool m_vsyncEnabled = true;

                // 工具方法
                bool CreateSwapChain(uint32_t width, uint32_t height);
                bool CreateImageViews();
                bool RecreateSwapChain(uint32_t width, uint32_t height);
                void CleanupSwapChain();

                VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats);
                VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes);
                VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities, uint32_t width, uint32_t height);
            };

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_SWAP_CHAIN_H