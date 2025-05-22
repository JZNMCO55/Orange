/**
 * @file IRenderResources.h
 * @brief 渲染资源接口，定义缓冲区、纹理、采样器等
 */

#ifndef ORANGE_IRENDER_RESOURCES_H
#define ORANGE_IRENDER_RESOURCES_H

#include "../RenderCommon/RenderCommon.h"

namespace Orange
{
    namespace Graphics
    {

        // 前向声明
        class IRenderDevice;
        class IFramebuffer;
        class IRenderTextureView;

        /**
         * @brief 渲染缓冲区接口
         *
         * 渲染缓冲区是GPU内存的线性块，用于存储顶点、索引、统一数据等。
         */
        class IRenderBuffer
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IRenderBuffer() = default;

            /**
             * @brief 获取缓冲区大小
             * @return 缓冲区大小（字节）
             */
            virtual uint64_t GetSize() const = 0;

            /**
             * @brief 获取缓冲区类型
             * @return 缓冲区类型
             */
            virtual BufferType GetType() const = 0;

            /**
             * @brief 获取是否为主机可见
             * @return 是否为主机可见
             */
            virtual bool IsHostVisible() const = 0;

            /**
             * @brief 映射缓冲区内存
             * @param offset 偏移量
             * @param size 大小，0表示映射整个缓冲区
             * @return 映射的内存指针，失败返回nullptr
             */
            virtual void *Map(uint64_t offset = 0, uint64_t size = 0) = 0;

            /**
             * @brief 解除缓冲区内存映射
             */
            virtual void Unmap() = 0;

            /**
             * @brief 刷新映射内存（对于某些平台需要）
             * @param offset 偏移量
             * @param size 大小，0表示整个缓冲区
             */
            virtual void FlushMappedMemory(uint64_t offset = 0, uint64_t size = 0) = 0;

            /**
             * @brief 使映射内存失效（对于某些平台需要）
             * @param offset 偏移量
             * @param size 大小，0表示整个缓冲区
             */
            virtual void InvalidateMappedMemory(uint64_t offset = 0, uint64_t size = 0) = 0;

            /**
             * @brief 获取设备地址（仅在支持的平台上）
             * @return 设备地址，不支持则返回0
             */
            virtual uint64_t GetDeviceAddress() const = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 获取原生缓冲区句柄
             * @return 原生缓冲区句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeBuffer() const = 0;
        };

        /**
         * @brief 渲染纹理接口
         *
         * 渲染纹理是GPU中的多维数据数组，用于存储图像数据。
         */
        class IRenderTexture
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IRenderTexture() = default;

            /**
             * @brief 获取纹理类型
             * @return 纹理类型
             */
            virtual TextureType GetType() const = 0;

            /**
             * @brief 获取像素格式
             * @return 像素格式
             */
            virtual PixelFormat GetFormat() const = 0;

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
             * @brief 获取深度
             * @return 深度
             */
            virtual uint32_t GetDepth() const = 0;

            /**
             * @brief 获取Mip级别数量
             * @return Mip级别数量
             */
            virtual uint32_t GetMipLevels() const = 0;

            /**
             * @brief 获取数组层数
             * @return 数组层数
             */
            virtual uint32_t GetArrayLayers() const = 0;

            /**
             * @brief 获取采样数
             * @return 采样数
             */
            virtual uint32_t GetSampleCount() const = 0;

            /**
             * @brief 检查是否是立方体纹理
             * @return 是否是立方体纹理
             */
            virtual bool IsCubemap() const = 0;

            /**
             * @brief 检查是否可以用作渲染目标
             * @return 是否可以用作渲染目标
             */
            virtual bool IsRenderTarget() const = 0;

            /**
             * @brief 检查是否是深度模板纹理
             * @return 是否是深度模板纹理
             */
            virtual bool IsDepthStencil() const = 0;

            /**
             * @brief 创建纹理视图
             * @param createInfo 纹理视图创建信息
             * @return 新创建的纹理视图，失败返回nullptr
             */
            virtual IRenderTextureView *CreateView(const TextureViewCreateInfo &createInfo) = 0;

            /**
             * @brief 获取默认视图
             * @return 默认纹理视图
             */
            virtual IRenderTextureView *GetDefaultView() const = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 获取原生纹理句柄
             * @return 原生纹理句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeTexture() const = 0;
        };

        /**
         * @brief 渲染纹理视图接口
         *
         * 渲染纹理视图是对纹理的特定视图，如特定的Mip级别、数组层或格式。
         */
        class IRenderTextureView
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IRenderTextureView() = default;

            /**
             * @brief 获取视图类型
             * @return 视图类型
             */
            virtual TextureViewType GetType() const = 0;

            /**
             * @brief 获取像素格式
             * @return 像素格式
             */
            virtual PixelFormat GetFormat() const = 0;

            /**
             * @brief 获取基础Mip级别
             * @return 基础Mip级别
             */
            virtual uint32_t GetBaseMipLevel() const = 0;

            /**
             * @brief 获取Mip级别数量
             * @return Mip级别数量
             */
            virtual uint32_t GetLevelCount() const = 0;

            /**
             * @brief 获取基础数组层
             * @return 基础数组层
             */
            virtual uint32_t GetBaseArrayLayer() const = 0;

            /**
             * @brief 获取数组层数量
             * @return 数组层数量
             */
            virtual uint32_t GetLayerCount() const = 0;

            /**
             * @brief 获取纹理
             * @return 纹理
             */
            virtual IRenderTexture *GetTexture() const = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 获取原生纹理视图句柄
             * @return 原生纹理视图句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeTextureView() const = 0;
        };

        /**
         * @brief 渲染采样器接口
         *
         * 渲染采样器定义了如何从纹理中采样，如过滤模式、寻址模式等。
         */
        class IRenderSampler
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IRenderSampler() = default;

            /**
             * @brief 获取放大过滤模式
             * @return 放大过滤模式
             */
            virtual TextureFilterMode GetMagFilter() const = 0;

            /**
             * @brief 获取缩小过滤模式
             * @return 缩小过滤模式
             */
            virtual TextureFilterMode GetMinFilter() const = 0;

            /**
             * @brief 获取Mipmap过滤模式
             * @return Mipmap过滤模式
             */
            virtual MipmapFilterMode GetMipmapMode() const = 0;

            /**
             * @brief 获取U寻址模式
             * @return U寻址模式
             */
            virtual TextureAddressMode GetAddressModeU() const = 0;

            /**
             * @brief 获取V寻址模式
             * @return V寻址模式
             */
            virtual TextureAddressMode GetAddressModeV() const = 0;

            /**
             * @brief 获取W寻址模式
             * @return W寻址模式
             */
            virtual TextureAddressMode GetAddressModeW() const = 0;

            /**
             * @brief 获取Mip LOD偏移
             * @return Mip LOD偏移
             */
            virtual float GetMipLodBias() const = 0;

            /**
             * @brief 获取是否启用各向异性过滤
             * @return 是否启用各向异性过滤
             */
            virtual bool IsAnisotropyEnabled() const = 0;

            /**
             * @brief 获取最大各向异性
             * @return 最大各向异性
             */
            virtual float GetMaxAnisotropy() const = 0;

            /**
             * @brief 获取是否启用比较
             * @return 是否启用比较
             */
            virtual bool IsCompareEnabled() const = 0;

            /**
             * @brief 获取比较操作
             * @return 比较操作
             */
            virtual CompareOp GetCompareOp() const = 0;

            /**
             * @brief 获取最小LOD
             * @return 最小LOD
             */
            virtual float GetMinLod() const = 0;

            /**
             * @brief 获取最大LOD
             * @return 最大LOD
             */
            virtual float GetMaxLod() const = 0;

            /**
             * @brief 获取边框颜色
             * @return 边框颜色
             */
            virtual Color4f GetBorderColor() const = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 获取原生采样器句柄
             * @return 原生采样器句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeSampler() const = 0;
        };

        /**
         * @brief 着色器模块接口
         *
         * 着色器模块封装了编译后的着色器代码。
         */
        class IShaderModule
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IShaderModule() = default;

            /**
             * @brief 获取着色器类型
             * @return 着色器类型
             */
            virtual ShaderType GetType() const = 0;

            /**
             * @brief 获取着色器入口点
             * @return 着色器入口点
             */
            virtual const std::string &GetEntryPoint() const = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 获取原生着色器模块句柄
             * @return 原生着色器模块句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeShaderModule() const = 0;
        };

        /**
         * @brief 栅栏接口（用于CPU-GPU同步）
         */
        class IFence
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IFence() = default;

            /**
             * @brief 等待栅栏触发
             * @param timeoutNs 超时时间（纳秒），0表示立即返回，UINT64_MAX表示无限等待
             * @return 是否成功（超时返回false）
             */
            virtual bool Wait(uint64_t timeoutNs = UINT64_MAX) = 0;

            /**
             * @brief 重置栅栏状态
             * @return 是否成功重置
             */
            virtual bool Reset() = 0;

            /**
             * @brief 检查栅栏是否已触发
             * @return 是否已触发
             */
            virtual bool IsSignaled() const = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 获取原生栅栏句柄
             * @return 原生栅栏句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeFence() const = 0;
        };

        /**
         * @brief 信号量接口（用于GPU-GPU同步）
         */
        class ISemaphore
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~ISemaphore() = default;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 获取原生信号量句柄
             * @return 原生信号量句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeSemaphore() const = 0;
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_IRENDER_RESOURCES_H