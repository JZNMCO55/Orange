/**
 * @file VulkanEvent.cpp
 * @brief Vulkan事件实现
 */

#include "VulkanEvent.h"
#include "../VulkanInterface/VulkanDevice.h"
#include <iostream>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            // ================================
            // VulkanEvent实现
            // ================================

            VulkanEvent::VulkanEvent(VulkanDevice *device)
                : m_device(device), m_event(VK_NULL_HANDLE)
            {
            }

            VulkanEvent::~VulkanEvent()
            {
                Shutdown();
            }

            bool VulkanEvent::Initialize(const EventCreateInfo &createInfo)
            {
                if (!m_device)
                {
                    std::cerr << "VulkanEvent: 设备为空" << std::endl;
                    return false;
                }

                VkEventCreateInfo vkCreateInfo{};
                vkCreateInfo.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;

                VkResult result = vkCreateEvent(m_device->GetVkDevice(), &vkCreateInfo, nullptr, &m_event);
                if (result != VK_SUCCESS)
                {
                    std::cerr << "VulkanEvent: 创建事件失败，错误码: " << result << std::endl;
                    return false;
                }

                if (createInfo.debugName && strlen(createInfo.debugName) > 0)
                {
                    m_name = createInfo.debugName;
                }

                std::cout << "VulkanEvent: 事件创建成功" << std::endl;
                return true;
            }

            void VulkanEvent::Shutdown()
            {
                if (m_event != VK_NULL_HANDLE)
                {
                    vkDestroyEvent(m_device->GetVkDevice(), m_event, nullptr);
                    m_event = VK_NULL_HANDLE;
                }
            }

            bool VulkanEvent::GetStatus() const
            {
                if (m_event == VK_NULL_HANDLE)
                {
                    return false;
                }

                VkResult result = vkGetEventStatus(m_device->GetVkDevice(), m_event);
                return result == VK_EVENT_SET;
            }

            bool VulkanEvent::Set()
            {
                if (m_event == VK_NULL_HANDLE)
                {
                    return false;
                }

                VkResult result = vkSetEvent(m_device->GetVkDevice(), m_event);
                return result == VK_SUCCESS;
            }

            bool VulkanEvent::Reset()
            {
                if (m_event == VK_NULL_HANDLE)
                {
                    return false;
                }

                VkResult result = vkResetEvent(m_device->GetVkDevice(), m_event);
                return result == VK_SUCCESS;
            }

            IRenderDevice *VulkanEvent::GetDevice() const
            {
                return m_device;
            }

            void *VulkanEvent::GetNativeEvent() const
            {
                return m_event;
            }

            void VulkanEvent::SetName(const char *name)
            {
                if (name)
                {
                    m_name = name;
                }
                else
                {
                    m_name.clear();
                }
            }

            const char *VulkanEvent::GetName() const
            {
                return m_name.c_str();
            }

            // ================================
            // VulkanEventFactory实现
            // ================================

            VulkanEventFactory::VulkanEventFactory(VulkanDevice *device)
                : m_device(device)
            {
            }

            VulkanEventFactory::~VulkanEventFactory()
            {
            }

            IRenderEvent *VulkanEventFactory::CreateEvent(const EventCreateInfo &createInfo)
            {
                auto event = std::make_unique<VulkanEvent>(m_device);
                if (!event->Initialize(createInfo))
                {
                    return nullptr;
                }

                return event.release();
            }

            void VulkanEventFactory::DestroyEvent(IRenderEvent *event)
            {
                if (event)
                {
                    delete event;
                }
            }

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange
