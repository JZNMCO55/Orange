/**
 * @file VulkanSync.h
 * @brief Vulkan同步原语实现
 */

#ifndef ORANGE_VULKAN_SYNC_H
#define ORANGE_VULKAN_SYNC_H

#include "../../../GraphicsInterface/RenderInterface/IRenderResources.h"
#include "../VulkanCommon/VulkanCommon.h"

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            class VulkanDevice;

            /**
             * @brief Vulkan信号量实现
             */
            class VulkanSemaphore : public ISemaphore
            {
            public:
                VulkanSemaphore(VulkanDevice *device);
                virtual ~VulkanSemaphore();

                // Vulkan特定方法
                VkSemaphore GetVkSemaphore() const { return m_semaphore; }

                // 初始化方法
                bool Initialize();
                void Shutdown();

            private:
                VulkanDevice *m_device;
                VkSemaphore m_semaphore = VK_NULL_HANDLE;
            };

            /**
             * @brief Vulkan围栏实现
             */
            class VulkanFence : public IFence
            {
            public:
                VulkanFence(VulkanDevice *device);
                virtual ~VulkanFence();

                // IFence接口实现
                virtual bool Wait(uint64_t timeoutNs = UINT64_MAX) override;
                virtual bool Reset() override;
                virtual bool IsSignaled() const override;

                // Vulkan特定方法
                VkFence GetVkFence() const { return m_fence; }

                // 初始化方法
                bool Initialize(bool signaled = false);
                void Shutdown();

            private:
                VulkanDevice *m_device;
                VkFence m_fence = VK_NULL_HANDLE;
            };

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_SYNC_H