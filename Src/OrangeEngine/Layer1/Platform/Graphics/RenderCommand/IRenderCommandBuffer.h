/**
 * @file IRenderCommandBuffer.h
 * @brief 渲染命令缓冲区接口定义
 */

#ifndef ORANGE_IRENDER_COMMAND_BUFFER_H
#define ORANGE_IRENDER_COMMAND_BUFFER_H

#include "CommandCommon.h"

namespace Orange
{
    namespace Graphics
    {
        // 前向声明
        class IRenderDevice;
        class IRenderCommandPool;
        class IRenderCommandEncoder;

        /**
         * @brief 渲染命令缓冲区接口
         *
         * 命令缓冲区用于记录GPU将要执行的命令
         */
        class IRenderCommandBuffer
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IRenderCommandBuffer() = default;

            /**
             * @brief 开始记录命令
             * @param beginInfo 开始信息
             * @return 是否成功开始
             */
            virtual bool Begin(const CommandBufferBeginInfo &beginInfo) = 0;

            /**
             * @brief 结束记录命令
             * @return 是否成功结束
             */
            virtual bool End() = 0;

            /**
             * @brief 重置命令缓冲区
             * @param releaseResources 是否释放资源
             * @return 是否成功重置
             */
            virtual bool Reset(bool releaseResources = false) = 0;

            /**
             * @brief 获取命令缓冲区状态
             * @return 命令缓冲区状态
             */
            virtual CommandBufferState GetState() const = 0;

            /**
             * @brief 获取命令缓冲区级别
             * @return 命令缓冲区级别
             */
            virtual CommandBufferLevel GetLevel() const = 0;

            /**
             * @brief 获取所属命令池
             * @return 命令池
             */
            virtual IRenderCommandPool *GetCommandPool() const = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 获取原生命令缓冲区句柄
             * @return 原生命令缓冲区句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeCommandBuffer() const = 0;

            /**
             * @brief 创建命令编码器
             * @param encoderType 编码器类型
             * @return 命令编码器，失败返回nullptr
             */
            virtual IRenderCommandEncoder *CreateEncoder(CommandEncoderType encoderType) = 0;

            /**
             * @brief 获取当前命令编码器
             * @return 当前命令编码器，没有则返回nullptr
             */
            virtual IRenderCommandEncoder *GetCurrentEncoder() const = 0;

            /**
             * @brief 结束当前编码器
             * @return 是否成功结束
             */
            virtual bool EndEncoder() = 0;

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
         * @brief 渲染命令池接口
         *
         * 命令池是用于分配命令缓冲区的对象
         */
        class IRenderCommandPool
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IRenderCommandPool() = default;

            /**
             * @brief 分配命令缓冲区
             * @param allocateInfo 分配信息
             * @param outCommandBuffers 输出命令缓冲区数组
             * @return 是否成功分配
             */
            virtual bool AllocateCommandBuffers(const CommandBufferAllocateInfo &allocateInfo, IRenderCommandBuffer **outCommandBuffers) = 0;

            /**
             * @brief 释放命令缓冲区
             * @param commandBuffers 命令缓冲区数组
             * @param count 命令缓冲区数量
             */
            virtual void FreeCommandBuffers(IRenderCommandBuffer **commandBuffers, uint32_t count) = 0;

            /**
             * @brief 重置命令池
             * @param releaseResources 是否释放资源
             * @return 是否成功重置
             */
            virtual bool Reset(bool releaseResources = false) = 0;

            /**
             * @brief 获取队列族索引
             * @return 队列族索引
             */
            virtual uint32_t GetQueueFamilyIndex() const = 0;

            /**
             * @brief 获取命令池标志
             * @return 命令池标志
             */
            virtual CommandPoolFlags GetFlags() const = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 获取原生命令池句柄
             * @return 原生命令池句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeCommandPool() const = 0;

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
         * @brief 命令缓冲区工厂接口（通常作为IRenderDevice的一部分）
         */
        class IRenderCommandBufferFactory
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IRenderCommandBufferFactory() = default;

            /**
             * @brief 创建命令池
             * @param createInfo 创建信息
             * @return 命令池，失败返回nullptr
             */
            virtual IRenderCommandPool *CreateCommandPool(const CommandPoolCreateInfo &createInfo) = 0;

            /**
             * @brief 销毁命令池
             * @param commandPool 要销毁的命令池
             */
            virtual void DestroyCommandPool(IRenderCommandPool *commandPool) = 0;
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_IRENDER_COMMAND_BUFFER_H