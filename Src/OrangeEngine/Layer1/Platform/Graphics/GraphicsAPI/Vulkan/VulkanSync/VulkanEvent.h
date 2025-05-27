/**
 * @file VulkanEvent.h
 * @brief Vulkan事件实现
 */

#ifndef ORANGE_VULKAN_EVENT_H
#define ORANGE_VULKAN_EVENT_H

#include "../../../GraphicsInterface/RenderSync/IRenderEvent.h"
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
             * @brief Vulkan事件实现
             */
            class VulkanEvent : public IRenderEvent
            {
            public:
                VulkanEvent(VulkanDevice *device);
                virtual ~VulkanEvent();

                // IRenderEvent接口实现
                virtual bool GetStatus() const override;
                virtual bool Set() override;
                virtual bool Reset() override;
                virtual IRenderDevice *GetDevice() const override;
                virtual void *GetNativeEvent() const override;
                virtual void SetName(const char *name) override;
                virtual const char *GetName() const override;

                // Vulkan特定方法
                VkEvent GetVkEvent() const { return m_event; }

                // 初始化方法
                bool Initialize(const EventCreateInfo &createInfo);
                void Shutdown();

            private:
                VulkanDevice *m_device;
                VkEvent m_event = VK_NULL_HANDLE;
                std::string m_name;
            };

            /**
             * @brief Vulkan事件工厂实现
             */
            class VulkanEventFactory : public IRenderEventFactory
            {
            public:
                VulkanEventFactory(VulkanDevice *device);
                virtual ~VulkanEventFactory();

                // IRenderEventFactory接口实现
                virtual IRenderEvent *CreateEvent(const EventCreateInfo &createInfo) override;
                virtual void DestroyEvent(IRenderEvent *event) override;

            private:
                VulkanDevice *m_device;
            };

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_EVENT_H 