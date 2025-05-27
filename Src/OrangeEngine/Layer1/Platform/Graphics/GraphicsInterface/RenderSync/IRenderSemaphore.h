/**
 * @file IRenderSemaphore.h
 * @brief 渲染信号量接口定义
 */

#ifndef ORANGE_IRENDER_SEMAPHORE_H
#define ORANGE_IRENDER_SEMAPHORE_H

#include "RenderSyncCommon.h"

namespace Orange
{
    namespace Graphics
    {
        // 前向声明
        class IRenderDevice;

        /**
         * @brief 渲染信号量接口
         *
         * 信号量用于GPU-GPU同步，通常用于队列间同步或帧间同步
         */
        class IRenderSemaphore
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IRenderSemaphore() = default;

            /**
             * @brief 获取信号量类型
             * @return 信号量类型
             */
            virtual SemaphoreType GetType() const { return SemaphoreType::Binary; }

            /**
             * @brief 获取信号量的当前值（仅用于时间线信号量）
             * @return 当前值，非时间线信号量返回0
             */
            virtual uint64_t GetCounterValue() const { return 0; }

            /**
             * @brief 等待信号量达到指定值（仅用于时间线信号量）
             * @param value 等待的值
             * @param timeout 超时时间（纳秒），UINT64_MAX表示无限等待
             * @return 是否成功等待（超时返回false）
             * @note 仅支持时间线信号量
             */
            virtual bool Wait(uint64_t value, uint64_t timeout = UINT64_MAX) { return false; }

            /**
             * @brief 信号触发（仅用于时间线信号量）
             * @param value 信号值
             * @return 是否成功触发
             * @note 仅支持时间线信号量，二进制信号量必须通过命令队列提交触发
             */
            virtual bool Signal(uint64_t value) { return false; }

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const { return nullptr; }

            /**
             * @brief 获取原生信号量句柄
             * @return 原生信号量句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeSemaphore() const { return nullptr; }

            /**
             * @brief 设置名称
             * @param name 名称
             */
            virtual void SetName(const char *name) {}

            /**
             * @brief 获取名称
             * @return 名称
             */
            virtual const char *GetName() const { return nullptr; }
        };

        /**
         * @brief 信号量工厂接口
         */
        class IRenderSemaphoreFactory
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IRenderSemaphoreFactory() = default;

            /**
             * @brief 创建信号量
             * @param createInfo 创建信息
             * @return 信号量，失败返回nullptr
             */
            virtual IRenderSemaphore *CreateSemaphore(const SemaphoreCreateInfo &createInfo) {}

            /**
             * @brief 销毁信号量
             * @param semaphore 要销毁的信号量
             */
            virtual void DestroySemaphore(IRenderSemaphore *semaphore) {}

            /**
             * @brief 等待多个时间线信号量
             * @param semaphores 信号量数组
             * @param values 等待值数组
             * @param semaphoreCount 信号量数量
             * @param waitAll 是否等待所有信号量
             * @param timeout 超时时间（纳秒），UINT64_MAX表示无限等待
             * @return 是否成功等待（超时返回false）
             * @note 仅支持时间线信号量
             */
            virtual bool WaitSemaphores(IRenderSemaphore **semaphores, const uint64_t *values, uint32_t semaphoreCount, bool waitAll, uint64_t timeout = UINT64_MAX) { return false; }

            /**
             * @brief 信号触发多个时间线信号量
             * @param semaphores 信号量数组
             * @param values 信号值数组
             * @param semaphoreCount 信号量数量
             * @return 是否成功触发
             * @note 仅支持时间线信号量
             */
            virtual bool SignalSemaphores(IRenderSemaphore **semaphores, const uint64_t *values, uint32_t semaphoreCount) { return false; }
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_IRENDER_SEMAPHORE_H