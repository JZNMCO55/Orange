/**
 * @file VulkanBuffer.cpp
 * @brief Vulkan缓冲区实现
 */

#include "VulkanBuffer.h"
#include "../VulkanInterface/VulkanDevice.h"
#include <cstring>
#include <stdexcept>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {

            VulkanBuffer::VulkanBuffer(VulkanDevice *device)
                : m_device(device), m_buffer(VK_NULL_HANDLE), m_memory(VK_NULL_HANDLE), m_size(0), m_type(BufferType::Vertex), m_isHostVisible(false), m_mappedMemory(nullptr)
            {
            }

            VulkanBuffer::~VulkanBuffer()
            {
                Shutdown();
            }

            bool VulkanBuffer::Initialize(const BufferCreateInfo &createInfo)
            {
                if (!m_device)
                {
                    return false;
                }

                m_size = createInfo.size;
                m_type = createInfo.type;
                m_isHostVisible = createInfo.hostVisible;

                // 创建Vulkan缓冲区
                VkBufferCreateInfo bufferInfo{};
                bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bufferInfo.size = createInfo.size;
                bufferInfo.usage = ToVulkanBufferUsage(createInfo.type);
                bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

                // 如果是主机可见，添加传输源用途以便更新数据
                if (createInfo.hostVisible)
                {
                    bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                }
                else
                {
                    bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                }

                VK_CHECK(vkCreateBuffer(m_device->GetVkDevice(), &bufferInfo, nullptr, &m_buffer));

                // 获取内存需求
                VkMemoryRequirements memRequirements;
                vkGetBufferMemoryRequirements(m_device->GetVkDevice(), m_buffer, &memRequirements);

                // 分配内存
                VkMemoryAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                allocInfo.allocationSize = memRequirements.size;

                // 确定内存属性
                VkMemoryPropertyFlags properties = 0;
                if (createInfo.hostVisible)
                {
                    properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
                    if (createInfo.hostCoherent)
                    {
                        properties |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                    }
                    if (createInfo.hostCached)
                    {
                        properties |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
                    }
                }

                if (createInfo.deviceLocal)
                {
                    properties |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                }

                allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

                VK_CHECK(vkAllocateMemory(m_device->GetVkDevice(), &allocInfo, nullptr, &m_memory));

                // 绑定缓冲区和内存
                VK_CHECK(vkBindBufferMemory(m_device->GetVkDevice(), m_buffer, m_memory, 0));

                // 如果需要映射，则立即映射
                if (createInfo.mapped && createInfo.hostVisible)
                {
                    Map();
                }

                return true;
            }

            void VulkanBuffer::Shutdown()
            {
                if (m_mappedMemory)
                {
                    Unmap();
                }

                if (m_buffer != VK_NULL_HANDLE)
                {
                    vkDestroyBuffer(m_device->GetVkDevice(), m_buffer, nullptr);
                    m_buffer = VK_NULL_HANDLE;
                }

                if (m_memory != VK_NULL_HANDLE)
                {
                    vkFreeMemory(m_device->GetVkDevice(), m_memory, nullptr);
                    m_memory = VK_NULL_HANDLE;
                }

                m_size = 0;
                m_isHostVisible = false;
            }

            void *VulkanBuffer::Map(uint64_t offset, uint64_t size)
            {
                if (!m_isHostVisible)
                {
                    return nullptr;
                }

                if (m_mappedMemory != nullptr)
                {
                    return m_mappedMemory;
                }

                VkDeviceSize mapSize = (size == 0) ? m_size : size;

                VkResult result = vkMapMemory(m_device->GetVkDevice(), m_memory, offset, mapSize, 0, &m_mappedMemory);
                if (result != VK_SUCCESS)
                {
                    return nullptr;
                }

                return m_mappedMemory;
            }

            void VulkanBuffer::Unmap()
            {
                if (m_mappedMemory != nullptr)
                {
                    vkUnmapMemory(m_device->GetVkDevice(), m_memory);
                    m_mappedMemory = nullptr;
                }
            }

            void VulkanBuffer::FlushMappedMemory(uint64_t offset, uint64_t size)
            {
                if (!m_isHostVisible || m_mappedMemory == nullptr)
                {
                    return;
                }

                VkMappedMemoryRange range{};
                range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
                range.memory = m_memory;
                range.offset = offset;
                range.size = (size == 0) ? VK_WHOLE_SIZE : size;

                vkFlushMappedMemoryRanges(m_device->GetVkDevice(), 1, &range);
            }

            void VulkanBuffer::InvalidateMappedMemory(uint64_t offset, uint64_t size)
            {
                if (!m_isHostVisible || m_mappedMemory == nullptr)
                {
                    return;
                }

                VkMappedMemoryRange range{};
                range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
                range.memory = m_memory;
                range.offset = offset;
                range.size = (size == 0) ? VK_WHOLE_SIZE : size;

                vkInvalidateMappedMemoryRanges(m_device->GetVkDevice(), 1, &range);
            }

            uint64_t VulkanBuffer::GetDeviceAddress() const
            {
                // 检查设备是否支持缓冲区设备地址扩展
                VkBufferDeviceAddressInfo addressInfo{};
                addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
                addressInfo.buffer = m_buffer;

                // 注意：这需要VK_KHR_buffer_device_address扩展
                // 在实际使用中应该检查扩展是否可用
                return vkGetBufferDeviceAddress(m_device->GetVkDevice(), &addressInfo);
            }

            bool VulkanBuffer::UpdateData(const void *data, uint64_t size, uint64_t offset)
            {
                if (!data || size == 0)
                {
                    return false;
                }

                if (offset + size > m_size)
                {
                    return false;
                }

                if (m_isHostVisible)
                {
                    // 直接内存复制到映射的内存
                    bool wasMapped = (m_mappedMemory != nullptr);
                    void *mappedData = Map(offset, size);
                    if (!mappedData)
                    {
                        return false;
                    }

                    std::memcpy(mappedData, data, size);
                    FlushMappedMemory(offset, size);

                    if (!wasMapped)
                    {
                        Unmap();
                    }

                    return true;
                }
                else
                {
                    // 使用临时缓冲区进行数据传输
                    BufferCreateInfo stagingInfo{};
                    stagingInfo.size = size;
                    stagingInfo.type = BufferType::Staging;
                    stagingInfo.hostVisible = true;
                    stagingInfo.hostCoherent = true;
                    stagingInfo.deviceLocal = false;
                    stagingInfo.mapped = false;

                    VulkanBuffer stagingBuffer(m_device);
                    if (!stagingBuffer.Initialize(stagingInfo))
                    {
                        return false;
                    }

                    // 复制数据到临时缓冲区
                    void *stagingData = stagingBuffer.Map();
                    if (!stagingData)
                    {
                        return false;
                    }

                    std::memcpy(stagingData, data, size);
                    stagingBuffer.FlushMappedMemory();
                    stagingBuffer.Unmap();

                    // 执行缓冲区复制命令
                    // 注意：这里需要一个命令缓冲区来执行复制操作
                    // 简化版本，实际应该通过命令队列执行
                    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

                    VkBufferCopy copyRegion{};
                    copyRegion.srcOffset = 0;
                    copyRegion.dstOffset = offset;
                    copyRegion.size = size;

                    vkCmdCopyBuffer(commandBuffer, stagingBuffer.GetVkBuffer(), m_buffer, 1, &copyRegion);

                    EndSingleTimeCommands(commandBuffer);

                    return true;
                }
            }

            uint32_t VulkanBuffer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
            {
                VkPhysicalDeviceMemoryProperties memProperties;
                vkGetPhysicalDeviceMemoryProperties(m_device->GetVkPhysicalDevice(), &memProperties);

                for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
                {
                    if ((typeFilter & (1 << i)) &&
                        (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
                    {
                        return i;
                    }
                }

                throw std::runtime_error("找不到合适的内存类型！");
            }

            VkCommandBuffer VulkanBuffer::BeginSingleTimeCommands()
            {
                VkCommandBufferAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocInfo.commandPool = m_device->GetCommandPool();
                allocInfo.commandBufferCount = 1;

                VkCommandBuffer commandBuffer;
                vkAllocateCommandBuffers(m_device->GetVkDevice(), &allocInfo, &commandBuffer);

                VkCommandBufferBeginInfo beginInfo{};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

                vkBeginCommandBuffer(commandBuffer, &beginInfo);

                return commandBuffer;
            }

            void VulkanBuffer::EndSingleTimeCommands(VkCommandBuffer commandBuffer)
            {
                vkEndCommandBuffer(commandBuffer);

                VkSubmitInfo submitInfo{};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &commandBuffer;

                vkQueueSubmit(m_device->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
                vkQueueWaitIdle(m_device->GetGraphicsQueue());

                vkFreeCommandBuffers(m_device->GetVkDevice(), m_device->GetCommandPool(), 1, &commandBuffer);
            }

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange
