#ifndef VULKAN_IMAGE_H
#define VULKAN_IMAGE_H

#include "Platform/RenderInterface/Image.h"
#include "Vulkan.h"
#include "VulkanContext.h"
#include "VulkanMemoryAllocator/vk_mem_alloc.h"

namespace Orange
{
    /**
     * @struct VulkanImageInfo
     * @brief Vulkan图像信息结构
     * @details 包含Vulkan图像的所有相关句柄和资源
     */
    struct VulkanImageInfo
    {
        VkImage Image = nullptr;             ///< Vulkan图像句柄
        VkImageView ImageView = nullptr;     ///< 图像视图句柄
        VkSampler Sampler = nullptr;         ///< 采样器句柄
        VmaAllocation MemoryAlloc = nullptr; ///< VMA内存分配句柄
    };

    /**
     * @class VulkanImage2D
     * @brief Vulkan 2D图像类
     * @details 继承自Image2D基类，提供Vulkan平台特定的2D图像实现
     *
     * 该类负责管理Vulkan 2D图像的生命周期，包括：
     * - 图像的创建和销毁
     * - 图像大小调整和重新创建
     * - 多层图像视图的管理
     * - Mipmap级别的处理
     * - 图像数据的读写操作
     * - 描述符信息的维护
     */
    class VulkanImage2D : public Image2D
    {
    public:
        /**
         * @brief 构造函数
         * @param specification 图像规格说明
         * @details 根据规格创建Vulkan 2D图像
         */
        VulkanImage2D(const ImageSpecification &specification);

        /**
         * @brief 析构函数
         * @details 释放图像资源
         */
        virtual ~VulkanImage2D() override;

        /**
         * @brief 调整图像大小
         * @param size 新的图像尺寸
         * @details 重新创建图像以适应新尺寸
         */
        virtual void Resize(const glm::uvec2 &size) override
        {
            Resize(size.x, size.y);
        }

        /**
         * @brief 调整图像大小
         * @param width 新宽度
         * @param height 新高度
         * @details 重新创建图像以适应新尺寸
         */
        virtual void Resize(const uint32_t width, const uint32_t height) override
        {
            m_Specification.Width = width;
            m_Specification.Height = height;
            Invalidate();
        }

        /**
         * @brief 使图像失效并重新创建
         * @details 重新创建底层Vulkan资源
         */
        virtual void Invalidate() override;

        /**
         * @brief 释放图像资源
         * @details 手动释放GPU资源
         */
        virtual void Release() override;

        /**
         * @brief 检查图像是否有效
         * @return 是否有效
         */
        virtual bool IsValid() const override { return m_DescriptorImageInfo.imageView != nullptr; }

        /**
         * @brief 获取图像宽度
         * @return 宽度像素值
         */
        virtual uint32_t GetWidth() const override { return m_Specification.Width; }

        /**
         * @brief 获取图像高度
         * @return 高度像素值
         */
        virtual uint32_t GetHeight() const override { return m_Specification.Height; }

        /**
         * @brief 获取图像尺寸
         * @return 尺寸向量
         */
        virtual glm::uvec2 GetSize() const override { return {m_Specification.Width, m_Specification.Height}; }

        /**
         * @brief 检查是否有Mipmap
         * @return 是否有多个Mip级别
         */
        virtual bool HasMips() const override { return m_Specification.Mips > 1; }

        /**
         * @brief 获取图像宽高比
         * @return 宽高比值
         */
        virtual float GetAspectRatio() const override { return (float)m_Specification.Width / (float)m_Specification.Height; }

        /**
         * @brief 获取最接近的Mip级别
         * @param width 目标宽度
         * @param height 目标高度
         * @return 最接近的Mip级别
         * @details 根据给定尺寸找到最匹配的Mip级别
         */
        int GetClosestMipLevel(uint32_t width, uint32_t height) const;

        /**
         * @brief 获取Mip级别的尺寸
         * @param mipLevel Mip级别
         * @return Mip级别的宽度和高度
         */
        std::pair<uint32_t, uint32_t> GetMipLevelSize(int mipLevel) const;

        /**
         * @brief 获取图像规格说明
         * @return 图像规格说明的引用
         */
        virtual ImageSpecification &GetSpecification() override { return m_Specification; }

        /**
         * @brief 获取图像规格说明（常量版本）
         * @return 图像规格说明的常量引用
         */
        virtual const ImageSpecification &GetSpecification() const override { return m_Specification; }

        /**
         * @brief 渲染线程图像失效处理
         * @details 在渲染线程中重新创建图像资源
         */
        void RT_Invalidate();

        /**
         * @brief 创建每层图像视图
         * @details 为图像的每一层创建独立的图像视图
         */
        virtual void CreatePerLayerImageViews() override;

        /**
         * @brief 渲染线程创建每层图像视图
         * @details 在渲染线程中为每一层创建图像视图
         */
        void RT_CreatePerLayerImageViews();

        /**
         * @brief 渲染线程创建特定层的图像视图
         * @param layerIndices 要创建视图的层索引列表
         * @details 为指定的层创建图像视图
         */
        void RT_CreatePerSpecificLayerImageViews(const std::vector<uint32_t> &layerIndices);

        /**
         * @brief 获取指定层的图像视图
         * @param layer 层索引
         * @return 图像视图句柄
         */
        virtual VkImageView GetLayerImageView(uint32_t layer)
        {
            ORG_CORE_ASSERT(layer < m_PerLayerImageViews.size());
            return m_PerLayerImageViews[layer];
        }

        /**
         * @brief 获取Mip级别的图像视图
         * @param mip Mip级别
         * @return 图像视图句柄
         */
        VkImageView GetMipImageView(uint32_t mip);

        /**
         * @brief 渲染线程获取Mip级别的图像视图
         * @param mip Mip级别
         * @return 图像视图句柄
         */
        VkImageView RT_GetMipImageView(uint32_t mip);

        /**
         * @brief 获取图像信息
         * @return 图像信息结构的引用
         */
        VulkanImageInfo &GetImageInfo() { return m_Info; }

        /**
         * @brief 获取图像信息（常量版本）
         * @return 图像信息结构的常量引用
         */
        const VulkanImageInfo &GetImageInfo() const { return m_Info; }

        /**
         * @brief 获取描述符信息
         * @return 资源描述符信息
         */
        virtual ResourceDescriptorInfo GetDescriptorInfo() const override { return (ResourceDescriptorInfo)&m_DescriptorImageInfo; }

        /**
         * @brief 获取Vulkan描述符信息
         * @return VkDescriptorImageInfo结构的引用
         */
        const VkDescriptorImageInfo &GetDescriptorInfoVulkan() const { return *(VkDescriptorImageInfo *)GetDescriptorInfo(); }

        /**
         * @brief 获取图像数据缓冲区
         * @return 数据缓冲区
         */
        virtual Buffer GetBuffer() const override { return m_ImageData; }

        /**
         * @brief 获取图像数据缓冲区（可修改版本）
         * @return 数据缓冲区引用
         */
        virtual Buffer &GetBuffer() override { return m_ImageData; }

        /**
         * @brief 获取GPU内存使用量
         * @return GPU内存使用字节数
         */
        virtual uint64_t GetGPUMemoryUsage() const override { return m_GPUAllocationSize; }

        /**
         * @brief 获取图像哈希值
         * @return 图像的唯一哈希值
         * @details 基于图像句柄地址生成哈希值
         */
        virtual uint64_t GetHash() const override { return (uint64_t)m_Info.Image; }

        /**
         * @brief 更新描述符信息
         * @details 刷新描述符集合中的图像信息
         */
        void UpdateDescriptor();

        /**
         * @brief 获取图像引用映射表（调试用）
         * @return 图像引用映射表
         * @details 用于调试和内存泄漏检测
         */
        static const std::map<VkImage, WeakRef<VulkanImage2D>> &GetImageRefs();

        /**
         * @brief 设置图像数据
         * @param buffer 数据缓冲区
         * @details 将数据上传到GPU图像
         */
        virtual void SetData(Buffer buffer) override;

        /**
         * @brief 复制图像数据到主机缓冲区
         * @param buffer 目标缓冲区
         * @details 将GPU图像数据读取到CPU内存
         */
        virtual void CopyToHostBuffer(Buffer &buffer) const override;

    private:
        ImageSpecification m_Specification; ///< 图像规格说明

        Buffer m_ImageData; ///< 图像数据缓冲区

        VulkanImageInfo m_Info;               ///< Vulkan图像信息
        VkDeviceSize m_GPUAllocationSize = 0; ///< GPU分配大小

        std::vector<VkImageView> m_PerLayerImageViews;      ///< 每层图像视图列表
        std::map<uint32_t, VkImageView> m_PerMipImageViews; ///< 每个Mip级别的图像视图映射
        VkDescriptorImageInfo m_DescriptorImageInfo = {};   ///< 描述符图像信息
    };

    /**
     * @class VulkanImageView
     * @brief Vulkan图像视图类
     * @details 继承自ImageView基类，提供Vulkan平台特定的图像视图实现
     *
     * 该类负责管理Vulkan图像视图的生命周期，包括：
     * - 图像视图的创建和销毁
     * - 视图规格的管理
     * - 描述符信息的维护
     */
    class VulkanImageView : public ImageView
    {
    public:
        /**
         * @brief 构造函数
         * @param specification 图像视图规格说明
         * @details 根据规格创建Vulkan图像视图
         */
        VulkanImageView(const ImageViewSpecification &specification);

        /**
         * @brief 析构函数
         * @details 释放图像视图资源
         */
        virtual ~VulkanImageView();

        /**
         * @brief 使图像视图失效并重新创建
         * @details 重新创建底层Vulkan资源
         */
        void Invalidate();

        /**
         * @brief 渲染线程图像视图失效处理
         * @details 在渲染线程中重新创建图像视图资源
         */
        void RT_Invalidate();

        /**
         * @brief 获取图像视图句柄
         * @return VkImageView句柄
         */
        VkImageView GetImageView() const { return m_ImageView; }

        /**
         * @brief 获取描述符信息
         * @return 资源描述符信息
         */
        virtual ResourceDescriptorInfo GetDescriptorInfo() const override { return (ResourceDescriptorInfo)&m_DescriptorImageInfo; }

        /**
         * @brief 获取Vulkan描述符信息
         * @return VkDescriptorImageInfo结构的引用
         */
        const VkDescriptorImageInfo &GetDescriptorInfoVulkan() const { return *(VkDescriptorImageInfo *)GetDescriptorInfo(); }

    private:
        ImageViewSpecification m_Specification; ///< 图像视图规格说明
        VkImageView m_ImageView = nullptr;      ///< Vulkan图像视图句柄

        VkDescriptorImageInfo m_DescriptorImageInfo = {}; ///< 描述符图像信息
    };

    namespace Utils
    {

        /**
         * @brief 将图像格式转换为Vulkan格式
         * @param format 引擎图像格式
         * @return 对应的VkFormat
         * @details 将引擎内部的图像格式枚举转换为Vulkan API格式
         */
        inline VkFormat VulkanImageFormat(ImageFormat format)
        {
            switch (format)
            {
            case ImageFormat::RED8UN:
                return VK_FORMAT_R8_UNORM;
            case ImageFormat::RED8UI:
                return VK_FORMAT_R8_UINT;
            case ImageFormat::RED16UI:
                return VK_FORMAT_R16_UINT;
            case ImageFormat::RED32UI:
                return VK_FORMAT_R32_UINT;
            case ImageFormat::RED32F:
                return VK_FORMAT_R32_SFLOAT;
            case ImageFormat::RG8:
                return VK_FORMAT_R8G8_UNORM;
            case ImageFormat::RG16F:
                return VK_FORMAT_R16G16_SFLOAT;
            case ImageFormat::RG32F:
                return VK_FORMAT_R32G32_SFLOAT;
            case ImageFormat::RGBA:
                return VK_FORMAT_R8G8B8A8_UNORM;
            case ImageFormat::SRGBA:
                return VK_FORMAT_R8G8B8A8_SRGB;
            case ImageFormat::RGBA16F:
                return VK_FORMAT_R16G16B16A16_SFLOAT;
            case ImageFormat::RGBA32F:
                return VK_FORMAT_R32G32B32A32_SFLOAT;
            case ImageFormat::B10R11G11UF:
                return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
            case ImageFormat::DEPTH32FSTENCIL8UINT:
                return VK_FORMAT_D32_SFLOAT_S8_UINT;
            case ImageFormat::DEPTH32F:
                return VK_FORMAT_D32_SFLOAT;
            case ImageFormat::DEPTH24STENCIL8:
                return VulkanContext::GetCurrentDevice()->GetPhysicalDevice()->GetDepthFormat();
            }
            ORG_CORE_ASSERT(false);
            return VK_FORMAT_UNDEFINED;
        }

    }
}

#endif