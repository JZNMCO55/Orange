/**
 * @file VulkanSemaphore.h
 * @brief Vulkan信号量实现
 */

#ifndef ORANGE_VULKAN_SEMAPHORE_H
#define ORANGE_VULKAN_SEMAPHORE_H

#include "../../../GraphicsInterface/RenderSync/IRenderSemaphore.h"
#include "../VulkanCommon/VulkanCommon.h"
#include <string>

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
            class VulkanSemaphore : public IRenderSemaphore
            {
            public:
                VulkanSemaphore(VulkanDevice *device);
                virtual ~VulkanSemaphore();

                // IRenderSemaphore接口实现
                virtual SemaphoreType GetType() const override;
                virtual uint64_t GetCounterValue() const override;
                virtual bool Wait(uint64_t value, uint64_t timeout = UINT64_MAX) override;
                virtual bool Signal(uint64_t value) override;
                virtual IRenderDevice *GetDevice() const override;
                virtual void *GetNativeSemaphore() const override;
                virtual void SetName(const char *name) override;
                virtual const char *GetName() const override;

                // Vulkan特定方法
                VkSemaphore GetVkSemaphore() const { return m_semaphore; }

                // 初始化方法
                bool Initialize(const SemaphoreCreateInfo &createInfo);
                void Shutdown();

            private:
                VulkanDevice *m_device;
                VkSemaphore m_semaphore = VK_NULL_HANDLE;
                SemaphoreType m_type = SemaphoreType::Binary;
                std::string m_name;
            };

            /**
             * @brief Vulkan信号量工厂实现
             */
            class VulkanSemaphoreFactory : public IRenderSemaphoreFactory
            {
            public:
                VulkanSemaphoreFactory(VulkanDevice *device);
                virtual ~VulkanSemaphoreFactory();

                // IRenderSemaphoreFactory接口实现
                virtual IRenderSemaphore *CreateSemaphore(const SemaphoreCreateInfo &createInfo) override;
                virtual void DestroySemaphore(IRenderSemaphore *semaphore) override;
                virtual bool WaitSemaphores(IRenderSemaphore **semaphores, const uint64_t *values, uint32_t semaphoreCount, bool waitAll, uint64_t timeout = UINT64_MAX) override;
                virtual bool SignalSemaphores(IRenderSemaphore **semaphores, const uint64_t *values, uint32_t semaphoreCount) override;

            private:
                VulkanDevice *m_device;
            };

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_SEMAPHORE_H 