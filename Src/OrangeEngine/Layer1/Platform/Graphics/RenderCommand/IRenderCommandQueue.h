/**
 * @file IRenderCommandQueue.h
 * @brief 渲染命令队列接口定义
 */

#ifndef ORANGE_IRENDER_COMMAND_QUEUE_H
#define ORANGE_IRENDER_COMMAND_QUEUE_H

#include "CommandCommon.h"

namespace Orange
{
    namespace Graphics
    {
        // 前向声明
        class IRenderDevice;
        class IRenderCommandBuffer;
        class IRenderFence;
        class IRenderSemaphore;
        class ISwapChain;

        /**
         * @brief 渲染命令队列接口
         *
         * 命令队列用于提交命令缓冲区并执行GPU命令
         */
        class IRenderCommandQueue
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IRenderCommandQueue() = default;

            /**
             * @brief 提交命令缓冲区
             * @param submitInfo 提交信息
             * @return 是否成功提交
             */
            virtual bool Submit(const QueueSubmitInfo &submitInfo) = 0;

            /**
             * @brief 提交多个命令缓冲区
             * @param submitInfos 提交信息数组
             * @param submitCount 提交信息数量
             * @return 是否成功提交
             */
            virtual bool SubmitBatch(const QueueSubmitInfo *submitInfos, uint32_t submitCount) = 0;

            /**
             * @brief 等待队列空闲
             * @return 是否成功等待
             */
            virtual bool WaitIdle() = 0;

            /**
             * @brief 向交换链呈现
             * @param swapChain 交换链
             * @param imageIndex 图像索引
             * @param waitSemaphore 等待信号量
             * @return 是否成功呈现
             */
            virtual bool Present(ISwapChain *swapChain, uint32_t imageIndex, IRenderSemaphore *waitSemaphore = nullptr) = 0;

            /**
             * @brief 绑定稀疏内存
             * @param bufferBinds 缓冲区绑定数组
             * @param bufferBindCount 缓冲区绑定数量
             * @param textureBinds 纹理绑定数组
             * @param textureBindCount 纹理绑定数量
             * @param signalSemaphore 信号信号量
             * @param fence 围栏
             * @return 是否成功绑定
             */
            virtual bool BindSparseMemory(
                const void *bufferBinds,
                uint32_t bufferBindCount,
                const void *textureBinds,
                uint32_t textureBindCount,
                IRenderSemaphore *signalSemaphore = nullptr,
                IRenderFence *fence = nullptr) = 0;

            /**
             * @brief 获取队列类型
             * @return 队列类型
             */
            virtual CommandQueueType GetType() const = 0;

            /**
             * @brief 获取队列族索引
             * @return 队列族索引
             */
            virtual uint32_t GetQueueFamilyIndex() const = 0;

            /**
             * @brief 获取队列索引
             * @return 队列索引
             */
            virtual uint32_t GetQueueIndex() const = 0;

            /**
             * @brief 是否支持呈现
             * @return 是否支持呈现
             */
            virtual bool SupportPresent() const = 0;

            /**
             * @brief 获取队列优先级
             * @return 队列优先级
             */
            virtual CommandQueuePriority GetPriority() const = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 获取原生队列句柄
             * @return 原生队列句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeQueue() const = 0;

            /**
             * @brief 设置名称
             * @param name 名称
             */
            virtual void SetName(const char *name) = 0;

            /**
             * @brief 获取名称
             * @return 名称
             */
            virtual const char *GetName() const = 0;
        };

        /**
         * @brief 命令队列工厂接口（通常作为IRenderDevice的一部分）
         */
        class IRenderCommandQueueFactory
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IRenderCommandQueueFactory() = default;

            /**
             * @brief 创建命令队列
             * @param createInfo 创建信息
             * @return 命令队列，失败返回nullptr
             */
            virtual IRenderCommandQueue *CreateCommandQueue(const CommandQueueCreateInfo &createInfo) = 0;

            /**
             * @brief 销毁命令队列
             * @param commandQueue 要销毁的命令队列
             */
            virtual void DestroyCommandQueue(IRenderCommandQueue *commandQueue) = 0;

            /**
             * @brief 获取队列族属性
             * @param queueFamilyIndex 队列族索引
             * @param outQueueFlags 输出队列标志
             * @param outQueueCount 输出队列数量
             * @param outSupportPresent 输出是否支持呈现
             * @return 是否成功获取
             */
            virtual bool GetQueueFamilyProperties(
                uint32_t queueFamilyIndex,
                QueueFlags &outQueueFlags,
                uint32_t &outQueueCount,
                bool &outSupportPresent) = 0;

            /**
             * @brief 获取队列族数量
             * @return 队列族数量
             */
            virtual uint32_t GetQueueFamilyCount() const = 0;

            /**
             * @brief 查找支持特定操作的队列族
             * @param queueFlags 队列标志
             * @param supportPresent 是否需要支持呈现
             * @param startIndex 开始索引
             * @return 队列族索引，失败返回UINT32_MAX
             */
            virtual uint32_t FindQueueFamily(
                QueueFlags queueFlags,
                bool supportPresent = false,
                uint32_t startIndex = 0) const = 0;

            /**
             * @brief 获取主要图形队列
             * @return 图形队列
             */
            virtual IRenderCommandQueue *GetMainGraphicsQueue() const = 0;

            /**
             * @brief 获取主要计算队列
             * @return 计算队列
             */
            virtual IRenderCommandQueue *GetMainComputeQueue() const = 0;

            /**
             * @brief 获取主要传输队列
             * @return 传输队列
             */
            virtual IRenderCommandQueue *GetMainTransferQueue() const = 0;

            /**
             * @brief 获取主要呈现队列
             * @return 呈现队列
             */
            virtual IRenderCommandQueue *GetMainPresentQueue() const = 0;
        };

        /**
         * @brief 创建简单的命令缓冲区提交信息
         * @param commandBuffer 命令缓冲区
         * @param waitStages 等待阶段
         * @return 命令缓冲区提交信息
         */
        inline CommandBufferSubmitInfo CreateCommandBufferSubmitInfo(
            IRenderCommandBuffer *commandBuffer,
            PipelineStageFlags waitStages = static_cast<PipelineStageFlags>(PipelineStageFlagBits::TopOfPipe))
        {
            CommandBufferSubmitInfo info;
            info.commandBuffer = commandBuffer;
            info.waitStages = waitStages;
            return info;
        }

        /**
         * @brief 创建简单的队列提交信息
         * @param commandBuffer 命令缓冲区
         * @param waitSemaphore 等待信号量
         * @param signalSemaphore 信号信号量
         * @param fence 围栏
         * @param waitStages 等待阶段
         * @return 队列提交信息
         */
        inline QueueSubmitInfo CreateQueueSubmitInfo(
            IRenderCommandBuffer *commandBuffer,
            IRenderSemaphore *waitSemaphore = nullptr,
            IRenderSemaphore *signalSemaphore = nullptr,
            IRenderFence *fence = nullptr,
            PipelineStageFlags waitStages = static_cast<PipelineStageFlags>(PipelineStageFlagBits::TopOfPipe))
        {
            CommandBufferSubmitInfo cbInfo = CreateCommandBufferSubmitInfo(commandBuffer, waitStages);

            QueueSubmitInfo info;
            info.commandBufferCount = 1;
            info.commandBuffers = &cbInfo;

            if (waitSemaphore)
            {
                info.waitSemaphoreCount = 1;
                info.waitSemaphores = &waitSemaphore;
                info.waitValues = nullptr;
            }

            if (signalSemaphore)
            {
                info.signalSemaphoreCount = 1;
                info.signalSemaphores = &signalSemaphore;
                info.signalValues = nullptr;
            }

            info.fence = fence;

            return info;
        }

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_IRENDER_COMMAND_QUEUE_H