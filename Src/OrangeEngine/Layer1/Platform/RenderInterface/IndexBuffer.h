#ifndef INDEX_BUFFER_H
#define INDEX_BUFFER_H

#include "Core/Base/Ref.h"
#include "RendererTypes.h"

namespace Orange
{
    /**
     * @class IndexBuffer
     * @brief 索引缓冲区抽象基类
     *
     * 这个类提供了索引缓冲区的统一接口，封装了索引数据的存储和管理。
     * 索引缓冲区与顶点缓冲区配合使用，通过索引来引用顶点数据，
     * 实现高效的几何体渲染。
     *
     * 主要功能包括：
     * - 创建和管理GPU索引缓冲区
     * - 上传和更新索引数据
     * - 绑定缓冲区用于渲染
     * - 提供索引数量和缓冲区大小查询
     *
     * 索引缓冲区的优势：
     * - 减少顶点数据的重复存储
     * - 降低内存带宽需求
     * - 提高缓存命中率
     * - 支持复杂几何体的高效渲染
     *
     * 具体的实现由各个图形API的派生类提供（如VulkanIndexBuffer）。
     */
    class IndexBuffer : public RefCounted
    {
    public:
        /**
         * @brief 虚析构函数
         *
         * 确保派生类能够正确清理GPU资源。
         */
        virtual ~IndexBuffer() {}

        /**
         * @brief 设置索引缓冲区数据
         * @param buffer 要上传的索引数据指针
         * @param size 数据大小（字节）
         * @param offset 缓冲区内的偏移量（字节），默认为0
         *
         * 将索引数据上传到索引缓冲区的指定位置。索引数据通常是
         * 16位或32位无符号整数数组，每个整数代表顶点缓冲区中的一个顶点索引。
         *
         * @note 索引值必须在有效的顶点范围内，否则会导致渲染错误。
         */
        virtual void SetData(void *buffer, uint64_t size, uint64_t offset = 0) = 0;

        /**
         * @brief 绑定索引缓冲区
         *
         * 将此索引缓冲区绑定到当前的渲染上下文，使其成为后续
         * 渲染操作的索引数据源。绑定后的索引缓冲区将用于指定
         * 顶点的绘制顺序。
         */
        virtual void Bind() const = 0;

        /**
         * @brief 获取索引数量
         * @return 索引缓冲区中的索引数量
         *
         * 返回索引缓冲区中包含的索引数量。这个值通常用于
         * 绘制调用中指定要渲染的索引数量。
         */
        virtual uint32_t GetCount() const = 0;

        /**
         * @brief 获取缓冲区大小
         * @return 缓冲区大小（字节）
         *
         * 返回索引缓冲区的总大小（以字节为单位）。
         */
        virtual uint64_t GetSize() const = 0;

        /**
         * @brief 获取渲染器ID
         * @return 渲染器相关的资源ID
         *
         * 返回底层图形API中对应的缓冲区资源ID，用于调试和内部操作。
         */
        virtual RendererID GetRendererID() const = 0;

        /**
         * @brief 创建索引缓冲区（仅分配空间）
         * @param size 缓冲区大小（字节）
         * @return 创建的索引缓冲区实例智能指针
         *
         * 创建一个指定大小的空索引缓冲区，不包含初始数据。
         * 索引数据可以稍后通过SetData方法上传。
         *
         * 具体的实现类型取决于当前使用的渲染API。
         */
        static Ref<IndexBuffer> Create(uint64_t size);

        /**
         * @brief 创建索引缓冲区（带初始数据）
         * @param data 初始索引数据指针
         * @param size 数据大小（字节），默认为0（自动计算）
         * @return 创建的索引缓冲区实例智能指针
         *
         * 创建一个新的索引缓冲区并用提供的数据初始化。
         * 如果size参数为0，将根据数据指针自动计算大小。
         *
         * @note 确保提供的数据在缓冲区创建完成前保持有效。
         */
        static Ref<IndexBuffer> Create(void *data, uint64_t size = 0);
    };
}

#endif