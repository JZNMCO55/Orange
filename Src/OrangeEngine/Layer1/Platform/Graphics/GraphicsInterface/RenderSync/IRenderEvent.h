/**
 * @file IRenderEvent.h
 * @brief 渲染事件接口定义
 */

#ifndef ORANGE_IRENDER_EVENT_H
#define ORANGE_IRENDER_EVENT_H

#include "RenderSyncCommon.h"

namespace Orange
{
    namespace Graphics
    {
        // 前向声明
        class IRenderDevice;

        /**
         * @brief 渲染事件接口
         *
         * 事件用于GPU内部同步，可以在命令缓冲区中设置和等待
         */
        class IRenderEvent
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IRenderEvent() = default;

            /**
             * @brief 获取事件状态
             * @return true表示已触发，false表示未触发
             */
            virtual bool GetStatus() const { return false; }

            /**
             * @brief 设置事件（CPU端）
             * @return 是否成功设置
             * @note 这会从CPU端触发事件，通常用于调试或特殊同步场景
             */
            virtual bool Set() { return false; }

            /**
             * @brief 重置事件（CPU端）
             * @return 是否成功重置
             */
            virtual bool Reset() { return false; }

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const { return nullptr; }

            /**
             * @brief 获取原生事件句柄
             * @return 原生事件句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeEvent() const { return nullptr; }

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
         * @brief 事件工厂接口
         */
        class IRenderEventFactory
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IRenderEventFactory() = default;

            /**
             * @brief 创建事件
             * @param createInfo 创建信息
             * @return 事件，失败返回nullptr
             */
            virtual IRenderEvent *CreateEvent(const EventCreateInfo &createInfo) { return nullptr; }

            /**
             * @brief 销毁事件
             * @param event 要销毁的事件
             */
            virtual void DestroyEvent(IRenderEvent *event) {}
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_IRENDER_EVENT_H