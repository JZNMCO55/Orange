/**
 * @file VulkanCommon.h
 * @brief Vulkan通用工具和定义
 */

#ifndef ORANGE_VULKAN_COMMON_H
#define ORANGE_VULKAN_COMMON_H

#ifdef ORANGE_VULKAN_ENABLED

#include <vulkan/vulkan.h>
#include "../../../GraphicsInterface/RenderCommon/RenderCommon.h"
#include <stdexcept>
#include <vector>
#include <optional>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
/**
 * @brief Vulkan错误检查宏
 */
#define VK_CHECK(result)                                                    \
    do                                                                      \
    {                                                                       \
        VkResult res = (result);                                            \
        if (res != VK_SUCCESS)                                              \
        {                                                                   \
            throw std::runtime_error("Vulkan错误: " + std::to_string(res)); \
        }                                                                   \
    } while (0)

            /**
             * @brief 队列族索引结构
             */
            struct QueueFamilyIndices
            {
                std::optional<uint32_t> graphicsFamily;
                std::optional<uint32_t> presentFamily;
                std::optional<uint32_t> computeFamily;
                std::optional<uint32_t> transferFamily;

                bool isComplete() const
                {
                    return graphicsFamily.has_value() && presentFamily.has_value();
                }
            };

            /**
             * @brief 交换链支持详情
             */
            struct SwapChainSupportDetails
            {
                VkSurfaceCapabilitiesKHR capabilities;
                std::vector<VkSurfaceFormatKHR> formats;
                std::vector<VkPresentModeKHR> presentModes;
            };

            /**
             * @brief 转换Orange格式到Vulkan格式
             */
            VkFormat ToVulkanFormat(PixelFormat format);

            /**
             * @brief 转换Orange缓冲区类型到Vulkan用途
             */
            VkBufferUsageFlags ToVulkanBufferUsage(BufferType type);

            /**
             * @brief 转换Orange着色器阶段到Vulkan阶段
             */
            VkShaderStageFlagBits ToVulkanShaderStage(ShaderStageFlagBits stage);

            /**
             * @brief 查找队列族
             */
            QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);

            /**
             * @brief 查询交换链支持
             */
            SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);

            /**
             * @brief 检查设备扩展支持
             */
            bool CheckDeviceExtensionSupport(VkPhysicalDevice device, const std::vector<const char *> &extensions);

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_ENABLED

#endif // ORANGE_VULKAN_COMMON_H