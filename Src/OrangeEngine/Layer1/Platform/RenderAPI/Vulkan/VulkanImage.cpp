/**
 * @file VulkanImage.cpp
 * @brief Vulkan图像系统实现
 * @details 实现Vulkan 2D图像的完整生命周期管理，包括：
 * - 图像创建和内存分配
 * - 图像视图管理（默认视图、Mip级别视图、层视图）
 * - 采样器创建和配置
 * - 图像布局转换和内存屏障
 * - 数据传输和读写操作
 *
 * 支持的图像用途：
 * - 纹理采样 (Texture)
 * - 渲染附件 (Attachment)
 * - 存储图像 (Storage)
 * - 主机读取 (HostRead)
 *
 * 支持的图像格式：
 * - 颜色格式：RGBA8、RGBA16F、RGBA32F等
 * - 深度格式：DEPTH32F、DEPTH24STENCIL8等
 * - 整数格式：R32_SINT、RG16_UINT等
 */
#include "orgpch.h"
#include "VulkanImage.h"

#include "VulkanAPI.h"
#include "VulkanContext.h"
// #include "VulkanRenderer.h"

#include <format>

namespace Orange
{

    // 全局图像引用映射，用于调试和资源跟踪
    static std::map<VkImage, WeakRef<VulkanImage2D>> s_ImageReferences;

    /**
     * @brief 构造Vulkan 2D图像
     * @param specification 图像规格说明
     * @details 验证图像尺寸有效性并存储规格信息
     * 实际的Vulkan资源创建在Invalidate()中进行
     */
    VulkanImage2D::VulkanImage2D(const ImageSpecification &specification)
        : m_Specification(specification)
    {
        // 验证图像尺寸必须大于0
        ORG_CORE_VERIFY(m_Specification.Width > 0 && m_Specification.Height > 0);
    }

    /**
     * @brief 析构函数
     * @details 确保所有Vulkan资源在对象销毁时被正确释放
     */
    VulkanImage2D::~VulkanImage2D()
    {
        Release();
    }

    /**
     * @brief 使图像无效并重新创建
     * @details 触发图像资源的重新创建，可以在主线程或渲染线程中调用
     * 目前直接在当前线程执行，未来可能改为异步执行
     */
    void VulkanImage2D::Invalidate()
    {
#if INVESTIGATE
        // 异步执行版本（目前被注释）
        Ref<VulkanImage2D> instance = this;
        Renderer::Submit([instance]() mutable
                         { instance->RT_Invalidate(); });
#endif

        // 直接在当前线程执行
        RT_Invalidate();
    }

    /**
     * @brief 释放图像资源
     * @details 安全地释放所有相关的Vulkan资源：
     * 1. 图像视图（默认视图、Mip级别视图、层视图）
     * 2. 采样器
     * 3. 图像和内存分配
     * 4. 从全局引用映射中移除
     *
     * 使用渲染线程确保在正确的Vulkan上下文中释放资源
     */
    void VulkanImage2D::Release()
    {
        if (m_Info.Image == nullptr)
            return;

        // 复制资源信息，避免在lambda中访问可能已销毁的对象
        const VulkanImageInfo &info = m_Info;
#ifdef TODO // From VulkanRenderer
        Renderer::SubmitResourceFree([info, mipViews = m_PerMipImageViews, layerViews = m_PerLayerImageViews]() mutable
                                     {
            const auto vulkanDevice = VulkanContext::GetCurrentDevice()->GetVulkanDevice();

            // 销毁默认图像视图
            vkDestroyImageView(vulkanDevice, info.ImageView, nullptr);

            // 销毁采样器
            Vulkan::DestroySampler(info.Sampler);

            // 销毁所有Mip级别视图
            for (auto& view : mipViews)
            {
                if (view.second)
                    vkDestroyImageView(vulkanDevice, view.second, nullptr);
            }

            // 销毁所有层视图
            for (auto& view : layerViews)
            {
                if (view)
                    vkDestroyImageView(vulkanDevice, view, nullptr);
            }

            // 释放图像和内存
            VulkanAllocator allocator("VulkanImage2D");
            allocator.DestroyImage(info.Image, info.MemoryAlloc);

            // 从全局引用映射中移除
            s_ImageReferences.erase(info.Image); });
#endif
        // 清空本地句柄，防止重复释放
        m_Info.Image = nullptr;
        m_Info.ImageView = nullptr;
        if (m_Specification.CreateSampler)
            m_Info.Sampler = nullptr;
        m_PerLayerImageViews.clear();
        m_PerMipImageViews.clear();
    }

    /**
     * @brief 获取最接近指定尺寸的Mip级别
     * @param width 目标宽度
     * @param height 目标高度
     * @return Mip级别索引
     * @details 计算哪个Mip级别最接近指定的尺寸
     * 用于LOD选择和纹理流送
     */
    int VulkanImage2D::GetClosestMipLevel(uint32_t width, uint32_t height) const
    {
        // 如果目标尺寸大于原始尺寸的一半，使用最高质量的Mip级别
        if (width > m_Specification.Width / 2 || height > m_Specification.Height / 2)
            return 0;

        // 计算基于尺寸的Mip级别差异
        int a = glm::log2(glm::min(m_Specification.Width, m_Specification.Height));
        int b = glm::log2(glm::min(width, height));
        return a - b;
    }

    /**
     * @brief 获取指定Mip级别的尺寸
     * @param mipLevel Mip级别
     * @return 该级别的宽度和高度
     * @details 每个Mip级别的尺寸是上一级别的一半
     */
    std::pair<uint32_t, uint32_t> VulkanImage2D::GetMipLevelSize(int mipLevel) const
    {
        uint32_t width = m_Specification.Width;
        uint32_t height = m_Specification.Height;
        return {width >> mipLevel, height >> mipLevel};
    }

    /**
     * @brief 在渲染线程中重新创建图像
     * @details 执行图像资源的完整创建流程：
     * 1. 释放现有资源
     * 2. 根据用途确定图像使用标志
     * 3. 创建Vulkan图像和分配内存
     * 4. 创建默认图像视图
     * 5. 创建采样器（如果需要）
     * 6. 执行必要的布局转换
     */
    void VulkanImage2D::RT_Invalidate()
    {
        ORG_CORE_VERIFY(m_Specification.Width > 0 && m_Specification.Height > 0);

        // 首先释放现有资源
        Release();

        VkDevice device = VulkanContext::GetCurrentDevice()->GetVulkanDevice();
        VulkanAllocator allocator("Image2D");

        // 确定图像使用标志
        VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT; // 默认支持采样

        // 根据用途添加相应的使用标志
        if (m_Specification.Usage == ImageUsage::Attachment)
        {
            // 渲染附件：根据格式选择深度或颜色附件
            if (Utils::IsDepthFormat(m_Specification.Format))
                usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            else
                usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }
        if (m_Specification.Transfer || m_Specification.Usage == ImageUsage::Texture)
        {
            // 纹理或需要传输：支持源和目标传输
            usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
        if (m_Specification.Usage == ImageUsage::Storage)
        {
            // 存储图像：支持存储操作和传输目标
            usage |= VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        // 确定图像方面掩码
        VkImageAspectFlags aspectMask = Utils::IsDepthFormat(m_Specification.Format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        if (m_Specification.Format == ImageFormat::DEPTH24STENCIL8)
            aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT; // 深度模板格式需要模板方面

        // 转换为Vulkan格式
        VkFormat vulkanFormat = Utils::VulkanImageFormat(m_Specification.Format);

        // 确定内存使用类型
        VmaMemoryUsage memoryUsage = m_Specification.Usage == ImageUsage::HostRead ? VMA_MEMORY_USAGE_GPU_TO_CPU : VMA_MEMORY_USAGE_GPU_ONLY;

        // 配置图像创建信息
        VkImageCreateInfo imageCreateInfo = {};
        imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;           // 2D图像
        imageCreateInfo.format = vulkanFormat;                  // 图像格式
        imageCreateInfo.extent.width = m_Specification.Width;   // 宽度
        imageCreateInfo.extent.height = m_Specification.Height; // 高度
        imageCreateInfo.extent.depth = 1;                       // 深度（2D图像为1）
        imageCreateInfo.mipLevels = m_Specification.Mips;       // Mip级别数
        imageCreateInfo.arrayLayers = m_Specification.Layers;   // 数组层数
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;        // 采样数（无多重采样）
        // 平铺模式：主机读取使用线性，其他使用最优
        imageCreateInfo.tiling = m_Specification.Usage == ImageUsage::HostRead ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.usage = usage; // 使用标志

        // 分配图像和内存
        m_Info.MemoryAlloc = allocator.AllocateImage(imageCreateInfo, memoryUsage, m_Info.Image, &m_GPUAllocationSize);
        s_ImageReferences[m_Info.Image] = this; // 添加到全局引用映射
        VKUtils::SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE, m_Specification.DebugName, m_Info.Image);

        // 创建默认图像视图
        VkImageViewCreateInfo imageViewCreateInfo = {};
        imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        // 视图类型：多层使用2D数组，单层使用2D
        imageViewCreateInfo.viewType = m_Specification.Layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.format = vulkanFormat;
        imageViewCreateInfo.flags = 0;
        imageViewCreateInfo.subresourceRange = {};
        imageViewCreateInfo.subresourceRange.aspectMask = aspectMask;             // 方面掩码
        imageViewCreateInfo.subresourceRange.baseMipLevel = 0;                    // 基础Mip级别
        imageViewCreateInfo.subresourceRange.levelCount = m_Specification.Mips;   // Mip级别数
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;                  // 基础数组层
        imageViewCreateInfo.subresourceRange.layerCount = m_Specification.Layers; // 数组层数
        imageViewCreateInfo.image = m_Info.Image;

        VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCreateInfo, nullptr, &m_Info.ImageView));
        VKUtils::SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE_VIEW,
                                         std::format("{} default image view", m_Specification.DebugName), m_Info.ImageView);

        // 创建采样器（如果需要）
        // TODO: 渲染器应该包含某种采样器缓存
        if (m_Specification.CreateSampler)
        {
            VkSamplerCreateInfo samplerCreateInfo = {};
            samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerCreateInfo.maxAnisotropy = 1.0f; // 无各向异性过滤

            // 根据格式选择过滤模式
            if (Utils::IsIntegerBased(m_Specification.Format))
            {
                // 整数格式使用最近邻过滤
                samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
                samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
                samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            }
            else
            {
                // 浮点格式使用线性过滤
                samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
                samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
                samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            }

            // 地址模式：夹紧到边缘
            samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerCreateInfo.addressModeV = samplerCreateInfo.addressModeU;
            samplerCreateInfo.addressModeW = samplerCreateInfo.addressModeU;
            samplerCreateInfo.mipLodBias = 0.0f;                                // Mip LOD偏移
            samplerCreateInfo.minLod = 0.0f;                                    // 最小LOD
            samplerCreateInfo.maxLod = 100.0f;                                  // 最大LOD
            samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // 边界颜色

            m_Info.Sampler = Vulkan::CreateSampler(samplerCreateInfo);
            VKUtils::SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_SAMPLER,
                                             std::format("{} default sampler", m_Specification.DebugName), m_Info.Sampler);
        }

        // 存储图像需要转换到GENERAL布局
        if (m_Specification.Usage == ImageUsage::Storage)
        {
            // 获取命令缓冲区进行布局转换
            VkCommandBuffer commandBuffer = VulkanContext::GetCurrentDevice()->GetCommandBuffer(true);

            // 配置子资源范围
            VkImageSubresourceRange subresourceRange = {};
            subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresourceRange.baseMipLevel = 0;
            subresourceRange.levelCount = m_Specification.Mips;
            subresourceRange.layerCount = m_Specification.Layers;
#ifdef TODO // From VulkanRenderer
            // 插入内存屏障，将图像从UNDEFINED转换到GENERAL布局
            Utils::InsertImageMemoryBarrier(commandBuffer, m_Info.Image,
                                            0, 0,
                                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                            subresourceRange);
#endif
            VulkanContext::GetCurrentDevice()->FlushCommandBuffer(commandBuffer);
        }
        else if (m_Specification.Usage == ImageUsage::HostRead)
        {
            // Transition image to TRANSFER_DST layout
            VkCommandBuffer commandBuffer = VulkanContext::GetCurrentDevice()->GetCommandBuffer(true);

            VkImageSubresourceRange subresourceRange = {};
            subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresourceRange.baseMipLevel = 0;
            subresourceRange.levelCount = m_Specification.Mips;
            subresourceRange.layerCount = m_Specification.Layers;
#ifdef TODO // From VulkanRenderer
            Utils::InsertImageMemoryBarrier(commandBuffer, m_Info.Image,
                                            0, 0,
                                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                            subresourceRange);
#endif
            VulkanContext::GetCurrentDevice()->FlushCommandBuffer(commandBuffer);
        }

        UpdateDescriptor();
    }

    void VulkanImage2D::CreatePerLayerImageViews()
    {
#ifdef TODO // From VulkanRenderer
        Ref<VulkanImage2D> instance = this;
        Renderer::Submit([instance]() mutable
                         { instance->RT_CreatePerLayerImageViews(); });
#endif
    }

    void VulkanImage2D::RT_CreatePerLayerImageViews()
    {
        ORG_CORE_ASSERT(m_Specification.Layers > 1);

        VkDevice device = VulkanContext::GetCurrentDevice()->GetVulkanDevice();

        VkImageAspectFlags aspectMask = Utils::IsDepthFormat(m_Specification.Format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        if (m_Specification.Format == ImageFormat::DEPTH24STENCIL8)
            aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

        const VkFormat vulkanFormat = Utils::VulkanImageFormat(m_Specification.Format);

        m_PerLayerImageViews.resize(m_Specification.Layers);
        for (uint32_t layer = 0; layer < m_Specification.Layers; layer++)
        {
            VkImageViewCreateInfo imageViewCreateInfo = {};
            imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            imageViewCreateInfo.format = vulkanFormat;
            imageViewCreateInfo.flags = 0;
            imageViewCreateInfo.subresourceRange = {};
            imageViewCreateInfo.subresourceRange.aspectMask = aspectMask;
            imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
            imageViewCreateInfo.subresourceRange.levelCount = m_Specification.Mips;
            imageViewCreateInfo.subresourceRange.baseArrayLayer = layer;
            imageViewCreateInfo.subresourceRange.layerCount = 1;
            imageViewCreateInfo.image = m_Info.Image;
            VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCreateInfo, nullptr, &m_PerLayerImageViews[layer]));
            VKUtils::SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE_VIEW, std::format("{} image view layer: {}", m_Specification.DebugName, layer), m_PerLayerImageViews[layer]);
        }
    }

    VkImageView VulkanImage2D::GetMipImageView(uint32_t mip)
    {
#ifdef TODO // From VulkanRenderer
        if (m_PerMipImageViews.find(mip) == m_PerMipImageViews.end())
        {
            Ref<VulkanImage2D> instance = this;
            Renderer::Submit([instance, mip]() mutable
                             { instance->RT_GetMipImageView(mip); });
            return nullptr;
        }
#endif

        return m_PerMipImageViews.at(mip);
    }

    VkImageView VulkanImage2D::RT_GetMipImageView(const uint32_t mip)
    {
        auto it = m_PerMipImageViews.find(mip);
        if (it != m_PerMipImageViews.end())
            return it->second;

        VkDevice device = VulkanContext::GetCurrentDevice()->GetVulkanDevice();

        VkImageAspectFlags aspectMask = Utils::IsDepthFormat(m_Specification.Format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        if (m_Specification.Format == ImageFormat::DEPTH24STENCIL8)
            aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

        VkFormat vulkanFormat = Utils::VulkanImageFormat(m_Specification.Format);

        VkImageViewCreateInfo imageViewCreateInfo = {};
        imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.format = vulkanFormat;
        imageViewCreateInfo.flags = 0;
        imageViewCreateInfo.subresourceRange = {};
        imageViewCreateInfo.subresourceRange.aspectMask = aspectMask;
        imageViewCreateInfo.subresourceRange.baseMipLevel = mip;
        imageViewCreateInfo.subresourceRange.levelCount = 1;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.layerCount = 1;
        imageViewCreateInfo.image = m_Info.Image;

        VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCreateInfo, nullptr, &m_PerMipImageViews[mip]));
        VKUtils::SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE_VIEW, std::format("{} image view mip: {}", m_Specification.DebugName, mip), m_PerMipImageViews[mip]);
        return m_PerMipImageViews.at(mip);
    }

    void VulkanImage2D::RT_CreatePerSpecificLayerImageViews(const std::vector<uint32_t> &layerIndices)
    {
        ORG_CORE_ASSERT(m_Specification.Layers > 1);

        VkDevice device = VulkanContext::GetCurrentDevice()->GetVulkanDevice();

        VkImageAspectFlags aspectMask = Utils::IsDepthFormat(m_Specification.Format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        if (m_Specification.Format == ImageFormat::DEPTH24STENCIL8)
            aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

        const VkFormat vulkanFormat = Utils::VulkanImageFormat(m_Specification.Format);

        // ORG_CORE_ASSERT(m_PerLayerImageViews.size() == m_Specification.Layers);
        if (m_PerLayerImageViews.empty())
            m_PerLayerImageViews.resize(m_Specification.Layers);

        for (uint32_t layer : layerIndices)
        {
            VkImageViewCreateInfo imageViewCreateInfo = {};
            imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            imageViewCreateInfo.format = vulkanFormat;
            imageViewCreateInfo.flags = 0;
            imageViewCreateInfo.subresourceRange = {};
            imageViewCreateInfo.subresourceRange.aspectMask = aspectMask;
            imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
            imageViewCreateInfo.subresourceRange.levelCount = m_Specification.Mips;
            imageViewCreateInfo.subresourceRange.baseArrayLayer = layer;
            imageViewCreateInfo.subresourceRange.layerCount = 1;
            imageViewCreateInfo.image = m_Info.Image;
            VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCreateInfo, nullptr, &m_PerLayerImageViews[layer]));
            VKUtils::SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE_VIEW, std::format("{} image view layer: {}", m_Specification.DebugName, layer), m_PerLayerImageViews[layer]);
        }
    }

    void VulkanImage2D::UpdateDescriptor()
    {
        if (m_Specification.Format == ImageFormat::DEPTH24STENCIL8 || m_Specification.Format == ImageFormat::DEPTH32F || m_Specification.Format == ImageFormat::DEPTH32FSTENCIL8UINT)
            m_DescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        else if (m_Specification.Usage == ImageUsage::Storage)
            m_DescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        else
            m_DescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        if (m_Specification.Usage == ImageUsage::Storage)
            m_DescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        else if (m_Specification.Usage == ImageUsage::HostRead)
            m_DescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        m_DescriptorImageInfo.imageView = m_Info.ImageView;
        m_DescriptorImageInfo.sampler = m_Info.Sampler;

        // ORG_CORE_WARN_TAG("Renderer", "VulkanImage2D::UpdateDescriptor to ImageView = {0}", (const void*)m_Info.ImageView);
    }

    const std::map<VkImage, WeakRef<VulkanImage2D>> &VulkanImage2D::GetImageRefs()
    {
        return s_ImageReferences;
    }

    void VulkanImage2D::SetData(Buffer buffer)
    {
        ORG_CORE_VERIFY(m_Specification.Transfer, "Image must be created with ImageSpecification::Transfer enabled!");

        if (buffer)
        {
            Ref<VulkanDevice> device = VulkanContext::GetCurrentDevice();

            VkDeviceSize size = buffer.Size;

            VkMemoryAllocateInfo memAllocInfo{};
            memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

            VulkanAllocator allocator("Image2D");

            // Create staging buffer
            VkBufferCreateInfo bufferCreateInfo{};
            bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreateInfo.size = size;
            bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VkBuffer stagingBuffer;
            VmaAllocation stagingBufferAllocation = allocator.AllocateBuffer(bufferCreateInfo, VMA_MEMORY_USAGE_CPU_TO_GPU, stagingBuffer);

            // Copy data to staging buffer
            uint8_t *destData = allocator.MapMemory<uint8_t>(stagingBufferAllocation);
            ORG_CORE_VERIFY(buffer.Data);
            memcpy(destData, buffer.Data, size);
            allocator.UnmapMemory(stagingBufferAllocation);

            VkCommandBuffer copyCmd = device->GetCommandBuffer(true);

            // Image memory barriers for the texture image

            // The sub resource range describes the regions of the image that will be transitioned using the memory barriers below
            VkImageSubresourceRange subresourceRange = {};
            // Image only contains color data
            subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            // Start at first mip level
            subresourceRange.baseMipLevel = 0;
            subresourceRange.levelCount = 1;
            subresourceRange.layerCount = 1;

            // Transition the texture image layout to transfer target, so we can safely copy our buffer data to it.
            VkImageMemoryBarrier imageMemoryBarrier{};
            imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageMemoryBarrier.image = m_Info.Image;
            imageMemoryBarrier.subresourceRange = subresourceRange;
            imageMemoryBarrier.srcAccessMask = 0;
            imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

            // Insert a memory dependency at the proper pipeline stages that will execute the image layout transition
            // Source pipeline stage is host write/read exection (VK_PIPELINE_STAGE_HOST_BIT)
            // Destination pipeline stage is copy command exection (VK_PIPELINE_STAGE_TRANSFER_BIT)
            vkCmdPipelineBarrier(
                copyCmd,
                VK_PIPELINE_STAGE_HOST_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &imageMemoryBarrier);

            VkBufferImageCopy bufferCopyRegion = {};
            bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            bufferCopyRegion.imageSubresource.mipLevel = 0;
            bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
            bufferCopyRegion.imageSubresource.layerCount = 1;
            bufferCopyRegion.imageExtent.width = m_Specification.Width;
            bufferCopyRegion.imageExtent.height = m_Specification.Height;
            bufferCopyRegion.imageExtent.depth = 1;
            bufferCopyRegion.bufferOffset = 0;

            // Copy mip levels from staging buffer
            vkCmdCopyBufferToImage(
                copyCmd,
                stagingBuffer,
                m_Info.Image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &bufferCopyRegion);

#if 0
            // Once the data has been uploaded we transfer to the texture image to the shader read layout, so it can be sampled from
            imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            // Insert a memory dependency at the proper pipeline stages that will execute the image layout transition 
            // Source pipeline stage stage is copy command exection (VK_PIPELINE_STAGE_TRANSFER_BIT)
            // Destination pipeline stage fragment shader access (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
            vkCmdPipelineBarrier(
                copyCmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &imageMemoryBarrier);

#endif

#ifdef TODO // From VulkanRenderer
            Utils::InsertImageMemoryBarrier(copyCmd, m_Info.Image,
                                            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_DescriptorImageInfo.imageLayout,
                                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                            subresourceRange);
#endif
            device->FlushCommandBuffer(copyCmd);

            // Clean up staging resources
            allocator.DestroyBuffer(stagingBuffer, stagingBufferAllocation);

            UpdateDescriptor();
        }
    }

    void VulkanImage2D::CopyToHostBuffer(Buffer &buffer) const
    {
        auto device = VulkanContext::GetCurrentDevice();
        auto vulkanDevice = device->GetVulkanDevice();
        VulkanAllocator allocator("Image2D");

        uint64_t bufferSize = m_Specification.Width * m_Specification.Height * Utils::GetImageFormatBPP(m_Specification.Format);

        // Create staging buffer
        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.size = bufferSize;
        bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

#if MEM_INFO
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(vulkanDevice, m_Info.Image, &memReqs);
        ORG_CORE_WARN("MemReq = {} ({})", memReqs.size, memReqs.alignment);
        ORG_CORE_WARN("Expected size = {}", bufferSize);
#endif

        VkBuffer stagingBuffer;
        VmaAllocation stagingBufferAllocation = allocator.AllocateBuffer(bufferCreateInfo, VMA_MEMORY_USAGE_GPU_TO_CPU, stagingBuffer);

        uint32_t mipCount = 1;
        uint32_t mipWidth = m_Specification.Width, mipHeight = m_Specification.Height;

        VkCommandBuffer copyCmd = device->GetCommandBuffer(true);

        VkImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = mipCount;
        subresourceRange.layerCount = 1;

#ifdef TODO // From VulkanRenderer
        Utils::InsertImageMemoryBarrier(copyCmd, m_Info.Image,
                                        VK_ACCESS_TRANSFER_READ_BIT, 0,
                                        m_DescriptorImageInfo.imageLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                        subresourceRange);
#endif

        uint64_t mipDataOffset = 0;
        for (uint32_t mip = 0; mip < mipCount; mip++)
        {
            VkBufferImageCopy bufferCopyRegion = {};
            bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            bufferCopyRegion.imageSubresource.mipLevel = mip;
            bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
            bufferCopyRegion.imageSubresource.layerCount = 1;
            bufferCopyRegion.imageExtent.width = mipWidth;
            bufferCopyRegion.imageExtent.height = mipHeight;
            bufferCopyRegion.imageExtent.depth = 1;
            bufferCopyRegion.bufferOffset = mipDataOffset;

            vkCmdCopyImageToBuffer(
                copyCmd,
                m_Info.Image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                stagingBuffer,
                1,
                &bufferCopyRegion);

            uint64_t mipDataSize = mipWidth * mipHeight * sizeof(float) * 4 * 6;
            mipDataOffset += mipDataSize;
            mipWidth /= 2;
            mipHeight /= 2;
        }

#ifdef TODO // From VulkanRenderer
        Utils::InsertImageMemoryBarrier(copyCmd, m_Info.Image,
                                        VK_ACCESS_TRANSFER_READ_BIT, 0,
                                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_DescriptorImageInfo.imageLayout,
                                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                        subresourceRange);
#endif
        device->FlushCommandBuffer(copyCmd);

        // Copy data from staging buffer
        uint8_t *srcData = allocator.MapMemory<uint8_t>(stagingBufferAllocation);
        buffer.Allocate(bufferSize);
        memcpy(buffer.Data, srcData, bufferSize);
        allocator.UnmapMemory(stagingBufferAllocation);

        allocator.DestroyBuffer(stagingBuffer, stagingBufferAllocation);
    }

    VulkanImageView::VulkanImageView(const ImageViewSpecification &specification)
        : m_Specification(specification)
    {
        Invalidate();
    }

    VulkanImageView::~VulkanImageView()
    {
#ifdef TODO // From VulkanRenderer
        Renderer::SubmitResourceFree([imageView = m_ImageView]() mutable
                                     {
            auto device = VulkanContext::GetCurrentDevice();
            VkDevice vulkanDevice = device->GetVulkanDevice();

            vkDestroyImageView(vulkanDevice, imageView, nullptr); });
#endif
        m_ImageView = nullptr;
    }

    void VulkanImageView::Invalidate()
    {
#ifdef TODO // From VulkanRenderer
        Ref<VulkanImageView> instance = this;
        Renderer::Submit([instance]() mutable
                         { instance->RT_Invalidate(); });
#endif
    }

    void VulkanImageView::RT_Invalidate()
    {
        auto device = VulkanContext::GetCurrentDevice();
        VkDevice vulkanDevice = device->GetVulkanDevice();

        Ref<VulkanImage2D> vulkanImage = m_Specification.Image.As<VulkanImage2D>();
        const auto &imageSpec = vulkanImage->GetSpecification();

        VkImageAspectFlags aspectMask = Utils::IsDepthFormat(imageSpec.Format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        if (imageSpec.Format == ImageFormat::DEPTH24STENCIL8)
            aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

        VkFormat vulkanFormat = Utils::VulkanImageFormat(imageSpec.Format);

        VkImageViewCreateInfo imageViewCreateInfo = {};
        imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewCreateInfo.viewType = imageSpec.Layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.format = vulkanFormat;
        imageViewCreateInfo.flags = 0;
        imageViewCreateInfo.subresourceRange = {};
        imageViewCreateInfo.subresourceRange.aspectMask = aspectMask;
        imageViewCreateInfo.subresourceRange.baseMipLevel = m_Specification.Mip;
        imageViewCreateInfo.subresourceRange.levelCount = 1;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.layerCount = imageSpec.Layers;
        imageViewCreateInfo.image = vulkanImage->GetImageInfo().Image;
        VK_CHECK_RESULT(vkCreateImageView(vulkanDevice, &imageViewCreateInfo, nullptr, &m_ImageView));
        VKUtils::SetDebugUtilsObjectName(vulkanDevice, VK_OBJECT_TYPE_IMAGE_VIEW, std::format("{} default image view", m_Specification.DebugName), m_ImageView);

        m_DescriptorImageInfo = vulkanImage->GetDescriptorInfoVulkan();
        m_DescriptorImageInfo.imageView = m_ImageView;
    }
}
