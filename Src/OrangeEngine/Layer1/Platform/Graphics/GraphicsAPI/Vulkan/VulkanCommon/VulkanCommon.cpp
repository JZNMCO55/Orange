/**
 * @file VulkanCommon.cpp
 * @brief Vulkan通用工具实现
 */

#include "VulkanCommon.h"
#include <stdexcept>
#include <set>
#include <string>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {

            VkFormat ToVulkanFormat(PixelFormat format)
            {
                switch (format)
                {
                case PixelFormat::R8_UNORM:
                    return VK_FORMAT_R8_UNORM;
                case PixelFormat::RG8_UNORM:
                    return VK_FORMAT_R8G8_UNORM;
                case PixelFormat::RGB8_UNORM:
                    return VK_FORMAT_R8G8B8_UNORM;
                case PixelFormat::RGBA8_UNORM:
                    return VK_FORMAT_R8G8B8A8_UNORM;
                case PixelFormat::BGRA8_UNORM:
                    return VK_FORMAT_B8G8R8A8_UNORM;
                case PixelFormat::R16_FLOAT:
                    return VK_FORMAT_R16_SFLOAT;
                case PixelFormat::RG16_FLOAT:
                    return VK_FORMAT_R16G16_SFLOAT;
                case PixelFormat::RGBA16_FLOAT:
                    return VK_FORMAT_R16G16B16A16_SFLOAT;
                case PixelFormat::R32_FLOAT:
                    return VK_FORMAT_R32_SFLOAT;
                case PixelFormat::RG32_FLOAT:
                    return VK_FORMAT_R32G32_SFLOAT;
                case PixelFormat::RGB32_FLOAT:
                    return VK_FORMAT_R32G32B32_SFLOAT;
                case PixelFormat::RGBA32_FLOAT:
                    return VK_FORMAT_R32G32B32A32_SFLOAT;
                case PixelFormat::D16_UNORM:
                    return VK_FORMAT_D16_UNORM;
                case PixelFormat::D24_UNORM_S8_UINT:
                    return VK_FORMAT_D24_UNORM_S8_UINT;
                case PixelFormat::D32_FLOAT:
                    return VK_FORMAT_D32_SFLOAT;
                case PixelFormat::D32_FLOAT_S8_UINT:
                    return VK_FORMAT_D32_SFLOAT_S8_UINT;
                default:
                    return VK_FORMAT_UNDEFINED;
                }
            }

            VkBufferUsageFlags ToVulkanBufferUsage(BufferType type)
            {
                switch (type)
                {
                case BufferType::Vertex:
                    return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
                case BufferType::Index:
                    return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
                case BufferType::Uniform:
                    return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                case BufferType::Storage:
                    return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                case BufferType::Indirect:
                    return VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
                case BufferType::Staging:
                    return VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                default:
                    return VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                }
            }

            QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface)
            {
                QueueFamilyIndices indices;

                uint32_t queueFamilyCount = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

                std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
                vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

                int i = 0;
                for (const auto &queueFamily : queueFamilies)
                {
                    if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
                    {
                        indices.graphicsFamily = i;
                    }

                    if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
                    {
                        indices.computeFamily = i;
                    }

                    if (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT)
                    {
                        indices.transferFamily = i;
                    }

                    if (surface != VK_NULL_HANDLE)
                    {
                        VkBool32 presentSupport = false;
                        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
                        if (presentSupport)
                        {
                            indices.presentFamily = i;
                        }
                    }

                    if (indices.isComplete())
                    {
                        break;
                    }

                    i++;
                }

                return indices;
            }

            SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface)
            {
                SwapChainSupportDetails details;

                vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

                uint32_t formatCount;
                vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

                if (formatCount != 0)
                {
                    details.formats.resize(formatCount);
                    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
                }

                uint32_t presentModeCount;
                vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

                if (presentModeCount != 0)
                {
                    details.presentModes.resize(presentModeCount);
                    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
                }

                return details;
            }

            bool CheckDeviceExtensionSupport(VkPhysicalDevice device, const std::vector<const char *> &extensions)
            {
                uint32_t extensionCount;
                vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

                std::vector<VkExtensionProperties> availableExtensions(extensionCount);
                vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

                std::set<std::string> requiredExtensions(extensions.begin(), extensions.end());

                for (const auto &extension : availableExtensions)
                {
                    requiredExtensions.erase(extension.extensionName);
                }

                return requiredExtensions.empty();
            }

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange