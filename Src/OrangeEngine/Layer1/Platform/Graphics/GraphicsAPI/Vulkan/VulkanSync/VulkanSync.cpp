/**
 * @file VulkanSync.cpp
 * @brief Vulkan同步对象实现
 */

#include "VulkanSync.h"
#include "../VulkanInterface/VulkanDevice.h"
#include <iostream>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            // VulkanSemaphore 实现
            VulkanSemaphore::VulkanSemaphore(VulkanDevice *device)
                : m_device(device)
            {
            }

            VulkanSemaphore::~VulkanSemaphore()
            {
                Shutdown();
            }

            bool VulkanSemaphore::Initialize()
            {
                VkSemaphoreCreateInfo semaphoreInfo{};
                semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

                if (vkCreateSemaphore(m_device->GetVkDevice(), &semaphoreInfo, nullptr, &m_semaphore) != VK_SUCCESS)
                {
                    std::cerr << "VulkanSemaphore::Initialize: 创建信号量失败" << std::endl;
                    return false;
                }

                return true;
            }

            void VulkanSemaphore::Shutdown()
            {
                if (m_semaphore != VK_NULL_HANDLE)
                {
                    vkDestroySemaphore(m_device->GetVkDevice(), m_semaphore, nullptr);
                    m_semaphore = VK_NULL_HANDLE;
                }
            }

            // VulkanFence 实现
            VulkanFence::VulkanFence(VulkanDevice *device)
                : m_device(device)
            {
            }

            VulkanFence::~VulkanFence()
            {
                Shutdown();
            }

            bool VulkanFence::Wait(uint64_t timeoutNs)
            {
                VkResult result = vkWaitForFences(m_device->GetVkDevice(), 1, &m_fence, VK_TRUE, timeoutNs);
                return result == VK_SUCCESS;
            }

            bool VulkanFence::Reset()
            {
                VkResult result = vkResetFences(m_device->GetVkDevice(), 1, &m_fence);
                return result == VK_SUCCESS;
            }

            bool VulkanFence::IsSignaled() const
            {
                VkResult result = vkGetFenceStatus(m_device->GetVkDevice(), m_fence);
                return result == VK_SUCCESS;
            }

            bool VulkanFence::Initialize(bool signaled)
            {
                VkFenceCreateInfo fenceInfo{};
                fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                if (signaled)
                {
                    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                }

                if (vkCreateFence(m_device->GetVkDevice(), &fenceInfo, nullptr, &m_fence) != VK_SUCCESS)
                {
                    std::cerr << "VulkanFence::Initialize: 创建栅栏失败" << std::endl;
                    return false;
                }

                return true;
            }

            void VulkanFence::Shutdown()
            {
                if (m_fence != VK_NULL_HANDLE)
                {
                    vkDestroyFence(m_device->GetVkDevice(), m_fence, nullptr);
                    m_fence = VK_NULL_HANDLE;
                }
            }

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange
