/**
 * @file VulkanBuffer.h
 * @brief Vulkan缓冲区实现
 */

#ifndef ORANGE_VULKAN_BUFFER_H
#define ORANGE_VULKAN_BUFFER_H

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
             * @brief Vulkan缓冲区实现
             */
            class VulkanBuffer : public IRenderBuffer
            {
            public:
                VulkanBuffer(VulkanDevice *device);
                virtual ~VulkanBuffer();

                // IRenderBuffer接口实现
                virtual uint64_t GetSize() const override { return m_size; }
                virtual BufferType GetType() const override { return m_type; }
                virtual bool IsHostVisible() const override { return m_isHostVisible; }
                virtual void *Map(uint64_t offset = 0, uint64_t size = 0) override;
                virtual void Unmap() override;
                virtual void FlushMappedMemory(uint64_t offset = 0, uint64_t size = 0) override;
                virtual void InvalidateMappedMemory(uint64_t offset = 0, uint64_t size = 0) override;
                virtual uint64_t GetDeviceAddress() const override;

                // Vulkan特定方法
                VkBuffer GetVkBuffer() const { return m_buffer; }
                VkDeviceMemory GetVkDeviceMemory() const { return m_memory; }

                // 初始化方法
                bool Initialize(const BufferCreateInfo &createInfo);
                void Shutdown();

                // 数据更新方法
                bool UpdateData(const void *data, uint64_t size, uint64_t offset = 0);

            private:
                VulkanDevice *m_device;
                VkBuffer m_buffer = VK_NULL_HANDLE;
                VkDeviceMemory m_memory = VK_NULL_HANDLE;
                uint64_t m_size = 0;
                BufferType m_type = BufferType::Vertex;
                bool m_isHostVisible = false;
                void *m_mappedMemory = nullptr;

                // 工具方法
                uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
            };

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_BUFFER_H