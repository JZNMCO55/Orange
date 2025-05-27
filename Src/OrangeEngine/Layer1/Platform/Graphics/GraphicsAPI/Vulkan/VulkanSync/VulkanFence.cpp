/**
 * @file VulkanFence.cpp
 * @brief Vulkan围栏实现
 */

#include "VulkanFence.h"
#include "../VulkanInterface/VulkanDevice.h"
#include <iostream>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            // VulkanFence 实现
            VulkanFence::VulkanFence(VulkanDevice *device)
                : m_device(device)
            {
            }

            VulkanFence::~VulkanFence()
            {
                Shutdown();
            }

            bool VulkanFence::Wait(uint64_t timeout)
            {
                VkResult result = vkWaitForFences(m_device->GetVkDevice(), 1, &m_fence, VK_TRUE, timeout);
                return result == VK_SUCCESS;
            }

            bool VulkanFence::Reset()
            {
                VkResult result = vkResetFences(m_device->GetVkDevice(), 1, &m_fence);
                return result == VK_SUCCESS;
            }

            bool VulkanFence::GetStatus() const
            {
                VkResult result = vkGetFenceStatus(m_device->GetVkDevice(), m_fence);
                return result == VK_SUCCESS;
            }

            FenceCreateFlags VulkanFence::GetFlags() const
            {
                return m_flags;
            }

            IRenderDevice *VulkanFence::GetDevice() const
            {
                return reinterpret_cast<IRenderDevice*>(m_device);
            }

            void *VulkanFence::GetNativeFence() const
            {
                return reinterpret_cast<void*>(m_fence);
            }

            void VulkanFence::SetName(const char *name)
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

            const char *VulkanFence::GetName() const
            {
                return m_name.c_str();
            }

            bool VulkanFence::Initialize(const FenceCreateInfo &createInfo)
            {
                m_flags = createInfo.flags;
                
                VkFenceCreateInfo fenceInfo{};
                fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                
                if (createInfo.flags & static_cast<uint32_t>(FenceCreateFlagBits::Signaled))
                {
                    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                }

                if (vkCreateFence(m_device->GetVkDevice(), &fenceInfo, nullptr, &m_fence) != VK_SUCCESS)
                {
                    std::cerr << "VulkanFence::Initialize: 创建栅栏失败" << std::endl;
                    return false;
                }

                // 设置调试名称
                if (createInfo.debugName)
                {
                    SetName(createInfo.debugName);
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

            // VulkanFenceFactory 实现
            VulkanFenceFactory::VulkanFenceFactory(VulkanDevice *device)
                : m_device(device)
            {
            }

            VulkanFenceFactory::~VulkanFenceFactory()
            {
            }

            IRenderFence *VulkanFenceFactory::CreateFence(const FenceCreateInfo &createInfo)
            {
                auto fence = new VulkanFence(m_device);
                if (!fence->Initialize(createInfo))
                {
                    delete fence;
                    return nullptr;
                }
                return fence;
            }

            void VulkanFenceFactory::DestroyFence(IRenderFence *fence)
            {
                if (fence)
                {
                    delete fence;
                }
            }

            bool VulkanFenceFactory::WaitForFences(IRenderFence **fences, uint32_t fenceCount, bool waitAll, uint64_t timeout)
            {
                if (!fences || fenceCount == 0)
                {
                    return false;
                }

                std::vector<VkFence> vkFences(fenceCount);
                for (uint32_t i = 0; i < fenceCount; ++i)
                {
                    auto vulkanFence = static_cast<VulkanFence*>(fences[i]);
                    vkFences[i] = vulkanFence->GetVkFence();
                }

                VkResult result = vkWaitForFences(
                    m_device->GetVkDevice(),
                    fenceCount,
                    vkFences.data(),
                    waitAll ? VK_TRUE : VK_FALSE,
                    timeout
                );

                return result == VK_SUCCESS;
            }

            bool VulkanFenceFactory::ResetFences(IRenderFence **fences, uint32_t fenceCount)
            {
                if (!fences || fenceCount == 0)
                {
                    return false;
                }

                std::vector<VkFence> vkFences(fenceCount);
                for (uint32_t i = 0; i < fenceCount; ++i)
                {
                    auto vulkanFence = static_cast<VulkanFence*>(fences[i]);
                    vkFences[i] = vulkanFence->GetVkFence();
                }

                VkResult result = vkResetFences(m_device->GetVkDevice(), fenceCount, vkFences.data());
                return result == VK_SUCCESS;
            }

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange 