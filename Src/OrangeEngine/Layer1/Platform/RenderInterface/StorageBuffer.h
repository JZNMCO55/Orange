#ifndef STORAGE_BUFFER_H
#define STORAGE_BUFFER_H

#include "Core/Base/Ref.h"

namespace Orange
{
    /**
     * @struct StorageBufferSpecification
     * @brief 存储缓冲区规格配置结构体
     *
     * 定义了创建存储缓冲区所需的配置参数，包括内存位置和调试信息。
     */
    struct StorageBufferSpecification
    {
        bool GPUOnly = true;   ///< 是否仅存储在GPU内存中（true=GPU专用，false=CPU可访问）
        std::string DebugName; ///< 调试名称，用于调试器和性能分析工具
    };

    /**
     * @class StorageBuffer
     * @brief 存储缓冲区抽象基类
     *
     * 这个类提供了存储缓冲区的统一接口，封装了大容量结构化数据的
     * 存储和管理。存储缓冲区在现代渲染管线中扮演重要角色，特别是
     * 在计算着色器、实例化渲染和复杂数据处理场景中。
     *
     * 主要功能包括：
     * - 大容量数据存储：支持MB级别的数据存储
     * - 灵活的内存访问：支持CPU和GPU之间的数据传输
     * - 动态大小调整：支持运行时调整缓冲区大小
     * - 多线程支持：提供线程安全的数据更新接口
     * - 高性能访问：优化的GPU内存访问模式
     *
     * 与统一缓冲区的区别：
     * - 容量：存储缓冲区支持更大的数据量
     * - 访问模式：支持着色器的读写访问
     * - 数据结构：更适合复杂的结构化数据
     * - 性能特性：针对大数据量访问优化
     *
     * 使用场景：
     * - 实例化渲染：存储大量实例的变换矩阵
     * - 粒子系统：存储粒子的位置、速度等属性
     * - 计算着色器：输入/输出大量计算数据
     * - 几何处理：存储顶点、索引等几何数据
     * - 光照计算：存储光源数据和阴影信息
     *
     * 内存管理策略：
     * - GPUOnly=true：数据仅存储在GPU内存中，访问速度最快
     * - GPUOnly=false：数据在CPU和GPU之间共享，便于频繁更新
     *
     * 线程安全说明：
     * - SetData()：主线程安全，用于主线程数据更新
     * - RT_SetData()：渲染线程安全，用于渲染线程数据更新
     *
     * 具体的实现由各个图形API的派生类提供（如VulkanStorageBuffer）。
     */
    class StorageBuffer : public RefCounted
    {
    public:
        /**
         * @brief 虚析构函数
         *
         * 确保派生类能够正确清理存储缓冲区资源。
         */
        virtual ~StorageBuffer() = default;

        /**
         * @brief 设置缓冲区数据（主线程）
         * @param data 要写入的数据指针
         * @param size 数据大小（字节）
         * @param offset 写入偏移量，默认为0
         *
         * 将数据写入存储缓冲区的指定位置。此方法在主线程中调用是安全的。
         *
         * 数据传输过程：
         * 1. 验证参数有效性（大小、偏移量）
         * 2. 如果需要，创建临时的暂存缓冲区
         * 3. 将数据复制到GPU内存
         * 4. 处理内存同步和缓存一致性
         *
         * 性能考虑：
         * - 大数据量传输可能需要较长时间
         * - 频繁的小数据更新可能影响性能
         * - 建议批量更新以提高效率
         *
         * @note 确保data指针在调用期间保持有效
         * @note offset + size不应超过缓冲区总大小
         */
        virtual void SetData(const void *data, uint32_t size, uint32_t offset = 0) = 0;

        /**
         * @brief 设置缓冲区数据（渲染线程）
         * @param data 要写入的数据指针
         * @param size 数据大小（字节）
         * @param offset 写入偏移量，默认为0
         *
         * 将数据写入存储缓冲区的指定位置。此方法在渲染线程中调用是安全的。
         * RT_前缀表示这是渲染线程(Render Thread)专用的方法。
         *
         * 与SetData()的区别：
         * - 线程安全性：专为渲染线程设计
         * - 同步机制：使用不同的同步策略
         * - 性能优化：可能使用更直接的GPU访问路径
         *
         * @note 此方法专为多线程渲染设计
         * @note 不应在主线程中调用此方法
         */
        virtual void RT_SetData(const void *data, uint32_t size, uint32_t offset = 0) = 0;

        /**
         * @brief 调整缓冲区大小
         * @param newSize 新的缓冲区大小（字节）
         *
         * 调整存储缓冲区的大小。这是一个代价较高的操作，因为可能需要
         * 重新分配GPU内存并复制现有数据。
         *
         * 调整大小的过程：
         * 1. 分配新的GPU内存
         * 2. 如果新大小更大，复制现有数据
         * 3. 如果新大小更小，截断数据
         * 4. 释放旧的内存
         * 5. 更新内部状态和引用
         *
         * 性能影响：
         * - 可能导致GPU同步等待
         * - 大缓冲区的重新分配代价很高
         * - 建议在初始化时预分配足够的空间
         *
         * 数据保留策略：
         * - 扩大：保留原有数据，新空间未初始化
         * - 缩小：保留前newSize字节的数据
         *
         * @note 此操作可能会导致GPU管线停顿
         * @note 建议避免在渲染循环中频繁调整大小
         */
        virtual void Resize(uint32_t newSize) = 0;

        /**
         * @brief 创建存储缓冲区实例
         * @param size 缓冲区大小（字节）
         * @param specification 存储缓冲区规格配置
         * @return 创建的存储缓冲区实例智能指针
         *
         * 根据指定的大小和规格创建一个新的存储缓冲区实例。
         *
         * 创建过程：
         * 1. 验证参数有效性
         * 2. 根据规格选择内存类型
         * 3. 分配GPU内存
         * 4. 设置缓冲区属性和用途
         * 5. 初始化同步对象
         *
         * 大小建议：
         * - 考虑数据的最大可能大小
         * - 预留一定的增长空间
         * - 避免过度分配造成内存浪费
         * - 考虑GPU内存对齐要求
         *
         * 规格配置影响：
         * - GPUOnly=true：最佳性能，但CPU访问受限
         * - GPUOnly=false：便于CPU访问，但可能影响性能
         *
         * 典型用法：
         * ```cpp
         * StorageBufferSpecification spec;
         * spec.GPUOnly = true;
         * spec.DebugName = "ParticleData";
         *
         * auto buffer = StorageBuffer::Create(
         *     sizeof(Particle) * maxParticles, spec);
         * ```
         */
        static Ref<StorageBuffer> Create(uint32_t size, const StorageBufferSpecification &specification);
    };
}

#endif