/**
 * @file VulkanSyncAll.h
 * @brief Vulkan同步原语统一包含文件
 */

#ifndef ORANGE_VULKAN_SYNC_ALL_H
#define ORANGE_VULKAN_SYNC_ALL_H

#include "VulkanFence.h"
#include "VulkanSemaphore.h"
#include "VulkanEvent.h"
#include <memory>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            /**
             * @brief Vulkan同步工厂集合
             * 
             * 提供所有Vulkan同步原语的工厂实例
             */
            class VulkanSyncFactories
            {
            public:
                VulkanSyncFactories(VulkanDevice *device);
                ~VulkanSyncFactories();

                // 获取各种工厂
                VulkanFenceFactory *GetFenceFactory() const { return m_fenceFactory.get(); }
                VulkanSemaphoreFactory *GetSemaphoreFactory() const { return m_semaphoreFactory.get(); }
                VulkanEventFactory *GetEventFactory() const { return m_eventFactory.get(); }

                // 初始化和清理
                bool Initialize();
                void Shutdown();

            private:
                VulkanDevice *m_device;
                std::unique_ptr<VulkanFenceFactory> m_fenceFactory;
                std::unique_ptr<VulkanSemaphoreFactory> m_semaphoreFactory;
                std::unique_ptr<VulkanEventFactory> m_eventFactory;
            };

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_SYNC_ALL_H 