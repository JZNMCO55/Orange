/**
 * @file IRenderPass.h
 * @brief 渲染通道接口，用于管理渲染目标和附件
 */

#ifndef ORANGE_IRENDER_PASS_H
#define ORANGE_IRENDER_PASS_H

#include "../RenderCommon/RenderCommon.h"

namespace Orange
{
    namespace Graphics
    {

        // 前向声明
        class IRenderDevice;
        class IRenderTexture;
        class IFramebuffer;

        /**
         * @brief 渲染通道接口
         *
         * 渲染通道定义了一系列子通道和它们之间的依赖关系，以及每个子通道使用的附件。
         */
        class IRenderPass
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IRenderPass() = default;

            /**
             * @brief 获取附件数量
             * @return 附件数量
             */
            virtual uint32_t GetAttachmentCount() const { return 0; }

            /**
             * @brief 获取附件描述
             * @param index 附件索引
             * @return 附件描述
             */
            virtual AttachmentDescription GetAttachmentDescription(uint32_t index) const = 0;

            /**
             * @brief 获取子通道数量
             * @return 子通道数量
             */
            virtual uint32_t GetSubpassCount() const { return 0; }

            /**
             * @brief 获取子通道描述
             * @param index 子通道索引
             * @return 子通道描述
             */
            virtual SubpassDescription GetSubpassDescription(uint32_t index) const = 0;

            /**
             * @brief 创建帧缓冲
             * @param createInfo 帧缓冲创建信息
             * @return 新创建的帧缓冲，失败返回nullptr
             */
            virtual IFramebuffer *CreateFramebuffer(const FramebufferCreateInfo &createInfo) { return nullptr; }

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const { return nullptr; }

            /**
             * @brief 获取原生渲染通道句柄
             * @return 原生渲染通道句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeRenderPass() const { return nullptr; }
        };

        /**
         * @brief 帧缓冲接口
         *
         * 帧缓冲包含一组纹理附件，用于渲染操作的输出。
         */
        class IFramebuffer
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IFramebuffer() = default;

            /**
             * @brief 获取宽度
             * @return 宽度
             */
            virtual uint32_t GetWidth() const { return 0; }

            /**
             * @brief 获取高度
             * @return 高度
             */
            virtual uint32_t GetHeight() const { return 0; }

            /**
             * @brief 获取层数
             * @return 层数
             */
            virtual uint32_t GetLayers() const { return 0; }

            /**
             * @brief 获取附件数量
             * @return 附件数量
             */
            virtual uint32_t GetAttachmentCount() const { return 0; }

            /**
             * @brief 获取附件
             * @param index 附件索引
             * @return 附件纹理
             */
            virtual IRenderTexture *GetAttachment(uint32_t index) const { return nullptr; }

            /**
             * @brief 获取渲染通道
             * @return 渲染通道
             */
            virtual IRenderPass *GetRenderPass() const { return nullptr; }

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const { return nullptr; }

            /**
             * @brief 获取原生帧缓冲句柄
             * @return 原生帧缓冲句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeFramebuffer() const { return nullptr; }
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_IRENDER_PASS_H