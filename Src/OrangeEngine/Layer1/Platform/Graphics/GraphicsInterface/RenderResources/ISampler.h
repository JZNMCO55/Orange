/**
 * @file ISampler.h
 * @brief 采样器接口定义
 */

#ifndef ORANGE_ISAMPLER_H
#define ORANGE_ISAMPLER_H

#include "../RenderCommon/RenderCommon.h"

namespace Orange
{
    namespace Graphics
    {

        // 前向声明
        class IRenderDevice;

        /**
         * @brief 采样器接口
         *
         * 采样器定义了如何从纹理中采样，如过滤模式、寻址模式等。
         */
        class ISampler
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~ISampler() = default;

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

            /**
             * @brief 获取绑定信息
             * @return 资源绑定描述
             */
            virtual ResourceBindingDesc GetBindingDesc() const = 0;
        };

        /**
         * @brief 创建采样器描述
         */
        struct SamplerCreateDesc
        {
            TextureFilterMode magFilter = TextureFilterMode::Linear;      ///< 放大过滤模式
            TextureFilterMode minFilter = TextureFilterMode::Linear;      ///< 缩小过滤模式
            MipmapFilterMode mipmapMode = MipmapFilterMode::Linear;       ///< Mipmap过滤模式
            TextureAddressMode addressModeU = TextureAddressMode::Repeat; ///< U寻址模式
            TextureAddressMode addressModeV = TextureAddressMode::Repeat; ///< V寻址模式
            TextureAddressMode addressModeW = TextureAddressMode::Repeat; ///< W寻址模式
            float mipLodBias = 0.0f;                                      ///< Mip LOD偏移
            bool anisotropyEnable = false;                                ///< 是否启用各向异性过滤
            float maxAnisotropy = 1.0f;                                   ///< 最大各向异性
            bool compareEnable = false;                                   ///< 是否启用比较
            CompareOp compareOp = CompareOp::Never;                       ///< 比较操作
            float minLod = 0.0f;                                          ///< 最小LOD
            float maxLod = 1000.0f;                                       ///< 最大LOD
            BorderColor borderColor = BorderColor::FloatOpaqueBlack;      ///< 边框颜色
            bool unnormalizedCoordinates = false;                         ///< 是否使用非归一化坐标
            const char *debugName = nullptr;                              ///< 调试名称

            /**
             * @brief 创建默认的线性过滤采样器描述
             * @return 采样器创建描述
             */
            static SamplerCreateDesc Linear()
            {
                SamplerCreateDesc desc;
                desc.magFilter = TextureFilterMode::Linear;
                desc.minFilter = TextureFilterMode::Linear;
                desc.mipmapMode = MipmapFilterMode::Linear;
                desc.addressModeU = TextureAddressMode::Repeat;
                desc.addressModeV = TextureAddressMode::Repeat;
                desc.addressModeW = TextureAddressMode::Repeat;
                return desc;
            }

            /**
             * @brief 创建最近点过滤采样器描述
             * @return 采样器创建描述
             */
            static SamplerCreateDesc Nearest()
            {
                SamplerCreateDesc desc;
                desc.magFilter = TextureFilterMode::Nearest;
                desc.minFilter = TextureFilterMode::Nearest;
                desc.mipmapMode = MipmapFilterMode::Nearest;
                desc.addressModeU = TextureAddressMode::Repeat;
                desc.addressModeV = TextureAddressMode::Repeat;
                desc.addressModeW = TextureAddressMode::Repeat;
                return desc;
            }

            /**
             * @brief 创建带各向异性过滤的采样器描述
             * @param maxAniso 最大各向异性值
             * @return 采样器创建描述
             */
            static SamplerCreateDesc Anisotropic(float maxAniso = 16.0f)
            {
                SamplerCreateDesc desc;
                desc.magFilter = TextureFilterMode::Linear;
                desc.minFilter = TextureFilterMode::Linear;
                desc.mipmapMode = MipmapFilterMode::Linear;
                desc.addressModeU = TextureAddressMode::Repeat;
                desc.addressModeV = TextureAddressMode::Repeat;
                desc.addressModeW = TextureAddressMode::Repeat;
                desc.anisotropyEnable = true;
                desc.maxAnisotropy = maxAniso;
                return desc;
            }

            /**
             * @brief 创建带比较功能的采样器描述（用于阴影贴图）
             * @param op 比较操作
             * @return 采样器创建描述
             */
            static SamplerCreateDesc Shadow(CompareOp op = CompareOp::Less)
            {
                SamplerCreateDesc desc;
                desc.magFilter = TextureFilterMode::Linear;
                desc.minFilter = TextureFilterMode::Linear;
                desc.mipmapMode = MipmapFilterMode::Linear;
                desc.addressModeU = TextureAddressMode::ClampToEdge;
                desc.addressModeV = TextureAddressMode::ClampToEdge;
                desc.addressModeW = TextureAddressMode::ClampToEdge;
                desc.compareEnable = true;
                desc.compareOp = op;
                return desc;
            }

            /**
             * @brief 创建带边框颜色的采样器描述
             * @param borderCol 边框颜色
             * @return 采样器创建描述
             */
            static SamplerCreateDesc WithBorder(BorderColor borderCol)
            {
                SamplerCreateDesc desc;
                desc.magFilter = TextureFilterMode::Linear;
                desc.minFilter = TextureFilterMode::Linear;
                desc.mipmapMode = MipmapFilterMode::Linear;
                desc.addressModeU = TextureAddressMode::ClampToBorder;
                desc.addressModeV = TextureAddressMode::ClampToBorder;
                desc.addressModeW = TextureAddressMode::ClampToBorder;
                desc.borderColor = borderCol;
                return desc;
            }
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_ISAMPLER_H