/**
 * @file VulkanTexture.h
 * @brief Vulkan纹理实现
 */

#ifndef ORANGE_VULKAN_TEXTURE_H
#define ORANGE_VULKAN_TEXTURE_H

#include "../../../GraphicsInterface/RenderInterface/IRenderResources.h"
#include "../VulkanCommon/VulkanCommon.h"
#include <memory>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            class VulkanDevice;
            class VulkanTextureView;

            /**
             * @brief Vulkan纹理实现
             */
            class VulkanTexture : public IRenderTexture
            {
            public:
                VulkanTexture(VulkanDevice *device);
                virtual ~VulkanTexture();

                // IRenderTexture接口实现
                virtual TextureType GetType() const override { return m_type; }
                virtual PixelFormat GetFormat() const override { return m_format; }
                virtual uint32_t GetWidth() const override { return m_width; }
                virtual uint32_t GetHeight() const override { return m_height; }
                virtual uint32_t GetDepth() const override { return m_depth; }
                virtual uint32_t GetMipLevels() const override { return m_mipLevels; }
                virtual uint32_t GetArrayLayers() const override { return m_arrayLayers; }
                virtual uint32_t GetSampleCount() const override { return m_sampleCount; }
                virtual bool IsCubemap() const override { return m_isCubemap; }
                virtual bool IsRenderTarget() const override { return m_isRenderTarget; }
                virtual bool IsDepthStencil() const override { return m_isDepthStencil; }
                virtual IRenderTextureView *CreateView(const TextureViewCreateInfo &createInfo) override;
                virtual IRenderTextureView* GetDefaultView() const override;
                virtual IRenderDevice *GetDevice() const override;
                virtual void *GetNativeTexture() const override { return (void *)m_image; }

                // Vulkan特定方法
                VkImage GetVkImage() const { return m_image; }
                VkImageView GetVkImageView() const { return m_imageView; }
                VkDeviceMemory GetVkDeviceMemory() const { return m_memory; }
                VkFormat GetVkFormat() const { return m_vkFormat; }

                // 初始化方法
                bool Initialize(const TextureCreateInfo &createInfo);
                bool InitializeFromExisting(VkImage image, VkFormat format, uint32_t width, uint32_t height,
                                            uint32_t depth = 1, uint32_t mipLevels = 1, uint32_t arrayLayers = 1);
                void Shutdown();

                // 数据更新
                bool UpdateData(const void *data, uint32_t mipLevel = 0, uint32_t arrayLayer = 0);
                bool GenerateMipmaps();

                VkImageType GetVkImageType() const;
                VkImageAspectFlags GetVkImageAspect() const;

            private:
                // 工具方法
                bool CreateImage();
                bool CreateImageView();
                bool AllocateMemory();
                VkImageUsageFlags GetVkImageUsage() const;
                uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
                void TransitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout);
                VkCommandBuffer BeginSingleTimeCommands();
                void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

            private:
                VulkanDevice *m_device;
                VkImage m_image = VK_NULL_HANDLE;
                VkImageView m_imageView = VK_NULL_HANDLE;
                VkDeviceMemory m_memory = VK_NULL_HANDLE;

                TextureType m_type = TextureType::Texture2D;
                PixelFormat m_format = PixelFormat::RGBA8_UNORM;
                VkFormat m_vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
                uint32_t m_width = 0;
                uint32_t m_height = 0;
                uint32_t m_depth = 1;
                uint32_t m_mipLevels = 1;
                uint32_t m_arrayLayers = 1;
                uint32_t m_sampleCount = 1;
                bool m_isCubemap = false;
                bool m_isRenderTarget = false;
                bool m_isDepthStencil = false;
                bool m_ownsImage = true; // 是否拥有VkImage（用于交换链图像）

                std::unique_ptr<VulkanTextureView> m_defaultView;
            };

            /**
             * @brief Vulkan纹理视图实现
             */
            class VulkanTextureView : public IRenderTextureView
            {
            public:
                VulkanTextureView(VulkanDevice *device, VulkanTexture *texture);
                virtual ~VulkanTextureView();

                // IRenderTextureView接口实现
                virtual TextureViewType GetType() const override { return m_viewType; }
                virtual PixelFormat GetFormat() const override { return m_format; }
                virtual uint32_t GetBaseMipLevel() const override { return m_baseMipLevel; }
                virtual uint32_t GetLevelCount() const override { return m_levelCount; }
                virtual uint32_t GetBaseArrayLayer() const override { return m_baseArrayLayer; }
                virtual uint32_t GetLayerCount() const override { return m_layerCount; }
                virtual IRenderTexture *GetTexture() const override { return m_texture; }
                virtual IRenderDevice *GetDevice() const override;
                virtual void *GetNativeTextureView() const override { return (void *)m_imageView; }

                // Vulkan特定方法
                VkImageView GetVkImageView() const { return m_imageView; }

                // 初始化方法
                bool Initialize(const TextureViewCreateInfo &createInfo);
                void Shutdown();

            private:
                VulkanDevice *m_device;
                VulkanTexture *m_texture;
                VkImageView m_imageView = VK_NULL_HANDLE;

                TextureViewType m_viewType = TextureViewType::View2D;
                PixelFormat m_format = PixelFormat::UNKNOWN;
                uint32_t m_baseMipLevel = 0;
                uint32_t m_levelCount = 1;
                uint32_t m_baseArrayLayer = 0;
                uint32_t m_layerCount = 1;

                // 工具方法
                VkImageViewType GetVkImageViewType() const;
                VkImageAspectFlags GetVkImageAspect() const;
            };

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_TEXTURE_H