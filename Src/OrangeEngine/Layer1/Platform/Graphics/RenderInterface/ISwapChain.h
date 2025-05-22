/**
 * @file ISwapChain.h
 * @brief 交换链接口，负责窗口系统集成和屏幕呈现
 */

#ifndef ORANGE_ISWAP_CHAIN_H
#define ORANGE_ISWAP_CHAIN_H

#include "../RenderCommon/RenderCommon.h"

namespace Orange
{
    namespace Graphics
    {

        // 前向声明
        class IRenderDevice;
        class IRenderTexture;
        class ISemaphore;

        /**
         * @brief 交换链接口
         *
         * 交换链负责管理用于显示的图像缓冲区，并处理与窗口系统的集成。
         * 它控制图像的获取、呈现和同步。
         */
        class ISwapChain
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~ISwapChain() = default;

            /**
             * @brief 获取图像数量
             * @return 图像数量
             */
            virtual uint32_t GetImageCount() const = 0;

            /**
             * @brief 获取当前图像索引
             * @return 当前图像索引
             */
            virtual uint32_t GetCurrentImageIndex() const = 0;

            /**
             * @brief 获取宽度
             * @return 宽度
             */
            virtual uint32_t GetWidth() const = 0;

            /**
             * @brief 获取高度
             * @return 高度
             */
            virtual uint32_t GetHeight() const = 0;

            /**
             * @brief 获取格式
             * @return 像素格式
             */
            virtual PixelFormat GetFormat() const = 0;

            /**
             * @brief 获取交换链图像
             * @param index 图像索引
             * @return 交换链图像
             */
            virtual IRenderTexture *GetImage(uint32_t index) const = 0;

            /**
             * @brief 获取当前图像
             * @return 当前图像
             */
            virtual IRenderTexture *GetCurrentImage() const = 0;

            /**
             * @brief 获取下一个可用图像索引
             * @param signalSemaphore 信号量，在图像可用时触发
             * @return 下一个图像索引，失败返回UINT32_MAX
             */
            virtual uint32_t AcquireNextImage(ISemaphore *signalSemaphore) = 0;

            /**
             * @brief 呈现当前图像
             * @param waitSemaphores 等待的信号量列表
             * @return 是否成功呈现
             */
            virtual bool Present(const std::vector<ISemaphore *> &waitSemaphores = {}) = 0;

            /**
             * @brief 调整交换链大小
             * @param width 新宽度
             * @param height 新高度
             * @return 是否成功调整大小
             */
            virtual bool Resize(uint32_t width, uint32_t height) = 0;

            /**
             * @brief 获取垂直同步状态
             * @return 是否启用垂直同步
             */
            virtual bool IsVSyncEnabled() const = 0;

            /**
             * @brief 设置垂直同步状态
             * @param enabled 是否启用垂直同步
             */
            virtual void SetVSyncEnabled(bool enabled) = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 获取原生交换链句柄
             * @return 原生交换链句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeSwapChain() const = 0;

            /**
             * @brief 获取原生表面句柄
             * @return 原生表面句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeSurface() const = 0;
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_ISWAP_CHAIN_H