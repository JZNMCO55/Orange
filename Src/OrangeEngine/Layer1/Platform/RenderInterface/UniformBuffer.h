#ifndef UNIFORM_BUFFER_H
#define UNIFORM_BUFFER_H

#include "Core/Base/Ref.h"

namespace Orange
{
    /**
     * @class UniformBuffer
     * @brief 统一缓冲区抽象基类
     *
     * 这个类提供了统一缓冲区的统一接口，封装了统一数据的存储和管理。
     * 统一缓冲区是现代图形API中用于高效传递着色器参数的重要机制。
     *
     * 主要功能包括：
     * - 创建和管理GPU统一缓冲区
     * - 上传和更新统一数据
     * - 支持主线程和渲染线程的数据更新
     * - 提供高效的批量数据传输
     *
     * 统一缓冲区的优势：
     * - 减少API调用次数，提高性能
     * - 支持结构化数据布局
     * - 允许着色器之间共享数据
     * - 提供更好的内存管理
     *
     * 具体的实现由各个图形API的派生类提供（如VulkanUniformBuffer）。
     */
    class UniformBuffer : public RefCounted
    {
    public:
        /**
         * @brief 虚析构函数
         *
         * 确保派生类能够正确清理GPU资源。
         */
        virtual ~UniformBuffer() {}

        /**
         * @brief 设置统一缓冲区数据
         * @param data 要上传的数据指针
         * @param size 数据大小（字节）
         * @param offset 缓冲区内的偏移量（字节），默认为0
         *
         * 将数据上传到统一缓冲区的指定位置。这是主线程版本的方法，
         * 适用于单线程渲染环境或主线程数据更新。
         *
         * @note 数据必须按照着色器中定义的布局进行组织，
         *       特别注意对齐要求（通常为16字节对齐）。
         */
        virtual void SetData(const void *data, uint32_t size, uint32_t offset = 0) = 0;

        /**
         * @brief 设置统一缓冲区数据（渲染线程版本）
         * @param data 要上传的数据指针
         * @param size 数据大小（字节）
         * @param offset 缓冲区内的偏移量（字节），默认为0
         *
         * 将数据上传到统一缓冲区的指定位置。这是渲染线程版本的方法，
         * 用于多线程渲染环境中的线程安全操作。
         *
         * @note 此方法应该在渲染线程中调用，确保与GPU操作的同步。
         */
        virtual void RT_SetData(const void *data, uint32_t size, uint32_t offset = 0) = 0;

        /**
         * @brief 创建统一缓冲区实例
         * @param size 缓冲区大小（字节）
         * @return 创建的统一缓冲区实例智能指针
         *
         * 创建一个指定大小的统一缓冲区。缓冲区创建后可以通过
         * SetData方法上传数据。
         *
         * @note 缓冲区大小应该考虑GPU的对齐要求，通常需要16字节对齐。
         *       具体的实现类型取决于当前使用的渲染API。
         */
        static Ref<UniformBuffer> Create(uint32_t size);
    };
}

#endif