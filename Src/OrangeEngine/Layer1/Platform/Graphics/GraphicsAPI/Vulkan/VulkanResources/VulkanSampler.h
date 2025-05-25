/**
 * @file VulkanSampler.h
 * @brief Vulkan采样器实现
 */

#ifndef ORANGE_VULKAN_SAMPLER_H
#define ORANGE_VULKAN_SAMPLER_H

#include "../../../GraphicsInterface/RenderInterface/IRenderResources.h"
#include "../VulkanCommon/VulkanCommon.h"

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            class VulkanDevice;

            /**
             * @brief Vulkan采样器实现
             */
            class VulkanSampler : public IRenderSampler
            {
            public:
                VulkanSampler(VulkanDevice *device);
                virtual ~VulkanSampler();

                // IRenderSampler接口实现
                virtual TextureFilterMode GetMagFilter() const override { return m_magFilter; }
                virtual TextureFilterMode GetMinFilter() const override { return m_minFilter; }
                virtual MipmapFilterMode GetMipmapMode() const override { return m_mipmapMode; }
                virtual TextureAddressMode GetAddressModeU() const override { return m_addressModeU; }
                virtual TextureAddressMode GetAddressModeV() const override { return m_addressModeV; }
                virtual TextureAddressMode GetAddressModeW() const override { return m_addressModeW; }
                virtual float GetMipLodBias() const override { return m_mipLodBias; }
                virtual bool IsAnisotropyEnabled() const override { return m_anisotropyEnable; }
                virtual float GetMaxAnisotropy() const override { return m_maxAnisotropy; }
                virtual bool IsCompareEnabled() const override { return m_compareEnable; }
                virtual CompareOp GetCompareOp() const override { return m_compareOp; }
                virtual float GetMinLod() const override { return m_minLod; }
                virtual float GetMaxLod() const override { return m_maxLod; }
                virtual Color4f GetBorderColor() const override { return m_borderColor; }
                virtual IRenderDevice* GetDevice() const override;
                virtual void *GetNativeSampler() const override { return (void *)m_sampler; }

                // Vulkan特定方法
                VkSampler GetVkSampler() const { return m_sampler; }

                // 初始化方法
                bool Initialize(const SamplerCreateInfo &createInfo);
                void Shutdown();

            private:
                VulkanDevice *m_device;
                VkSampler m_sampler = VK_NULL_HANDLE;

                TextureFilterMode m_magFilter = TextureFilterMode::Linear;
                TextureFilterMode m_minFilter = TextureFilterMode::Linear;
                MipmapFilterMode m_mipmapMode = MipmapFilterMode::Linear;
                TextureAddressMode m_addressModeU = TextureAddressMode::Repeat;
                TextureAddressMode m_addressModeV = TextureAddressMode::Repeat;
                TextureAddressMode m_addressModeW = TextureAddressMode::Repeat;
                float m_mipLodBias = 0.0f;
                bool m_anisotropyEnable = false;
                float m_maxAnisotropy = 1.0f;
                bool m_compareEnable = false;
                CompareOp m_compareOp = CompareOp::Never;
                float m_minLod = 0.0f;
                float m_maxLod = 1000.0f;
                Color4f m_borderColor = {0.0f, 0.0f, 0.0f, 0.0f};

                // 工具方法
                VkFilter ConvertTextureFilter(TextureFilterMode filter) const;
                VkSamplerMipmapMode ConvertMipmapFilter(MipmapFilterMode filter) const;
                VkSamplerAddressMode ConvertAddressMode(TextureAddressMode mode) const;
                VkCompareOp ConvertCompareOp(CompareOp op) const;
                VkBorderColor ConvertBorderColor(const Color4f &color) const;
            };

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_SAMPLER_H