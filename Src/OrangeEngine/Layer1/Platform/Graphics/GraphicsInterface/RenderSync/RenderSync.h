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

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_RENDERER_SYNC_H