/**
 * @file RendererSync.h
 * @brief 渲染同步模块主头文件
 */

#ifndef ORANGE_RENDERER_SYNC_H
#define ORANGE_RENDERER_SYNC_H

#include "RenderSyncCommon.h"
#include "IRenderFence.h"
#include "IRenderSemaphore.h"
#include "IRenderEvent.h"

namespace Orange
{
    namespace Graphics
    {
        // 前向声明
        class IRenderDevice;

        /**
         * @brief 初始化同步子系统
         * @param device 渲染设备
         * @return 是否成功初始化
         */
        bool InitializeSyncSystem(IRenderDevice *device);

        /**
         * @brief 关闭同步子系统
         * @param device 渲染设备
         */
        void ShutdownSyncSystem(IRenderDevice *device);

        /**
         * @brief 创建围栏工厂
         * @param device 渲染设备
         * @return 围栏工厂
         */
        IRenderFenceFactory *CreateFenceFactory(IRenderDevice *device);

        /**
         * @brief 创建信号量工厂
         * @param device 渲染设备
         * @return 信号量工厂
         */
        IRenderSemaphoreFactory *CreateSemaphoreFactory(IRenderDevice *device);

        /**
         * @brief 创建事件工厂
         * @param device 渲染设备
         * @return 事件工厂
         */
        IRenderEventFactory *CreateEventFactory(IRenderDevice *device);

        /**
         * @brief 销毁围栏工厂
         * @param factory 围栏工厂
         */
        void DestroyFenceFactory(IRenderFenceFactory *factory);

        /**
         * @brief 销毁信号量工厂
         * @param factory 信号量工厂
         */
        void DestroySemaphoreFactory(IRenderSemaphoreFactory *factory);

        /**
         * @brief 销毁事件工厂
         * @param factory 事件工厂
         */
        void DestroyEventFactory(IRenderEventFactory *factory);

        /**
         * @brief 检查同步系统是否已初始化
         * @return 是否已初始化
         */
        bool IsSyncSystemInitialized();

        /**
         * @brief 获取同步原语类型名称
         * @param type 同步原语类型
         * @return 类型名称
         */
        const char *GetSyncPrimitiveTypeName(SyncPrimitiveType type);

        /**
         * @brief 获取信号量类型名称
         * @param type 信号量类型
         * @return 类型名称
         */
        const char *GetSemaphoreTypeName(SemaphoreType type);

        /**
         * @brief 获取同步点类型名称
         * @param type 同步点类型
         * @return 类型名称
         */
        const char *GetSyncPointTypeName(SyncPointType type);

        /**
         * @brief 验证围栏创建信息
         * @param createInfo 创建信息
         * @return 是否有效
         */
        bool ValidateFenceCreateInfo(const FenceCreateInfo &createInfo);

        /**
         * @brief 验证信号量创建信息
         * @param createInfo 创建信息
         * @return 是否有效
         */
        bool ValidateSemaphoreCreateInfo(const SemaphoreCreateInfo &createInfo);

        /**
         * @brief 验证事件创建信息
         * @param createInfo 创建信息
         * @return 是否有效
         */
        bool ValidateEventCreateInfo(const EventCreateInfo &createInfo);

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_RENDERER_SYNC_H