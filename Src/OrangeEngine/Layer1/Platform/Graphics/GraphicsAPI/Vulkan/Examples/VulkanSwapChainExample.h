/**
 * @file VulkanSwapChainExample.h
 * @brief Vulkan交换链使用示例
 */

#ifndef ORANGE_VULKAN_SWAP_CHAIN_EXAMPLE_H
#define ORANGE_VULKAN_SWAP_CHAIN_EXAMPLE_H

#include "../VulkanInterface/VulkanSwapChain.h"
#include "../VulkanInterface/VulkanDevice.h"
#include <memory>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            /**
             * @brief Vulkan交换链使用示例类
             */
            class VulkanSwapChainExample
            {
            public:
                /**
                 * @brief 构造函数
                 * @param device Vulkan设备
                 */
                VulkanSwapChainExample(VulkanDevice *device);

                /**
                 * @brief 析构函数
                 */
                ~VulkanSwapChainExample();

                /**
                 * @brief 初始化示例
                 * @param surface 表面句柄
                 * @param width 宽度
                 * @param height 高度
                 * @return 是否成功
                 */
                bool Initialize(VkSurfaceKHR surface, uint32_t width, uint32_t height);

                /**
                 * @brief 清理资源
                 */
                void Cleanup();

                /**
                 * @brief 演示基本的交换链操作
                 * @return 是否成功
                 */
                bool DemoBasicSwapChainOperations();

                /**
                 * @brief 演示交换链调整大小
                 * @param newWidth 新宽度
                 * @param newHeight 新高度
                 * @return 是否成功
                 */
                bool DemoSwapChainResize(uint32_t newWidth, uint32_t newHeight);

                /**
                 * @brief 演示垂直同步切换
                 * @return 是否成功
                 */
                bool DemoVSyncToggle();

                /**
                 * @brief 获取交换链实例
                 * @return 交换链实例
                 */
                std::shared_ptr<VulkanSwapChain> GetSwapChain() const { return m_swapChain; }

            private:
                VulkanDevice *m_device;
                std::shared_ptr<VulkanSwapChain> m_swapChain;

                /**
                 * @brief 验证交换链是否有效
                 * @return 是否有效
                 */
                bool ValidateSwapChain();

                /**
                 * @brief 打印交换链信息
                 */
                void PrintSwapChainInfo();
            };

            /**
             * @brief 运行Vulkan交换链示例
             * @param device Vulkan设备
             * @param surface 表面句柄
             * @param width 宽度
             * @param height 高度
             * @return 是否成功
             */
            bool RunVulkanSwapChainExample(VulkanDevice *device, VkSurfaceKHR surface, uint32_t width, uint32_t height);

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_SWAP_CHAIN_EXAMPLE_H