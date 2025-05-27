/**
 * @file VulkanFence.h
 * @brief Vulkan围栏实现
 */

#ifndef ORANGE_VULKAN_FENCE_H
#define ORANGE_VULKAN_FENCE_H

#include "../../../GraphicsInterface/RenderSync/IRenderFence.h"
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
             * @brief Vulkan围栏实现
             */
            class VulkanFence : public IRenderFence
            {
            public:
                VulkanFence(VulkanDevice *device);
                virtual ~VulkanFence();

                // IRenderFence接口实现
                virtual bool Wait(uint64_t timeout = UINT64_MAX) override;
                virtual bool Reset() override;
                virtual bool GetStatus() const override;
                virtual FenceCreateFlags GetFlags() const override;
                virtual IRenderDevice *GetDevice() const override;
                virtual void *GetNativeFence() const override;
                virtual void SetName(const char *name) override;
                virtual const char *GetName() const override;

                // Vulkan特定方法
                VkFence GetVkFence() const { return m_fence; }

                // 初始化方法
                bool Initialize(const FenceCreateInfo &createInfo);
                void Shutdown();

            private:
                VulkanDevice *m_device;
                VkFence m_fence = VK_NULL_HANDLE;
                FenceCreateFlags m_flags = static_cast<FenceCreateFlags>(0);
                std::string m_name;
            };

            /**
             * @brief Vulkan围栏工厂实现
             */
            class VulkanFenceFactory : public IRenderFenceFactory
            {
            public:
                VulkanFenceFactory(VulkanDevice *device);
                virtual ~VulkanFenceFactory();

                // IRenderFenceFactory接口实现
                virtual IRenderFence *CreateFence(const FenceCreateInfo &createInfo) override;
                virtual void DestroyFence(IRenderFence *fence) override;
                virtual bool WaitForFences(IRenderFence **fences, uint32_t fenceCount, bool waitAll, uint64_t timeout = UINT64_MAX) override;
                virtual bool ResetFences(IRenderFence **fences, uint32_t fenceCount) override;

            private:
                VulkanDevice *m_device;
            };

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_FENCE_H 