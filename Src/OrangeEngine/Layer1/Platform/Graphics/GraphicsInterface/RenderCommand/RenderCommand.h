/**
 * @file RenderCommand.h
 * @brief 渲染命令管理主头文件
 */

#ifndef ORANGE_RENDER_COMMAND_H
#define ORANGE_RENDER_COMMAND_H

#include "CommandCommon.h"
#include "IRenderCommandBuffer.h"
#include "IRenderCommandEncoder.h"
#include "IRenderCommandQueue.h"

namespace Orange
{
    namespace Graphics
    {
        /**
         * @brief 初始化命令管理子系统
         * @param device 渲染设备
         * @return 是否成功初始化
         */
        bool InitializeCommandSystem(IRenderDevice *device);

        /**
         * @brief 关闭命令管理子系统
         * @param device 渲染设备
         */
        void ShutdownCommandSystem(IRenderDevice *device);

        /**
         * @brief 创建命令缓冲区工厂
         * @param device 渲染设备
         * @return 命令缓冲区工厂
         */
        IRenderCommandBufferFactory *CreateCommandBufferFactory(IRenderDevice *device);

        /**
         * @brief 创建命令队列工厂
         * @param device 渲染设备
         * @return 命令队列工厂
         */
        IRenderCommandQueueFactory *CreateCommandQueueFactory(IRenderDevice *device);

        /**
         * @brief 创建命令编码器工厂
         * @param device 渲染设备
         * @return 命令编码器工厂
         */
        IRenderCommandEncoderFactory *CreateCommandEncoderFactory(IRenderDevice *device);

        /**
         * @brief 获取命令缓冲区级别名称
         * @param level 命令缓冲区级别
         * @return 命令缓冲区级别名称
         */
        const char *GetCommandBufferLevelName(CommandBufferLevel level);

        /**
         * @brief 获取命令缓冲区状态名称
         * @param state 命令缓冲区状态
         * @return 命令缓冲区状态名称
         */
        const char *GetCommandBufferStateName(CommandBufferState state);

        /**
         * @brief 获取命令队列类型名称
         * @param type 命令队列类型
         * @return 命令队列类型名称
         */
        const char *GetCommandQueueTypeName(CommandQueueType type);

        /**
         * @brief 获取命令队列优先级名称
         * @param priority 命令队列优先级
         * @return 命令队列优先级名称
         */
        const char *GetCommandQueuePriorityName(CommandQueuePriority priority);

        /**
         * @brief 获取命令编码器类型名称
         * @param type 命令编码器类型
         * @return 命令编码器类型名称
         */
        const char *GetCommandEncoderTypeName(CommandEncoderType type);

        /**
         * @brief 创建单次使用命令缓冲区
         * @param device 渲染设备
         * @param queueType 队列类型
         * @param level 命令缓冲区级别
         * @return 命令缓冲区，失败返回nullptr
         */
        IRenderCommandBuffer *CreateSingleUseCommandBuffer(
            IRenderDevice *device,
            CommandQueueType queueType = CommandQueueType::Graphics,
            CommandBufferLevel level = CommandBufferLevel::Primary);

        /**
         * @brief 提交单次使用命令缓冲区
         * @param commandBuffer 命令缓冲区
         * @param queue 命令队列，nullptr则使用相应类型的主队列
         * @param waitSemaphore 等待信号量
         * @param signalSemaphore 信号信号量
         * @param fence 围栏
         * @param waitForCompletion 是否等待完成
         * @return 是否成功提交
         */
        bool SubmitSingleUseCommandBuffer(
            IRenderCommandBuffer *commandBuffer,
            IRenderCommandQueue *queue = nullptr,
            IRenderSemaphore *waitSemaphore = nullptr,
            IRenderSemaphore *signalSemaphore = nullptr,
            IRenderFence *fence = nullptr,
            bool waitForCompletion = true);

        /**
         * @brief 执行单次命令
         * @param device 渲染设备
         * @param queueType 队列类型
         * @param callback 命令回调函数
         * @param userData 用户数据
         * @return 是否成功执行
         */
        bool ExecuteOneTimeCommand(
            IRenderDevice *device,
            CommandQueueType queueType,
            void (*callback)(IRenderCommandBuffer *cmdBuffer, void *userData),
            void *userData = nullptr);

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_RENDER_COMMAND_H