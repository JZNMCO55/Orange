/**
 * @file IRenderFence.h
 * @brief 渲染围栏接口定义
 */

#ifndef ORANGE_IRENDER_FENCE_H
#define ORANGE_IRENDER_FENCE_H

#include "RenderSyncCommon.h"

namespace Orange
{
    namespace Graphics
    {
        // 前向声明
        class IRenderDevice;

        /**
         * @brief 渲染围栏接口
         *
         * 围栏用于CPU-GPU同步，通常用于等待GPU操作完成
         */
        class IRenderFence
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IRenderFence() = default;

            /**
             * @brief 等待围栏信号
             * @param timeout 超时时间（纳秒），UINT64_MAX表示无限等待
             * @return 是否成功等待（超时返回false）
             */
            virtual bool Wait(uint64_t timeout = UINT64_MAX) {}

            /**
             * @brief 重置围栏状态为未触发
             * @return 是否成功重置
             */
            virtual bool Reset() {}

            /**
             * @brief 获取围栏状态
             * @return true表示已触发，false表示未触发
             */
            virtual bool GetStatus() const {}

            /**
             * @brief 获取围栏创建标志
             * @return 围栏创建标志
             */
            virtual FenceCreateFlags GetFlags() const {}

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const {}

            /**
             * @brief 获取原生围栏句柄
             * @return 原生围栏句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeFence() const {}

            /**
             * @brief 设置名称
             * @param name 名称
             */
            virtual void SetName(const char *name) {}

            /**
             * @brief 获取名称
             * @return 名称
             */
            virtual const char *GetName() const {}
        };

        /**
         * @brief 围栏工厂接口
         */
        class IRenderFenceFactory
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IRenderFenceFactory() = default;

            /**
             * @brief 创建围栏
             * @param createInfo 创建信息
             * @return 围栏，失败返回nullptr
             */
            virtual IRenderFence *CreateFence(const FenceCreateInfo &createInfo) {}

            /**
             * @brief 销毁围栏
             * @param fence 要销毁的围栏
             */
            virtual void DestroyFence(IRenderFence *fence) {}

            /**
             * @brief 等待多个围栏
             * @param fences 围栏数组
             * @param fenceCount 围栏数量
             * @param waitAll 是否等待所有围栏
             * @param timeout 超时时间（纳秒），UINT64_MAX表示无限等待
             * @return 是否成功等待（超时返回false）
             */
            virtual bool WaitForFences(IRenderFence **fences, uint32_t fenceCount, bool waitAll, uint64_t timeout = UINT64_MAX) {}

            /**
             * @brief 重置多个围栏
             * @param fences 围栏数组
             * @param fenceCount 围栏数量
             * @return 是否成功重置
             */
            virtual bool ResetFences(IRenderFence **fences, uint32_t fenceCount) {}
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_IRENDER_FENCE_H