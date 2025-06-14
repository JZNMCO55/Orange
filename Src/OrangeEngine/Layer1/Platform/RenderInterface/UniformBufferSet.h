#ifndef UNIFORM_BUFFER_SET_H
#define UNIFORM_BUFFER_SET_H

#include "UniformBuffer.h"

namespace Orange
{

    /**
     * @class UniformBufferSet
     * @brief 统一缓冲区集合抽象基类
     *
     * 这个类提供了统一缓冲区集合的统一接口，用于管理多个统一缓冲区
     * 实例。统一缓冲区集合在现代渲染管线中扮演重要角色，特别是在
     * 需要管理大量着色器统一变量的场景中。
     *
     * 主要功能包括：
     * - 多帧缓冲区管理：为每一帧提供独立的缓冲区实例
     * - 线程安全访问：支持渲染线程和主线程的并发访问
     * - 统一变量管理：高效管理着色器统一变量数据
     * - 资源同步：确保GPU和CPU之间的数据一致性
     * - 内存对齐：自动处理GPU内存对齐要求
     *
     * 使用场景：
     * - 相机变换矩阵（视图矩阵、投影矩阵）
     * - 光照参数（光源位置、颜色、强度）
     * - 材质属性（漫反射、镜面反射、粗糙度）
     * - 时间和动画参数
     * - 全局渲染设置
     *
     * 线程安全说明：
     * - Get()：主线程安全，用于主线程访问
     * - RT_Get()：渲染线程安全，用于渲染线程访问
     * - Get(frame)：指定帧访问，需要外部同步
     *
     * 与StorageBufferSet的区别：
     * - UniformBufferSet：用于小量、频繁更新的统一变量
     * - StorageBufferSet：用于大量、结构化的数据存储
     *
     * 具体的实现由各个图形API的派生类提供（如VulkanUniformBufferSet）。
     */
    class UniformBufferSet : public RefCounted
    {
    public:
        /**
         * @brief 虚析构函数
         *
         * 确保派生类能够正确清理所有缓冲区资源。
         */
        virtual ~UniformBufferSet() {}

        /**
         * @brief 获取当前统一缓冲区（主线程）
         * @return 当前帧的统一缓冲区智能指针
         *
         * 返回当前帧对应的统一缓冲区实例，此方法在主线程中调用是安全的。
         * 内部会根据当前的帧索引自动选择合适的缓冲区实例。
         *
         * 典型用法：
         * ```cpp
         * auto uniformBuffer = uniformBufferSet->Get();
         * uniformBuffer->SetData(&cameraData, sizeof(CameraData));
         * ```
         */
        virtual Ref<UniformBuffer> Get() = 0;

        /**
         * @brief 获取当前统一缓冲区（渲染线程）
         * @return 当前帧的统一缓冲区智能指针
         *
         * 返回当前帧对应的统一缓冲区实例，此方法在渲染线程中调用是安全的。
         * RT_前缀表示这是渲染线程(Render Thread)专用的方法。
         *
         * @note 此方法专为多线程渲染设计，确保渲染线程能够安全访问缓冲区
         *
         * 典型用法：
         * ```cpp
         * // 在渲染线程中
         * auto uniformBuffer = uniformBufferSet->RT_Get();
         * renderPass->SetInput("CameraUBO", uniformBuffer);
         * ```
         */
        virtual Ref<UniformBuffer> RT_Get() = 0;

        /**
         * @brief 获取指定帧的统一缓冲区
         * @param frame 帧索引
         * @return 指定帧的统一缓冲区智能指针
         *
         * 返回指定帧索引对应的统一缓冲区实例。这允许直接访问特定帧的
         * 缓冲区，通常用于调试或特殊的渲染场景。
         *
         * @note 调用者需要确保frame索引在有效范围内（0 到 framesInFlight-1）
         *
         * 使用场景：
         * - 调试特定帧的数据
         * - 实现自定义的帧同步逻辑
         * - 特殊的多帧效果处理
         */
        virtual Ref<UniformBuffer> Get(uint32_t frame) = 0;

        /**
         * @brief 设置指定帧的统一缓冲区
         * @param uniformBuffer 要设置的统一缓冲区对象
         * @param frame 目标帧索引
         *
         * 将指定的统一缓冲区设置到指定的帧索引位置。这允许运行时
         * 替换或更新特定帧的缓冲区实例。
         *
         * @note 调用者需要确保frame索引在有效范围内
         * @note 替换的缓冲区应该具有相同的大小和用途
         *
         * 使用场景：
         * - 动态切换不同的统一缓冲区配置
         * - 实现缓冲区池化管理
         * - 特殊的渲染效果需求
         */
        virtual void Set(Ref<UniformBuffer> uniformBuffer, uint32_t frame) = 0;

        /**
         * @brief 创建统一缓冲区集合实例
         * @param size 缓冲区大小（字节）
         * @param framesInFlight 飞行帧数量，默认为0（使用系统默认值）
         * @return 创建的统一缓冲区集合实例智能指针
         *
         * 根据指定的参数创建一个新的统一缓冲区集合实例。
         *
         * 参数说明：
         * - size：每个缓冲区实例的大小，需要考虑GPU内存对齐要求
         * - framesInFlight：同时处理的帧数量，影响缓冲区实例数量
         *
         * 飞行帧数量的选择：
         * - 0：使用渲染器的默认设置（通常是2-3帧）
         * - 1：单缓冲，适用于简单场景或调试
         * - 2：双缓冲，适用于大多数实时渲染应用
         * - 3：三缓冲，适用于高性能渲染或VR应用
         * - 更多：适用于复杂的多线程渲染管线
         *
         * 大小计算注意事项：
         * - 考虑GPU的最小对齐要求（通常是256字节）
         * - 预留足够空间用于未来扩展
         * - 避免过大的缓冲区造成内存浪费
         *
         * 典型用法：
         * ```cpp
         * // 创建相机数据的统一缓冲区集合
         * auto cameraUBOSet = UniformBufferSet::Create(sizeof(CameraData), 3);
         *
         * // 每帧更新数据
         * auto cameraUBO = cameraUBOSet->Get();
         * cameraUBO->SetData(&cameraData, sizeof(CameraData));
         * ```
         */
        static Ref<UniformBufferSet> Create(uint32_t size, uint32_t framesInFlight = 0);
    };
}

#endif