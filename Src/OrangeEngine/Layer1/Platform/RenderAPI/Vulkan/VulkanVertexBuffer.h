#ifndef VULKAN_VERTEX_BUFFER_H
#define VULKAN_VERTEX_BUFFER_H

#include "Core/Memory/Buffer.h"
#include "Platform/RenderInterface/VertexBuffer.h"
#include "VulkanAllocator.h"

namespace Orange
{
#ifdef TODO
    /**
     * @class VulkanVertexBuffer
     * @brief Vulkan顶点缓冲区类
     * @details 继承自VertexBuffer基类，提供Vulkan平台特定的顶点缓冲区实现
     *
     * 该类负责管理Vulkan顶点缓冲区的生命周期，包括：
     * - 顶点数据的GPU内存分配
     * - 数据上传和更新
     * - 缓冲区的绑定和使用
     * - 支持静态和动态使用模式
     */
    class VulkanVertexBuffer : public VertexBuffer
    {
    public:
        /**
         * @brief 构造函数（带初始数据）
         * @param data 顶点数据指针
         * @param size 数据大小（字节）
         * @param usage 缓冲区使用模式，默认为静态
         * @details 创建顶点缓冲区并立即上传数据到GPU
         */
        VulkanVertexBuffer(void *data, uint64_t size,
                           VertexBufferUsage usage = VertexBufferUsage::Static);

        /**
         * @brief 构造函数（仅分配内存）
         * @param size 缓冲区大小（字节）
         * @param usage 缓冲区使用模式，默认为动态
         * @details 创建指定大小的顶点缓冲区，不上传初始数据
         */
        VulkanVertexBuffer(uint64_t size,
                           VertexBufferUsage usage = VertexBufferUsage::Dynamic);

        /**
         * @brief 析构函数
         * @details 释放GPU内存和本地缓存
         */
        virtual ~VulkanVertexBuffer() override;

        /**
         * @brief 设置顶点数据（主线程调用）
         * @param buffer 数据源指针
         * @param size 数据大小（字节）
         * @param offset 数据偏移量，默认为0
         * @details 更新本地缓存并提交渲染线程任务
         */
        virtual void SetData(void *buffer, uint64_t size, uint64_t offset = 0) override;

        /**
         * @brief 设置顶点数据（渲染线程调用）
         * @param buffer 数据源指针
         * @param size 数据大小（字节）
         * @param offset 数据偏移量，默认为0
         * @details 直接更新GPU缓冲区数据
         */
        virtual void RT_SetData(void *buffer, uint64_t size, uint64_t offset = 0) override;

        /**
         * @brief 绑定顶点缓冲区
         * @details Vulkan实现中此方法为空，绑定在渲染管线中处理
         */
        virtual void Bind() const override {}

        /**
         * @brief 获取缓冲区大小
         * @return 缓冲区大小（字节）
         */
        virtual unsigned int GetSize() const override { return m_Size; }

        /**
         * @brief 获取渲染器ID
         * @return 渲染器ID（Vulkan实现中返回0）
         */
        virtual RendererID GetRendererID() const override { return 0; }

        /**
         * @brief 获取Vulkan缓冲区句柄
         * @return VkBuffer句柄
         */
        VkBuffer GetVulkanBuffer() const { return m_VulkanBuffer; }

    private:
        uint64_t m_Size = 0; ///< 缓冲区大小
        Buffer m_LocalData;  ///< 本地数据缓存

        VkBuffer m_VulkanBuffer = nullptr; ///< Vulkan缓冲区句柄
        VmaAllocation m_MemoryAllocation;  ///< VMA内存分配句柄
    };
#endif
}

#endif