/**
 * @file RenderSyncCommon.h
 * @brief 渲染同步通用定义
 */

#ifndef ORANGE_RENDER_SYNC_COMMON_H
#define ORANGE_RENDER_SYNC_COMMON_H

#include "../RenderCommon/RenderCommon.h"

namespace Orange
{
    namespace Graphics
    {
        /**
         * @brief 同步原语类型
         */
        enum class SyncPrimitiveType
        {
            Fence,     ///< 围栏，用于CPU-GPU同步
            Semaphore, ///< 信号量，用于GPU-GPU同步
            Event,     ///< 事件，用于GPU内部同步
        };

        /**
         * @brief 围栏创建标志
         */
        enum class FenceCreateFlagBits : uint32_t
        {
            None = 0,
            Signaled = 0x00000001 ///< 创建时已发出信号
        };
        // 定义类型别名
        using FenceCreateFlags = uint32_t;
        DEFINE_ENUM_FLAG_OPERATORS(FenceCreateFlagBits, FenceCreateFlags);

        /**
         * @brief 围栏等待标志
         */
        enum class FenceWaitFlagBits : uint32_t
        {
            None = 0,
            Any = 0x00000001 ///< 任意一个围栏满足条件即可返回
        };
        // 定义类型别名
        using FenceWaitFlags = uint32_t;
        DEFINE_ENUM_FLAG_OPERATORS(FenceWaitFlagBits, FenceWaitFlags);

        /**
         * @brief 信号量类型
         */
        enum class SemaphoreType
        {
            Binary = 0,  ///< 二进制信号量
            Timeline = 1 ///< 时间线信号量
        };

        /**
         * @brief 围栏创建信息
         */
        struct FenceCreateInfo
        {
            FenceCreateFlags flags = static_cast<FenceCreateFlags>(FenceCreateFlagBits::None); ///< 创建标志
            const char *debugName = nullptr;                                                   ///< 调试名称
        };

        /**
         * @brief 信号量创建信息
         */
        struct SemaphoreCreateInfo
        {
            SemaphoreType type = SemaphoreType::Binary; ///< 信号量类型
            uint64_t initialValue = 0;                                                ///< 初始值（仅用于时间线信号量）
            const char *debugName = nullptr;                                        ///< 调试名称
        };

        /**
         * @brief 事件创建信息
         */
        struct EventCreateInfo
        {
            bool signaled = false;           ///< 是否已触发
            const char *debugName = nullptr; ///< 调试名称
        };

        /**
         * @brief 同步点类型
         */
        enum class SyncPointType
        {
            Submit,              ///< 提交点
            Present,             ///< 呈现点
            Acquire,             ///< 获取点
            ExecutionDependency, ///< 执行依赖点
            MemoryDependency,    ///< 内存依赖点
        };

        /**
         * @brief 同步点描述
         */
        struct SyncPointDesc
        {
            SyncPointType type = SyncPointType::Submit; ///< 同步点类型
            uint32_t queueFamilyIndex = 0;              ///< 队列族索引
            uint32_t queueIndex = 0;                    ///< 队列索引
            uint64_t value = 1;                         ///< 同步值（用于时间线信号量）
        };

        /**
         * @brief 等待超时
         */
        constexpr uint64_t SYNC_WAIT_INFINITE = ~0ULL;

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_RENDER_SYNC_COMMON_H