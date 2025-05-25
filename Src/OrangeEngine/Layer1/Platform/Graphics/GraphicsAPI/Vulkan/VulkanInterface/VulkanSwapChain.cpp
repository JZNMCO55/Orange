/**
 * @file VulkanSwapChain.cpp
 * @brief Vulkan交换链实现
 */

#include "VulkanSwapChain.h"
#include "VulkanDevice.h"
#include "../VulkanResources/VulkanTexture.h"
#include "../VulkanCommon/VulkanCommon.h"
#include <algorithm>
#include <limits>
#include <iostream>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            VulkanSwapChain::VulkanSwapChain(VulkanDevice *device)
                : m_device(device)
            {
            }

            VulkanSwapChain::~VulkanSwapChain()
            {
                Shutdown();
            }

            bool VulkanSwapChain::Initialize(const SwapChainCreateInfo &createInfo)
            {
                if (!m_device)
                {
                    std::cerr << "VulkanSwapChain::Initialize: 无效的设备指针" << std::endl;
                    return false;
                }

                // 获取表面句柄
                m_surface = static_cast<VkSurfaceKHR>(createInfo.surface);
                if (m_surface == VK_NULL_HANDLE)
                {
                    std::cerr << "VulkanSwapChain::Initialize: 无效的表面句柄" << std::endl;
                    return false;
                }

                m_vsyncEnabled = createInfo.vsync;

                // 创建交换链
                if (!CreateSwapChain(createInfo.width, createInfo.height))
                {
                    std::cerr << "VulkanSwapChain::Initialize: 创建交换链失败" << std::endl;
                    return false;
                }

                // 创建图像视图
                if (!CreateImageViews())
                {
                    std::cerr << "VulkanSwapChain::Initialize: 创建图像视图失败" << std::endl;
                    return false;
                }

                std::cout << "VulkanSwapChain::Initialize: 交换链初始化成功，图像数量: "
                          << m_swapChainImages.size() << std::endl;

                return true;
            }

            void VulkanSwapChain::Shutdown()
            {
                if (m_device && m_device->GetVkDevice() != VK_NULL_HANDLE)
                {
                    CleanupSwapChain();
                }
            }

            IRenderTexture *VulkanSwapChain::GetImage(uint32_t index) const
            {
                if (index >= m_renderTextures.size())
                {
                    return nullptr;
                }
                return m_renderTextures[index].get();
            }

            IRenderTexture *VulkanSwapChain::GetCurrentImage() const
            {
                return GetImage(m_currentImageIndex);
            }

            uint32_t VulkanSwapChain::AcquireNextImage(ISemaphore *signalSemaphore)
            {
                VkSemaphore vkSemaphore = VK_NULL_HANDLE;
                if (signalSemaphore)
                {
                    // 这里需要从ISemaphore获取VkSemaphore，暂时使用nullptr
                    // 在实际实现中需要添加VulkanSemaphore类
                }

                VkResult result = vkAcquireNextImageKHR(
                    m_device->GetVkDevice(),
                    m_swapChain,
                    UINT64_MAX,
                    vkSemaphore,
                    VK_NULL_HANDLE,
                    &m_currentImageIndex);

                if (result == VK_ERROR_OUT_OF_DATE_KHR)
                {
                    // 交换链过期，需要重新创建
                    return UINT32_MAX;
                }
                else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
                {
                    std::cerr << "VulkanSwapChain::AcquireNextImage: 获取下一个图像失败" << std::endl;
                    return UINT32_MAX;
                }

                return m_currentImageIndex;
            }

            bool VulkanSwapChain::Present(const std::vector<ISemaphore *> &waitSemaphores)
            {
                std::vector<VkSemaphore> vkWaitSemaphores;
                for (auto semaphore : waitSemaphores)
                {
                    if (semaphore)
                    {
                        // 这里需要从ISemaphore获取VkSemaphore
                        // 暂时跳过
                    }
                }

                VkPresentInfoKHR presentInfo{};
                presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                presentInfo.waitSemaphoreCount = static_cast<uint32_t>(vkWaitSemaphores.size());
                presentInfo.pWaitSemaphores = vkWaitSemaphores.data();

                VkSwapchainKHR swapChains[] = {m_swapChain};
                presentInfo.swapchainCount = 1;
                presentInfo.pSwapchains = swapChains;
                presentInfo.pImageIndices = &m_currentImageIndex;
                presentInfo.pResults = nullptr;

                VkResult result = vkQueuePresentKHR(m_device->GetPresentQueue(), &presentInfo);

                if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
                {
                    // 交换链需要重新创建
                    return false;
                }
                else if (result != VK_SUCCESS)
                {
                    std::cerr << "VulkanSwapChain::Present: 呈现失败" << std::endl;
                    return false;
                }

                return true;
            }

            bool VulkanSwapChain::Resize(uint32_t width, uint32_t height)
            {
                if (width == 0 || height == 0)
                {
                    return false;
                }

                // 等待设备空闲
                vkDeviceWaitIdle(m_device->GetVkDevice());

                return RecreateSwapChain(width, height);
            }

            void VulkanSwapChain::SetVSyncEnabled(bool enabled)
            {
                if (m_vsyncEnabled != enabled)
                {
                    m_vsyncEnabled = enabled;
                    // 重新创建交换链以应用新的呈现模式
                    RecreateSwapChain(m_extent.width, m_extent.height);
                }
            }

            IRenderDevice* VulkanSwapChain::GetDevice() const
            {
                return m_device;;
            }

            bool VulkanSwapChain::CreateSwapChain(uint32_t width, uint32_t height)
            {
                SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(
                    m_device->GetVkPhysicalDevice(), m_surface);

                VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
                VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
                VkExtent2D extent = ChooseSwapExtent(swapChainSupport.capabilities, width, height);

                uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
                if (swapChainSupport.capabilities.maxImageCount > 0 &&
                    imageCount > swapChainSupport.capabilities.maxImageCount)
                {
                    imageCount = swapChainSupport.capabilities.maxImageCount;
                }

                VkSwapchainCreateInfoKHR createInfo{};
                createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
                createInfo.surface = m_surface;
                createInfo.minImageCount = imageCount;
                createInfo.imageFormat = surfaceFormat.format;
                createInfo.imageColorSpace = surfaceFormat.colorSpace;
                createInfo.imageExtent = extent;
                createInfo.imageArrayLayers = 1;
                createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

                QueueFamilyIndices indices = FindQueueFamilies(m_device->GetVkPhysicalDevice(), m_surface);
                uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

                if (indices.graphicsFamily != indices.presentFamily)
                {
                    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
                    createInfo.queueFamilyIndexCount = 2;
                    createInfo.pQueueFamilyIndices = queueFamilyIndices;
                }
                else
                {
                    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
                    createInfo.queueFamilyIndexCount = 0;
                    createInfo.pQueueFamilyIndices = nullptr;
                }

                createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
                createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
                createInfo.presentMode = presentMode;
                createInfo.clipped = VK_TRUE;
                createInfo.oldSwapchain = VK_NULL_HANDLE;

                if (vkCreateSwapchainKHR(m_device->GetVkDevice(), &createInfo, nullptr, &m_swapChain) != VK_SUCCESS)
                {
                    std::cerr << "VulkanSwapChain::CreateSwapChain: 创建交换链失败" << std::endl;
                    return false;
                }

                // 获取交换链图像
                vkGetSwapchainImagesKHR(m_device->GetVkDevice(), m_swapChain, &imageCount, nullptr);
                m_swapChainImages.resize(imageCount);
                vkGetSwapchainImagesKHR(m_device->GetVkDevice(), m_swapChain, &imageCount, m_swapChainImages.data());

                m_swapChainImageFormat = surfaceFormat.format;
                m_extent = extent;

                // 转换格式
                switch (m_swapChainImageFormat)
                {
                case VK_FORMAT_B8G8R8A8_UNORM:
                    m_format = PixelFormat::BGRA8_UNORM;
                    break;
                case VK_FORMAT_B8G8R8A8_SRGB:
                    m_format = PixelFormat::BGRA8_SRGB;
                    break;
                case VK_FORMAT_R8G8B8A8_UNORM:
                    m_format = PixelFormat::RGBA8_UNORM;
                    break;
                case VK_FORMAT_R8G8B8A8_SRGB:
                    m_format = PixelFormat::RGBA8_SRGB;
                    break;
                default:
                    m_format = PixelFormat::UNKNOWN;
                    break;
                }

                return true;
            }

            bool VulkanSwapChain::CreateImageViews()
            {
                m_swapChainImageViews.resize(m_swapChainImages.size());
                m_renderTextures.resize(m_swapChainImages.size());

                for (size_t i = 0; i < m_swapChainImages.size(); i++)
                {
                    VkImageViewCreateInfo createInfo{};
                    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                    createInfo.image = m_swapChainImages[i];
                    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                    createInfo.format = m_swapChainImageFormat;
                    createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
                    createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
                    createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
                    createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
                    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    createInfo.subresourceRange.baseMipLevel = 0;
                    createInfo.subresourceRange.levelCount = 1;
                    createInfo.subresourceRange.baseArrayLayer = 0;
                    createInfo.subresourceRange.layerCount = 1;

                    if (vkCreateImageView(m_device->GetVkDevice(), &createInfo, nullptr, &m_swapChainImageViews[i]) != VK_SUCCESS)
                    {
                        std::cerr << "VulkanSwapChain::CreateImageViews: 创建图像视图失败，索引: " << i << std::endl;
                        return false;
                    }

                    // 创建纹理包装对象
                    auto texture = std::make_unique<VulkanTexture>(m_device);
                    if (!texture->InitializeFromExisting(
                            m_swapChainImages[i],
                            m_swapChainImageFormat,
                            m_extent.width,
                            m_extent.height))
                    {
                        std::cerr << "VulkanSwapChain::CreateImageViews: 创建纹理包装失败，索引: " << i << std::endl;
                        return false;
                    }

                    m_renderTextures[i] = std::move(texture);
                }

                return true;
            }

            bool VulkanSwapChain::RecreateSwapChain(uint32_t width, uint32_t height)
            {
                CleanupSwapChain();

                if (!CreateSwapChain(width, height))
                {
                    return false;
                }

                if (!CreateImageViews())
                {
                    return false;
                }

                return true;
            }

            void VulkanSwapChain::CleanupSwapChain()
            {
                // 清理纹理包装对象
                m_renderTextures.clear();

                // 清理图像视图
                for (auto imageView : m_swapChainImageViews)
                {
                    if (imageView != VK_NULL_HANDLE)
                    {
                        vkDestroyImageView(m_device->GetVkDevice(), imageView, nullptr);
                    }
                }
                m_swapChainImageViews.clear();

                // 清理交换链
                if (m_swapChain != VK_NULL_HANDLE)
                {
                    vkDestroySwapchainKHR(m_device->GetVkDevice(), m_swapChain, nullptr);
                    m_swapChain = VK_NULL_HANDLE;
                }

                m_swapChainImages.clear();
            }

            VkSurfaceFormatKHR VulkanSwapChain::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats)
            {
                for (const auto &availableFormat : availableFormats)
                {
                    if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
                        availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                    {
                        return availableFormat;
                    }
                }

                return availableFormats[0];
            }

            VkPresentModeKHR VulkanSwapChain::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes)
            {
                if (!m_vsyncEnabled)
                {
                    for (const auto &availablePresentMode : availablePresentModes)
                    {
                        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
                        {
                            return availablePresentMode;
                        }
                    }

                    for (const auto &availablePresentMode : availablePresentModes)
                    {
                        if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR)
                        {
                            return availablePresentMode;
                        }
                    }
                }

                return VK_PRESENT_MODE_FIFO_KHR;
            }

            VkExtent2D VulkanSwapChain::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities, uint32_t width, uint32_t height)
            {
                if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
                {
                    return capabilities.currentExtent;
                }
                else
                {
                    VkExtent2D actualExtent = {width, height};

                    actualExtent.width = std::clamp(actualExtent.width,
                                                    capabilities.minImageExtent.width,
                                                    capabilities.maxImageExtent.width);
                    actualExtent.height = std::clamp(actualExtent.height,
                                                     capabilities.minImageExtent.height,
                                                     capabilities.maxImageExtent.height);

                    return actualExtent;
                }
            }

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange