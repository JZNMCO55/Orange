/**
 * @file ITexture.h
 * @brief 纹理接口定义
 */

#ifndef ORANGE_ITEXTURE_H
#define ORANGE_ITEXTURE_H

#include "../RenderCommon/RenderCommon.h"

namespace Orange
{
    namespace Graphics
    {

        // 前向声明
        class IRenderDevice;
        class ISampler;

        /**
         * @brief 纹理视图接口
         *
         * 纹理视图是对纹理的特定视图，如特定的Mip级别、数组层或格式。
         */
        class ITextureView
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~ITextureView() = default;

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
            virtual class ITexture *GetTexture() const = 0;

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

            /**
             * @brief 获取绑定信息
             * @return 资源绑定描述
             */
            virtual ResourceBindingDesc GetBindingDesc() const = 0;
        };

        /**
         * @brief 纹理接口
         *
         * 纹理是GPU中的多维数据数组，用于存储图像数据。
         */
        class ITexture
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~ITexture() = default;

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
             * @brief 获取纹理用途
             * @return 纹理用途标志
             */
            virtual TextureUsageFlags GetUsage() const = 0;

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
            virtual ITextureView *CreateView(const TextureViewCreateInfo &createInfo) = 0;

            /**
             * @brief 获取默认视图
             * @return 默认纹理视图
             */
            virtual ITextureView *GetDefaultView() const = 0;

            /**
             * @brief 获取特定子资源的默认视图
             * @param mipLevel Mip级别
             * @param arrayLayer 数组层
             * @return 子资源视图
             */
            virtual ITextureView *GetSubresourceView(uint32_t mipLevel, uint32_t arrayLayer) = 0;

            /**
             * @brief 更新纹理数据
             * @param data 源数据指针
             * @param dataSize 数据大小
             * @param subresource 子资源描述
             * @return 是否成功更新
             */
            virtual bool Update(const void *data, uint64_t dataSize, const TextureSubresourceDesc &subresource) = 0;

            /**
             * @brief 获取当前纹理的内存状态
             * @return 资源状态
             */
            virtual ResourceState GetState() const = 0;

            /**
             * @brief 转换纹理状态
             * @param newState 新状态
             * @param immediate 是否立即执行转换
             * @return 是否成功转换
             */
            virtual bool TransitionState(ResourceState newState, bool immediate = true) = 0;

            /**
             * @brief 生成Mipmap
             * @param immediate 是否立即执行
             * @return 是否成功生成
             */
            virtual bool GenerateMipmaps(bool immediate = true) = 0;

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

            /**
             * @brief 获取绑定信息
             * @return 资源绑定描述
             */
            virtual ResourceBindingDesc GetBindingDesc() const = 0;
        };

        /**
         * @brief 创建纹理描述
         */
        struct TextureCreateDesc
        {
            TextureType type = TextureType::Texture2D;                             ///< 纹理类型
            PixelFormat format = PixelFormat::RGBA8_UNORM;                         ///< 像素格式
            uint32_t width = 1;                                                    ///< 宽度
            uint32_t height = 1;                                                   ///< 高度
            uint32_t depth = 1;                                                    ///< 深度
            uint32_t mipLevels = 1;                                                ///< Mip级别数量
            uint32_t arrayLayers = 1;                                              ///< 数组层数量
            uint32_t sampleCount = 1;                                              ///< 采样数
            TextureUsageFlags usage = TextureUsageFlagBits::Sampled;               ///< 纹理用途
            MemoryPropertyFlags memoryFlags = MemoryPropertyFlagBits::DeviceLocal; ///< 内存属性
            SharingMode sharingMode = SharingMode::Exclusive;                      ///< 共享模式
            ResourceState initialState = ResourceState::Undefined;                 ///< 初始状态
            bool cubemap = false;                                                  ///< 是否为立方体纹理
            bool generateMips = false;                                             ///< 是否生成Mipmap
            const void *initialData = nullptr;                                     ///< 初始数据
            uint64_t initialDataSize = 0;                                          ///< 初始数据大小
            const char *debugName = nullptr;                                       ///< 调试名称

            /**
             * @brief 创建2D纹理描述
             * @param texWidth 宽度
             * @param texHeight 高度
             * @param texFormat 像素格式
             * @param texUsage 纹理用途
             * @return 纹理创建描述
             */
            static TextureCreateDesc Texture2D(
                uint32_t texWidth,
                uint32_t texHeight,
                PixelFormat texFormat = PixelFormat::RGBA8_UNORM,
                TextureUsageFlags texUsage = TextureUsageFlagBits::Sampled | TextureUsageFlagBits::TransferDst)
            {
                TextureCreateDesc desc;
                desc.type = TextureType::Texture2D;
                desc.format = texFormat;
                desc.width = texWidth;
                desc.height = texHeight;
                desc.usage = texUsage;
                return desc;
            }

            /**
             * @brief 创建3D纹理描述
             * @param texWidth 宽度
             * @param texHeight 高度
             * @param texDepth 深度
             * @param texFormat 像素格式
             * @param texUsage 纹理用途
             * @return 纹理创建描述
             */
            static TextureCreateDesc Texture3D(
                uint32_t texWidth,
                uint32_t texHeight,
                uint32_t texDepth,
                PixelFormat texFormat = PixelFormat::RGBA8_UNORM,
                TextureUsageFlags texUsage = TextureUsageFlagBits::Sampled | TextureUsageFlagBits::TransferDst)
            {
                TextureCreateDesc desc;
                desc.type = TextureType::Texture3D;
                desc.format = texFormat;
                desc.width = texWidth;
                desc.height = texHeight;
                desc.depth = texDepth;
                desc.usage = texUsage;
                return desc;
            }

            /**
             * @brief 创建立方体纹理描述
             * @param texSize 纹理大小
             * @param texFormat 像素格式
             * @param texUsage 纹理用途
             * @return 纹理创建描述
             */
            static TextureCreateDesc TextureCube(
                uint32_t texSize,
                PixelFormat texFormat = PixelFormat::RGBA8_UNORM,
                TextureUsageFlags texUsage = TextureUsageFlagBits::Sampled | TextureUsageFlagBits::TransferDst)
            {
                TextureCreateDesc desc;
                desc.type = TextureType::Texture2D;
                desc.format = texFormat;
                desc.width = texSize;
                desc.height = texSize;
                desc.arrayLayers = 6;
                desc.cubemap = true;
                desc.usage = texUsage;
                return desc;
            }

            /**
             * @brief 创建渲染目标描述
             * @param texWidth 宽度
             * @param texHeight 高度
             * @param texFormat 像素格式
             * @return 纹理创建描述
             */
            static TextureCreateDesc RenderTarget(
                uint32_t texWidth,
                uint32_t texHeight,
                PixelFormat texFormat = PixelFormat::RGBA8_UNORM)
            {
                TextureCreateDesc desc;
                desc.type = TextureType::Texture2D;
                desc.format = texFormat;
                desc.width = texWidth;
                desc.height = texHeight;
                desc.usage = TextureUsageFlagBits::RenderTarget | TextureUsageFlagBits::Sampled;
                desc.initialState = ResourceState::RenderTarget;
                return desc;
            }

            /**
             * @brief 创建深度模板纹理描述
             * @param texWidth 宽度
             * @param texHeight 高度
             * @param texFormat 像素格式
             * @return 纹理创建描述
             */
            static TextureCreateDesc DepthStencil(
                uint32_t texWidth,
                uint32_t texHeight,
                PixelFormat texFormat = PixelFormat::D24_UNORM_S8_UINT)
            {
                TextureCreateDesc desc;
                desc.type = TextureType::Texture2D;
                desc.format = texFormat;
                desc.width = texWidth;
                desc.height = texHeight;
                desc.usage = TextureUsageFlagBits::DepthStencil | TextureUsageFlagBits::Sampled;
                desc.initialState = ResourceState::DepthStencil;
                return desc;
            }
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_ITEXTURE_H