/**
 * @file CommandCommon.h
 * @brief 渲染命令通用定义
 */

#ifndef ORANGE_COMMAND_COMMON_H
#define ORANGE_COMMAND_COMMON_H

#include "../RenderCommon/RenderCommon.h"

namespace Orange
{
    namespace Graphics
    {
        /**
         * @brief 命令缓冲区级别
         */
        enum class CommandBufferLevel
        {
            Primary,  ///< 主要命令缓冲区，可以直接提交到队列
            Secondary ///< 次要命令缓冲区，只能被主要命令缓冲区执行
        };

        /**
         * @brief 命令缓冲区用途标志位
         */
        enum class CommandBufferUsageFlagBits : uint32_t
        {
            None = 0,
            OneTimeSubmit = 0x01,      ///< 只提交一次
            RenderPassContinue = 0x02, ///< 渲染通道继续
            SimultaneousUse = 0x04,    ///< 可同时使用
            ProtectedMemory = 0x08     ///< 使用受保护内存
        };
        // 定义类型别名
        using CommandBufferUsageFlags = uint32_t;
        DEFINE_ENUM_FLAG_OPERATORS(CommandBufferUsageFlagBits, CommandBufferUsageFlags);

        /**
         * @brief 命令缓冲区状态
         */
        enum class CommandBufferState
        {
            Initial,    ///< 初始状态
            Recording,  ///< 记录中
            Executable, ///< 可执行
            Pending,    ///< 等待执行
            Invalid     ///< 无效
        };

        /**
         * @brief 命令缓冲区开始信息
         */
        struct CommandBufferBeginInfo
        {
            CommandBufferUsageFlags flags = static_cast<CommandBufferUsageFlags>(CommandBufferUsageFlagBits::None); ///< 使用标志
            const void *inheritanceInfo = nullptr;                                                                  ///< 继承信息（用于次要命令缓冲区）
        };

        /**
         * @brief 命令队列类型
         */
        enum class CommandQueueType
        {
            Graphics, ///< 图形队列
            Compute,  ///< 计算队列
            Transfer, ///< 传输队列
            Present   ///< 呈现队列
        };

        /**
         * @brief 命令队列优先级
         */
        enum class CommandQueuePriority
        {
            Low,     ///< 低优先级
            Normal,  ///< 普通优先级
            High,    ///< 高优先级
            Realtime ///< 实时优先级
        };

        /**
         * @brief 命令缓冲区提交标志位
         */
        enum class SubmitFlagBits : uint32_t
        {
            None = 0,
            Protected = 0x01, ///< 受保护提交
            Signaled = 0x02   ///< 提交完成后信号
        };
        // 定义类型别名
        using SubmitFlags = uint32_t;
        DEFINE_ENUM_FLAG_OPERATORS(SubmitFlagBits, SubmitFlags);

        /**
         * @brief 命令缓冲区提交信息
         */
        struct CommandBufferSubmitInfo
        {
            class IRenderCommandBuffer *commandBuffer = nullptr;                                               ///< 命令缓冲区
            PipelineStageFlags waitStages = static_cast<PipelineStageFlags>(PipelineStageFlagBits::TopOfPipe); ///< 等待阶段
        };

        /**
         * @brief 命令队列提交信息
         */
        struct QueueSubmitInfo
        {
            uint32_t commandBufferCount = 0;                                    ///< 命令缓冲区数量
            const CommandBufferSubmitInfo *commandBuffers = nullptr;            ///< 命令缓冲区数组
            uint32_t waitSemaphoreCount = 0;                                    ///< 等待信号量数量
            class IRenderSemaphore **waitSemaphores = nullptr;                  ///< 等待信号量数组
            const uint64_t *waitValues = nullptr;                               ///< 等待值数组（用于时间线信号量）
            uint32_t signalSemaphoreCount = 0;                                  ///< 信号信号量数量
            class IRenderSemaphore **signalSemaphores = nullptr;                ///< 信号信号量数组
            const uint64_t *signalValues = nullptr;                             ///< 信号值数组（用于时间线信号量）
            class IRenderFence *fence = nullptr;                                ///< 围栏
            SubmitFlags flags = static_cast<SubmitFlags>(SubmitFlagBits::None); ///< 提交标志
        };

        /**
         * @brief 命令缓冲区池创建标志位
         */
        enum class CommandPoolFlagBits : uint32_t
        {
            None = 0,
            Transient = 0x01,          ///< 短暂性，优化短生命周期命令缓冲区
            ResetCommandBuffer = 0x02, ///< 允许单独重置命令缓冲区
            Protected = 0x04           ///< 受保护的命令缓冲区
        };
        // 定义类型别名
        using CommandPoolFlags = uint32_t;
        DEFINE_ENUM_FLAG_OPERATORS(CommandPoolFlagBits, CommandPoolFlags);

        /**
         * @brief 命令编码器类型
         */
        enum class CommandEncoderType
        {
            Graphics, ///< 图形编码器
            Compute,  ///< 计算编码器
            Transfer, ///< 传输编码器
            Generic   ///< 通用编码器
        };

        /**
         * @brief 命令池创建信息
         */
        struct CommandPoolCreateInfo
        {
            CommandQueueType queueType = CommandQueueType::Graphics;                           ///< 队列类型
            CommandPoolFlags flags = static_cast<CommandPoolFlags>(CommandPoolFlagBits::None); ///< 创建标志
            uint32_t queueFamilyIndex = 0;                                                     ///< 队列族索引
            const char *debugName = nullptr;                                                   ///< 调试名称
        };

        /**
         * @brief 命令缓冲区分配信息
         */
        struct CommandBufferAllocateInfo
        {
            class IRenderCommandPool *commandPool = nullptr;        ///< 命令池
            CommandBufferLevel level = CommandBufferLevel::Primary; ///< 命令缓冲区级别
            uint32_t count = 1;                                     ///< 分配数量
            const char *debugName = nullptr;                        ///< 调试名称
        };

        /**
         * @brief 命令队列创建信息
         */
        struct CommandQueueCreateInfo
        {
            CommandQueueType type = CommandQueueType::Graphics;           ///< 队列类型
            CommandQueuePriority priority = CommandQueuePriority::Normal; ///< 优先级
            uint32_t queueFamilyIndex = 0;                                ///< 队列族索引
            uint32_t queueIndex = 0;                                      ///< 队列索引
            bool supportPresent = false;                                  ///< 是否支持呈现
            const char *debugName = nullptr;                              ///< 调试名称
        };

        /**
         * @brief 命令编码器创建信息
         */
        struct CommandEncoderCreateInfo
        {
            CommandEncoderType type = CommandEncoderType::Generic; ///< 编码器类型
            class IRenderCommandBuffer *commandBuffer = nullptr;   ///< 关联的命令缓冲区
            const char *debugName = nullptr;                       ///< 调试名称
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_COMMAND_COMMON_H