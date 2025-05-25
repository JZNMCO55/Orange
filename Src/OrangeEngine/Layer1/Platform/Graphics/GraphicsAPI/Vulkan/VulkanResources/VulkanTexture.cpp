/**
 * @file VulkanTexture.cpp
 * @brief Vulkan纹理实现
 */

#include "VulkanTexture.h"
#include "../VulkanInterface/VulkanDevice.h"
#include <iostream>
#include <algorithm>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            VulkanTexture::VulkanTexture(VulkanDevice *device)
                : m_device(device)
            {
            }

            VulkanTexture::~VulkanTexture()
            {
                Shutdown();
            }

            IRenderTextureView *VulkanTexture::CreateView(const TextureViewCreateInfo &createInfo)
            {
                auto view = std::make_unique<VulkanTextureView>(m_device, this);
                if (!view->Initialize(createInfo))
                {
                    return nullptr;
                }
                return view.release();
            }

            IRenderTextureView* VulkanTexture::GetDefaultView() const
            {
                return m_defaultView.get(); 
            }

            IRenderDevice *VulkanTexture::GetDevice() const
            {
                return reinterpret_cast<IRenderDevice *>(m_device);
            }

            bool VulkanTexture::Initialize(const TextureCreateInfo &createInfo)
            {
                m_type = createInfo.type;
                m_format = createInfo.format;
                m_width = createInfo.extent.width;
                m_height = createInfo.extent.height;
                m_depth = createInfo.extent.depth;
                m_mipLevels = createInfo.mipLevels;
                m_arrayLayers = createInfo.arrayLayers;
                m_isCubemap = createInfo.cubemap;
                m_isRenderTarget = createInfo.renderTarget;
                m_isDepthStencil = createInfo.depthStencil;
                m_sampleCount = 1; // 暂时不支持多重采样

                // 转换格式
                m_vkFormat = ToVulkanFormat(m_format);
                if (m_vkFormat == VK_FORMAT_UNDEFINED)
                {
                    std::cerr << "VulkanTexture::Initialize: 不支持的像素格式" << std::endl;
                    return false;
                }

                // 创建图像
                if (!CreateImage())
                {
                    std::cerr << "VulkanTexture::Initialize: 创建图像失败" << std::endl;
                    return false;
                }

                // 分配内存
                if (!AllocateMemory())
                {
                    std::cerr << "VulkanTexture::Initialize: 分配内存失败" << std::endl;
                    return false;
                }

                // 创建图像视图
                if (!CreateImageView())
                {
                    std::cerr << "VulkanTexture::Initialize: 创建图像视图失败" << std::endl;
                    return false;
                }

                // 创建默认视图
                TextureViewCreateInfo defaultViewInfo{};
                defaultViewInfo.viewType = (m_type == TextureType::TextureCube) ? TextureViewType::ViewCube : (m_type == TextureType::Texture1D) ? TextureViewType::View1D
                                                                                                          : (m_type == TextureType::Texture3D)   ? TextureViewType::View3D
                                                                                                                                                 : TextureViewType::View2D;
                defaultViewInfo.format = PixelFormat::UNKNOWN; // 使用纹理格式
                defaultViewInfo.baseMipLevel = 0;
                defaultViewInfo.levelCount = m_mipLevels;
                defaultViewInfo.baseArrayLayer = 0;
                defaultViewInfo.layerCount = m_arrayLayers;

                m_defaultView = std::make_unique<VulkanTextureView>(m_device, this);
                if (!m_defaultView->Initialize(defaultViewInfo))
                {
                    std::cerr << "VulkanTexture::Initialize: 创建默认视图失败" << std::endl;
                    return false;
                }

                return true;
            }

            bool VulkanTexture::InitializeFromExisting(VkImage image, VkFormat format, uint32_t width, uint32_t height,
                                                       uint32_t depth, uint32_t mipLevels, uint32_t arrayLayers)
            {
                m_image = image;
                m_vkFormat = format;
                m_width = width;
                m_height = height;
                m_depth = depth;
                m_mipLevels = mipLevels;
                m_arrayLayers = arrayLayers;
                m_ownsImage = false; // 不拥有图像，不需要销毁

                // 转换格式
                switch (format)
                {
                case VK_FORMAT_B8G8R8A8_UNORM:
                    m_format = PixelFormat::BGRA8_UNORM;
                    break;
                case VK_FORMAT_B8G8R8A8_SRGB:
                    m_format = PixelFormat::BGRA8_SRGB;
                    break;
                case VK_FORMAT_R8G8B8A8_UNORM:
                    m_format = PixelFormat::RGBA8_UNORM;
                    break;
                case VK_FORMAT_R8G8B8A8_SRGB:
                    m_format = PixelFormat::RGBA8_SRGB;
                    break;
                default:
                    m_format = PixelFormat::UNKNOWN;
                    break;
                }

                // 创建图像视图
                if (!CreateImageView())
                {
                    std::cerr << "VulkanTexture::InitializeFromExisting: 创建图像视图失败" << std::endl;
                    return false;
                }

                // 创建默认视图
                TextureViewCreateInfo defaultViewInfo{};
                defaultViewInfo.viewType = TextureViewType::View2D;
                defaultViewInfo.format = PixelFormat::UNKNOWN;
                defaultViewInfo.baseMipLevel = 0;
                defaultViewInfo.levelCount = m_mipLevels;
                defaultViewInfo.baseArrayLayer = 0;
                defaultViewInfo.layerCount = m_arrayLayers;

                m_defaultView = std::make_unique<VulkanTextureView>(m_device, this);
                if (!m_defaultView->Initialize(defaultViewInfo))
                {
                    std::cerr << "VulkanTexture::InitializeFromExisting: 创建默认视图失败" << std::endl;
                    return false;
                }

                return true;
            }

            void VulkanTexture::Shutdown()
            {
                m_defaultView.reset();

                if (m_imageView != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(m_device->GetVkDevice(), m_imageView, nullptr);
                    m_imageView = VK_NULL_HANDLE;
                }

                if (m_ownsImage)
                {
                    if (m_memory != VK_NULL_HANDLE)
                    {
                        vkFreeMemory(m_device->GetVkDevice(), m_memory, nullptr);
                        m_memory = VK_NULL_HANDLE;
                    }

                    if (m_image != VK_NULL_HANDLE)
                    {
                        vkDestroyImage(m_device->GetVkDevice(), m_image, nullptr);
                        m_image = VK_NULL_HANDLE;
                    }
                }
            }

            bool VulkanTexture::UpdateData(const void *data, uint32_t mipLevel, uint32_t arrayLayer)
            {
                // 暂时简单实现，后续可以优化
                // 这里需要创建暂存缓冲区并复制数据
                std::cout << "VulkanTexture::UpdateData: 暂未实现" << std::endl;
                return false;
            }

            bool VulkanTexture::GenerateMipmaps()
            {
                // 暂时简单实现，后续可以优化
                std::cout << "VulkanTexture::GenerateMipmaps: 暂未实现" << std::endl;
                return false;
            }

            bool VulkanTexture::CreateImage()
            {
                VkImageCreateInfo imageInfo{};
                imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                imageInfo.imageType = GetVkImageType();
                imageInfo.extent.width = m_width;
                imageInfo.extent.height = m_height;
                imageInfo.extent.depth = m_depth;
                imageInfo.mipLevels = m_mipLevels;
                imageInfo.arrayLayers = m_arrayLayers;
                imageInfo.format = m_vkFormat;
                imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                imageInfo.usage = GetVkImageUsage();
                imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
                imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

                if (m_isCubemap)
                {
                    imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
                }

                if (vkCreateImage(m_device->GetVkDevice(), &imageInfo, nullptr, &m_image) != VK_SUCCESS)
                {
                    return false;
                }

                return true;
            }

            bool VulkanTexture::CreateImageView()
            {
                VkImageViewCreateInfo viewInfo{};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = m_image;
                viewInfo.viewType = m_isCubemap ? VK_IMAGE_VIEW_TYPE_CUBE : (m_type == TextureType::Texture1D) ? VK_IMAGE_VIEW_TYPE_1D
                                                                        : (m_type == TextureType::Texture3D)   ? VK_IMAGE_VIEW_TYPE_3D
                                                                                                               : VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = m_vkFormat;
                viewInfo.subresourceRange.aspectMask = GetVkImageAspect();
                viewInfo.subresourceRange.baseMipLevel = 0;
                viewInfo.subresourceRange.levelCount = m_mipLevels;
                viewInfo.subresourceRange.baseArrayLayer = 0;
                viewInfo.subresourceRange.layerCount = m_arrayLayers;

                if (vkCreateImageView(m_device->GetVkDevice(), &viewInfo, nullptr, &m_imageView) != VK_SUCCESS)
                {
                    return false;
                }

                return true;
            }

            bool VulkanTexture::AllocateMemory()
            {
                VkMemoryRequirements memRequirements;
                vkGetImageMemoryRequirements(m_device->GetVkDevice(), m_image, &memRequirements);

                VkMemoryAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                allocInfo.allocationSize = memRequirements.size;
                allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

                if (vkAllocateMemory(m_device->GetVkDevice(), &allocInfo, nullptr, &m_memory) != VK_SUCCESS)
                {
                    return false;
                }

                vkBindImageMemory(m_device->GetVkDevice(), m_image, m_memory, 0);
                return true;
            }

            VkImageType VulkanTexture::GetVkImageType() const
            {
                switch (m_type)
                {
                case TextureType::Texture1D:
                case TextureType::Texture1DArray:
                    return VK_IMAGE_TYPE_1D;
                case TextureType::Texture2D:
                case TextureType::Texture2DArray:
                case TextureType::TextureCube:
                case TextureType::TextureCubeArray:
                    return VK_IMAGE_TYPE_2D;
                case TextureType::Texture3D:
                    return VK_IMAGE_TYPE_3D;
                default:
                    return VK_IMAGE_TYPE_2D;
                }
            }

            VkImageUsageFlags VulkanTexture::GetVkImageUsage() const
            {
                VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

                if (m_isRenderTarget)
                {
                    if (m_isDepthStencil)
                    {
                        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                    }
                    else
                    {
                        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                    }
                }

                return usage;
            }

            VkImageAspectFlags VulkanTexture::GetVkImageAspect() const
            {
                if (m_isDepthStencil)
                {
                    VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;

                    // 检查是否有模板组件
                    if (m_vkFormat == VK_FORMAT_D24_UNORM_S8_UINT || m_vkFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
                    {
                        aspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
                    }

                    return aspectFlags;
                }
                else
                {
                    return VK_IMAGE_ASPECT_COLOR_BIT;
                }
            }

            uint32_t VulkanTexture::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
            {
                VkPhysicalDeviceMemoryProperties memProperties;
                vkGetPhysicalDeviceMemoryProperties(m_device->GetVkPhysicalDevice(), &memProperties);

                for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
                {
                    if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
                    {
                        return i;
                    }
                }

                return 0; // 应该抛出异常
            }

            void VulkanTexture::TransitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout)
            {
                VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = oldLayout;
                barrier.newLayout = newLayout;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = m_image;
                barrier.subresourceRange.aspectMask = GetVkImageAspect();
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = m_mipLevels;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = m_arrayLayers;

                VkPipelineStageFlags sourceStage;
                VkPipelineStageFlags destinationStage;

                if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
                {
                    barrier.srcAccessMask = 0;
                    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

                    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                }
                else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                {
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                }
                else
                {
                    sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                    destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                }

                vkCmdPipelineBarrier(
                    commandBuffer,
                    sourceStage, destinationStage,
                    0,
                    0, nullptr,
                    0, nullptr,
                    1, &barrier);

                EndSingleTimeCommands(commandBuffer);
            }

            VkCommandBuffer VulkanTexture::BeginSingleTimeCommands()
            {
                VkCommandBufferAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocInfo.commandPool = m_device->GetCommandPool();
                allocInfo.commandBufferCount = 1;

                VkCommandBuffer commandBuffer;
                vkAllocateCommandBuffers(m_device->GetVkDevice(), &allocInfo, &commandBuffer);

                VkCommandBufferBeginInfo beginInfo{};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

                vkBeginCommandBuffer(commandBuffer, &beginInfo);

                return commandBuffer;
            }

            void VulkanTexture::EndSingleTimeCommands(VkCommandBuffer commandBuffer)
            {
                vkEndCommandBuffer(commandBuffer);

                VkSubmitInfo submitInfo{};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &commandBuffer;

                vkQueueSubmit(m_device->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
                vkQueueWaitIdle(m_device->GetGraphicsQueue());

                vkFreeCommandBuffers(m_device->GetVkDevice(), m_device->GetCommandPool(), 1, &commandBuffer);
            }

            // VulkanTextureView 实现
            VulkanTextureView::VulkanTextureView(VulkanDevice *device, VulkanTexture *texture)
                : m_device(device), m_texture(texture)
            {
            }

            VulkanTextureView::~VulkanTextureView()
            {
                Shutdown();
            }

            IRenderDevice *VulkanTextureView::GetDevice() const
            {
                return reinterpret_cast<IRenderDevice *>(m_device);
            }

            bool VulkanTextureView::Initialize(const TextureViewCreateInfo &createInfo)
            {
                m_viewType = createInfo.viewType;
                m_format = (createInfo.format == PixelFormat::UNKNOWN) ? m_texture->GetFormat() : createInfo.format;
                m_baseMipLevel = createInfo.baseMipLevel;
                m_levelCount = createInfo.levelCount;
                m_baseArrayLayer = createInfo.baseArrayLayer;
                m_layerCount = createInfo.layerCount;

                VkImageViewCreateInfo viewInfo{};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = m_texture->GetVkImage();
                viewInfo.viewType = GetVkImageViewType();
                viewInfo.format = m_texture->GetVkFormat();
                viewInfo.subresourceRange.aspectMask = GetVkImageAspect();
                viewInfo.subresourceRange.baseMipLevel = m_baseMipLevel;
                viewInfo.subresourceRange.levelCount = m_levelCount;
                viewInfo.subresourceRange.baseArrayLayer = m_baseArrayLayer;
                viewInfo.subresourceRange.layerCount = m_layerCount;

                if (vkCreateImageView(m_device->GetVkDevice(), &viewInfo, nullptr, &m_imageView) != VK_SUCCESS)
                {
                    return false;
                }

                return true;
            }

            void VulkanTextureView::Shutdown()
            {
                if (m_imageView != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(m_device->GetVkDevice(), m_imageView, nullptr);
                    m_imageView = VK_NULL_HANDLE;
                }
            }

            VkImageViewType VulkanTextureView::GetVkImageViewType() const
            {
                switch (m_viewType)
                {
                case TextureViewType::View1D:
                    return VK_IMAGE_VIEW_TYPE_1D;
                case TextureViewType::View2D:
                    return VK_IMAGE_VIEW_TYPE_2D;
                case TextureViewType::View3D:
                    return VK_IMAGE_VIEW_TYPE_3D;
                case TextureViewType::ViewCube:
                    return VK_IMAGE_VIEW_TYPE_CUBE;
                case TextureViewType::View1DArray:
                    return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
                case TextureViewType::View2DArray:
                    return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                case TextureViewType::ViewCubeArray:
                    return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
                default:
                    return VK_IMAGE_VIEW_TYPE_2D;
                }
            }

            VkImageAspectFlags VulkanTextureView::GetVkImageAspect() const
            {
                return m_texture->GetVkImageAspect();
            }

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange