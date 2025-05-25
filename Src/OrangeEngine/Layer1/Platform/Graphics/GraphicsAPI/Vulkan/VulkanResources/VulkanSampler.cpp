/**
 * @file VulkanSampler.cpp
 * @brief Vulkan采样器实现
 */

#include "VulkanSampler.h"
#include "../VulkanInterface/VulkanDevice.h"
#include <iostream>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            VulkanSampler::VulkanSampler(VulkanDevice *device)
                : m_device(device)
            {
            }

            VulkanSampler::~VulkanSampler()
            {
                Shutdown();
            }

            IRenderDevice* VulkanSampler::GetDevice() const
            {
                return m_device;
            }

            bool VulkanSampler::Initialize(const SamplerCreateInfo &createInfo)
            {
                m_magFilter = createInfo.magFilter;
                m_minFilter = createInfo.minFilter;
                m_mipmapMode = createInfo.mipmapMode;
                m_addressModeU = createInfo.addressModeU;
                m_addressModeV = createInfo.addressModeV;
                m_addressModeW = createInfo.addressModeW;
                m_mipLodBias = createInfo.mipLodBias;
                m_anisotropyEnable = createInfo.anisotropyEnable;
                m_maxAnisotropy = createInfo.maxAnisotropy;
                m_compareEnable = createInfo.compareEnable;
                m_compareOp = createInfo.compareOp;
                m_minLod = createInfo.minLod;
                m_maxLod = createInfo.maxLod;
                m_borderColor = createInfo.borderColor;

                VkSamplerCreateInfo samplerInfo{};
                samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                samplerInfo.magFilter = ConvertTextureFilter(m_magFilter);
                samplerInfo.minFilter = ConvertTextureFilter(m_minFilter);
                samplerInfo.addressModeU = ConvertAddressMode(m_addressModeU);
                samplerInfo.addressModeV = ConvertAddressMode(m_addressModeV);
                samplerInfo.addressModeW = ConvertAddressMode(m_addressModeW);
                samplerInfo.anisotropyEnable = m_anisotropyEnable ? VK_TRUE : VK_FALSE;
                samplerInfo.maxAnisotropy = m_maxAnisotropy;
                samplerInfo.borderColor = ConvertBorderColor(m_borderColor);
                samplerInfo.unnormalizedCoordinates = VK_FALSE;
                samplerInfo.compareEnable = m_compareEnable ? VK_TRUE : VK_FALSE;
                samplerInfo.compareOp = ConvertCompareOp(m_compareOp);
                samplerInfo.mipmapMode = ConvertMipmapFilter(m_mipmapMode);
                samplerInfo.mipLodBias = m_mipLodBias;
                samplerInfo.minLod = m_minLod;
                samplerInfo.maxLod = m_maxLod;

                if (vkCreateSampler(m_device->GetVkDevice(), &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
                {
                    std::cerr << "VulkanSampler::Initialize: 创建采样器失败" << std::endl;
                    return false;
                }

                return true;
            }

            void VulkanSampler::Shutdown()
            {
                if (m_sampler != VK_NULL_HANDLE)
                {
                    vkDestroySampler(m_device->GetVkDevice(), m_sampler, nullptr);
                    m_sampler = VK_NULL_HANDLE;
                }
            }

            VkFilter VulkanSampler::ConvertTextureFilter(TextureFilterMode filter) const
            {
                switch (filter)
                {
                case TextureFilterMode::Nearest:
                    return VK_FILTER_NEAREST;
                case TextureFilterMode::Linear:
                    return VK_FILTER_LINEAR;
                case TextureFilterMode::Cubic:
                    return VK_FILTER_CUBIC_IMG; // 需要扩展支持
                default:
                    return VK_FILTER_LINEAR;
                }
            }

            VkSamplerMipmapMode VulkanSampler::ConvertMipmapFilter(MipmapFilterMode filter) const
            {
                switch (filter)
                {
                case MipmapFilterMode::Nearest:
                    return VK_SAMPLER_MIPMAP_MODE_NEAREST;
                case MipmapFilterMode::Linear:
                    return VK_SAMPLER_MIPMAP_MODE_LINEAR;
                default:
                    return VK_SAMPLER_MIPMAP_MODE_LINEAR;
                }
            }

            VkSamplerAddressMode VulkanSampler::ConvertAddressMode(TextureAddressMode mode) const
            {
                switch (mode)
                {
                case TextureAddressMode::Repeat:
                    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
                case TextureAddressMode::MirroredRepeat:
                    return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                case TextureAddressMode::ClampToEdge:
                    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                case TextureAddressMode::ClampToBorder:
                    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
                case TextureAddressMode::MirrorOnce:
                    return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
                default:
                    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
                }
            }

            VkCompareOp VulkanSampler::ConvertCompareOp(CompareOp op) const
            {
                switch (op)
                {
                case CompareOp::Never:
                    return VK_COMPARE_OP_NEVER;
                case CompareOp::Less:
                    return VK_COMPARE_OP_LESS;
                case CompareOp::Equal:
                    return VK_COMPARE_OP_EQUAL;
                case CompareOp::LessOrEqual:
                    return VK_COMPARE_OP_LESS_OR_EQUAL;
                case CompareOp::Greater:
                    return VK_COMPARE_OP_GREATER;
                case CompareOp::NotEqual:
                    return VK_COMPARE_OP_NOT_EQUAL;
                case CompareOp::GreaterOrEqual:
                    return VK_COMPARE_OP_GREATER_OR_EQUAL;
                case CompareOp::Always:
                    return VK_COMPARE_OP_ALWAYS;
                default:
                    return VK_COMPARE_OP_NEVER;
                }
            }

            VkBorderColor VulkanSampler::ConvertBorderColor(const Color4f &color) const
            {
                // 简化实现，只支持几种常见的边框颜色
                if (color.r == 0.0f && color.g == 0.0f && color.b == 0.0f && color.a == 0.0f)
                {
                    return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
                }
                else if (color.r == 0.0f && color.g == 0.0f && color.b == 0.0f && color.a == 1.0f)
                {
                    return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
                }
                else if (color.r == 1.0f && color.g == 1.0f && color.b == 1.0f && color.a == 1.0f)
                {
                    return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
                }
                else
                {
                    // 默认使用透明黑色
                    return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
                }
            }

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange