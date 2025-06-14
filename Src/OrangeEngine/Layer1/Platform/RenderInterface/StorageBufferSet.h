#ifndef STORAGE_BUFFER_SET_H
#define STORAGE_BUFFER_SET_H

#include "Core/Base/Ref.h"
#include "StorageBuffer.h"

namespace Orange
{
    /**
     * @class StorageBufferSet
     * @brief 存储缓冲区集合抽象基类
     *
     * 这个类提供了存储缓冲区集合的统一接口，用于管理多个存储缓冲区
     * 实例。存储缓冲区集合在现代渲染管线中扮演重要角色，特别是在
     * 需要处理大量结构化数据的场景中。
     *
     * 主要功能包括：
     * - 多帧缓冲区管理：为每一帧提供独立的缓冲区实例
     * - 线程安全访问：支持渲染线程和主线程的并发访问
     * - 动态大小调整：支持运行时调整缓冲区大小
     * - 资源同步：确保GPU和CPU之间的数据一致性
     *
     * 使用场景：
     * - 实例化渲染的变换矩阵数组
     * - 粒子系统的粒子数据
     * - 计算着色器的输入/输出数据
     * - 动态几何数据的存储
     *
     * 线程安全说明：
     * - Get()：主线程安全，用于主线程访问
     * - RT_Get()：渲染线程安全，用于渲染线程访问
     * - Get(frame)：指定帧访问，需要外部同步
     *
     * 具体的实现由各个图形API的派生类提供（如VulkanStorageBufferSet）。
     */
    class StorageBufferSet : public RefCounted
    {
    public:
        /**
         * @brief 虚析构函数
         *
         * 确保派生类能够正确清理所有缓冲区资源。
         */
        virtual ~StorageBufferSet() {}

        /**
         * @brief 获取当前存储缓冲区（主线程）
         * @return 当前帧的存储缓冲区智能指针
         *
         * 返回当前帧对应的存储缓冲区实例，此方法在主线程中调用是安全的。
         * 内部会根据当前的帧索引自动选择合适的缓冲区实例。
         */
        virtual Ref<StorageBuffer> Get() = 0;

        /**
         * @brief 获取当前存储缓冲区（渲染线程）
         * @return 当前帧的存储缓冲区智能指针
         *
         * 返回当前帧对应的存储缓冲区实例，此方法在渲染线程中调用是安全的。
         * RT_前缀表示这是渲染线程(Render Thread)专用的方法。
         *
         * @note 此方法专为多线程渲染设计，确保渲染线程能够安全访问缓冲区
         */
        virtual Ref<StorageBuffer> RT_Get() = 0;

        /**
         * @brief 获取指定帧的存储缓冲区
         * @param frame 帧索引
         * @return 指定帧的存储缓冲区智能指针
         *
         * 返回指定帧索引对应的存储缓冲区实例。这允许直接访问特定帧的
         * 缓冲区，通常用于调试或特殊的渲染场景。
         *
         * @note 调用者需要确保frame索引在有效范围内
         */
        virtual Ref<StorageBuffer> Get(uint32_t frame) = 0;

        /**
         * @brief 设置指定帧的存储缓冲区
         * @param storageBuffer 要设置的存储缓冲区对象
         * @param frame 目标帧索引
         *
         * 将指定的存储缓冲区设置到指定的帧索引位置。这允许运行时
         * 替换或更新特定帧的缓冲区实例。
         *
         * @note 调用者需要确保frame索引在有效范围内
         */
        virtual void Set(Ref<StorageBuffer> storageBuffer, uint32_t frame) = 0;

        /**
         * @brief 调整缓冲区集合大小
         * @param newSize 新的缓冲区大小（字节）
         *
         * 调整集合中所有存储缓冲区的大小。这是一个代价较高的操作，
         * 因为可能需要重新分配GPU内存并复制现有数据。
         *
         * 调整大小的过程：
         * 1. 为每个帧创建新的缓冲区实例
         * 2. 如果可能，复制现有数据到新缓冲区
         * 3. 释放旧的缓冲区资源
         * 4. 更新内部引用
         *
         * @note 此操作可能会导致GPU同步，影响性能
         */
        virtual void Resize(uint32_t newSize) = 0;

        /**
         * @brief 创建存储缓冲区集合实例
         * @param specification 存储缓冲区规格配置
         * @param size 缓冲区大小（字节）
         * @param framesInFlight 飞行帧数量，默认为0（使用系统默认值）
         * @return 创建的存储缓冲区集合实例智能指针
         *
         * 根据指定的规格创建一个新的存储缓冲区集合实例。
         *
         * 参数说明：
         * - specification：定义缓冲区的使用模式和属性
         * - size：每个缓冲区实例的大小
         * - framesInFlight：同时处理的帧数量，影响缓冲区实例数量
         *
         * 飞行帧数量的选择：
         * - 0：使用渲染器的默认设置（通常是2-3帧）
         * - 1：单缓冲，适用于简单场景
         * - 2-3：双/三缓冲，适用于大多数实时渲染
         * - 更多：适用于复杂的多线程渲染管线
         */
        static Ref<StorageBufferSet> Create(const StorageBufferSpecification &specification, uint32_t size, uint32_t framesInFlight = 0);
    };
}

#endif