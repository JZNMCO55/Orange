#ifndef VULKAN_ALLOCATOR_H
#define VULKAN_ALLOCATOR_H

#include <vk_mem_alloc.h>
#include "Platform/RenderInterface/GPUStats.h"
#include "VulkanDevice.h"
#include "vk_mem_alloc.h"

#include "Platform/RenderInterface/GPUStats.h"

namespace Orange
{
    /**
     * @class VulkanAllocator
     * @brief Vulkan内存分配器类
     * @details 封装VMA库，提供GPU内存的分配、释放和管理功能
     *
     * 该类负责管理Vulkan应用程序中的GPU内存分配，包括：
     * - 缓冲区(Buffer)内存分配
     * - 图像(Image)内存分配
     * - 内存映射和解映射
     * - 内存使用统计
     */
    class VulkanAllocator
    {
    public:
        /**
         * @brief 默认构造函数
         */
        VulkanAllocator() = default;

        /**
         * @brief 带标签的构造函数
         * @param tag 分配器标签，用于调试和日志记录
         */
        VulkanAllocator(const std::string &tag);

        /**
         * @brief 析构函数
         */
        ~VulkanAllocator();

        // void Allocate(VkMemoryRequirements requirements, VkDeviceMemory* dest, VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        /**
         * @brief 分配缓冲区内存
         * @param bufferCreateInfo Vulkan缓冲区创建信息
         * @param usage VMA内存使用类型
         * @param outBuffer 输出的Vulkan缓冲区句柄
         * @return VMA分配句柄
         * @details 创建并分配一个Vulkan缓冲区及其对应的GPU内存
         */
        VmaAllocation AllocateBuffer(VkBufferCreateInfo bufferCreateInfo, VmaMemoryUsage usage, VkBuffer &outBuffer);

        /**
         * @brief 分配图像内存
         * @param imageCreateInfo Vulkan图像创建信息
         * @param usage VMA内存使用类型
         * @param outImage 输出的Vulkan图像句柄
         * @param allocatedSize 可选的输出参数，返回实际分配的内存大小
         * @return VMA分配句柄
         * @details 创建并分配一个Vulkan图像及其对应的GPU内存
         */
        VmaAllocation AllocateImage(VkImageCreateInfo imageCreateInfo, VmaMemoryUsage usage, VkImage &outImage, VkDeviceSize *allocatedSize = nullptr);

        /**
         * @brief 释放内存分配
         * @param allocation 要释放的VMA分配句柄
         */
        void Free(VmaAllocation allocation);

        /**
         * @brief 销毁图像及其内存
         * @param image 要销毁的Vulkan图像
         * @param allocation 对应的VMA分配句柄
         */
        void DestroyImage(VkImage image, VmaAllocation allocation);

        /**
         * @brief 销毁缓冲区及其内存
         * @param buffer 要销毁的Vulkan缓冲区
         * @param allocation 对应的VMA分配句柄
         */
        void DestroyBuffer(VkBuffer buffer, VmaAllocation allocation);

        /**
         * @brief 映射内存到CPU可访问的地址空间
         * @tparam T 映射内存的数据类型
         * @param allocation VMA分配句柄
         * @return 映射后的内存指针
         * @details 将GPU内存映射到CPU地址空间，允许CPU直接访问GPU内存
         */
        template <typename T>
        T *MapMemory(VmaAllocation allocation)
        {
            T *mappedMemory;
            vmaMapMemory(VulkanAllocator::GetVMAAllocator(), allocation, (void **)&mappedMemory);
            return mappedMemory;
        }

        /**
         * @brief 解除内存映射
         * @param allocation VMA分配句柄
         */
        void UnmapMemory(VmaAllocation allocation);

        /**
         * @brief 输出内存使用统计信息
         * @details 将当前GPU内存使用情况输出到日志
         */
        static void DumpStats();

        /**
         * @brief 获取GPU内存使用统计
         * @return GPU内存统计信息结构体
         */
        static GPUMemoryStats GetStats();

        /**
         * @brief 初始化全局VMA分配器
         * @param device Vulkan设备引用
         */
        static void Init(Ref<VulkanDevice> device);

        /**
         * @brief 关闭并清理VMA分配器
         */
        static void Shutdown();

        /**
         * @brief 获取VMA分配器实例
         * @return VMA分配器引用
         */
        static VmaAllocator &GetVMAAllocator();

    private:
        std::string m_Tag; ///< 分配器标签，用于调试和日志
    };
}

#endif