#ifndef IMAGE_H
#define IMAGE_H

#include "Core/Base/Ref.h"
#include "Core/Memory/Buffer.h"
#include "RendererResource.h"
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/integer.hpp>

namespace Orange
{
    /**
     * @enum ImageFormat
     * @brief 图像格式枚举
     *
     * 定义了支持的各种图像格式，包括颜色格式、深度格式和特殊格式。
     * 这些格式对应于GPU硬件支持的像素格式。
     */
    enum class ImageFormat
    {
        None = 0, ///< 无格式
        RED8UN,   ///< 8位无符号归一化红色通道
        RED8UI,   ///< 8位无符号整数红色通道
        RED16UI,  ///< 16位无符号整数红色通道
        RED32UI,  ///< 32位无符号整数红色通道
        RED32F,   ///< 32位浮点红色通道
        RG8,      ///< 8位双通道（红绿）
        RG16F,    ///< 16位浮点双通道（红绿）
        RG32F,    ///< 32位浮点双通道（红绿）
        RGB,      ///< 8位RGB三通道
        RGBA,     ///< 8位RGBA四通道
        RGBA16F,  ///< 16位浮点RGBA四通道
        RGBA32F,  ///< 32位浮点RGBA四通道

        B10R11G11UF, ///< 特殊格式：10位蓝色，11位红绿无符号浮点

        SRGB,  ///< sRGB颜色空间RGB格式
        SRGBA, ///< sRGB颜色空间RGBA格式

        DEPTH32FSTENCIL8UINT, ///< 32位浮点深度 + 8位无符号整数模板
        DEPTH32F,             ///< 32位浮点深度
        DEPTH24STENCIL8,      ///< 24位深度 + 8位模板

        // 默认格式
        Depth = DEPTH24STENCIL8, ///< 默认深度格式
    };

    /**
     * @enum ImageUsage
     * @brief 图像使用模式枚举
     *
     * 定义了图像的不同使用方式，影响GPU内存分配和访问模式。
     */
    enum class ImageUsage
    {
        None = 0,   ///< 未指定用途
        Texture,    ///< 作为纹理使用（着色器采样）
        Attachment, ///< 作为渲染目标附件使用
        Storage,    ///< 作为存储图像使用（计算着色器读写）
        HostRead    ///< 支持主机端读取
    };

    /**
     * @enum TextureWrap
     * @brief 纹理包装模式枚举
     *
     * 定义了纹理坐标超出[0,1]范围时的处理方式。
     */
    enum class TextureWrap
    {
        None = 0, ///< 无包装模式
        Clamp,    ///< 夹紧模式：坐标被限制在[0,1]范围内
        Repeat    ///< 重复模式：纹理在边界处重复
    };

    /**
     * @enum TextureFilter
     * @brief 纹理过滤模式枚举
     *
     * 定义了纹理采样时的过滤方式，影响图像质量和性能。
     */
    enum class TextureFilter
    {
        None = 0, ///< 无过滤
        Linear,   ///< 线性过滤：双线性插值
        Nearest,  ///< 最近邻过滤：选择最近的像素
        Cubic     ///< 立方过滤：三次插值（高质量）
    };

    /**
     * @enum TextureType
     * @brief 纹理类型枚举
     *
     * 定义了不同类型的纹理几何形状。
     */
    enum class TextureType
    {
        None = 0,   ///< 无类型
        Texture2D,  ///< 2D纹理
        TextureCube ///< 立方体纹理（用于环境映射）
    };

    /**
     * @struct ImageSpecification
     * @brief 图像规格配置结构体
     *
     * 包含创建图像所需的所有配置参数，定义了图像的格式、
     * 尺寸、用途和其他属性。
     */
    struct ImageSpecification
    {
        std::string DebugName; ///< 调试名称，用于调试和性能分析

        ImageFormat Format = ImageFormat::RGBA; ///< 图像格式，默认为RGBA
        ImageUsage Usage = ImageUsage::Texture; ///< 使用模式，默认为纹理
        bool Transfer = false;                  ///< 是否用于传输操作
        uint32_t Width = 1;                     ///< 图像宽度（像素）
        uint32_t Height = 1;                    ///< 图像高度（像素）
        uint32_t Mips = 1;                      ///< Mipmap层级数量
        uint32_t Layers = 1;                    ///< 图像层数（用于数组纹理）
        bool CreateSampler = true;              ///< 是否创建采样器
    };

    /**
     * @struct ImageSubresourceRange
     * @brief 图像子资源范围结构体
     *
     * 定义了图像操作涉及的Mipmap层级和数组层的范围。
     */
    struct ImageSubresourceRange
    {
        uint32_t BaseMip = 0;           ///< 起始Mipmap层级
        uint32_t MipCount = UINT_MAX;   ///< Mipmap层级数量（UINT_MAX表示所有）
        uint32_t BaseLayer = 0;         ///< 起始数组层
        uint32_t LayerCount = UINT_MAX; ///< 数组层数量（UINT_MAX表示所有）
    };

    /**
     * @union ImageClearValue
     * @brief 图像清除值联合体
     *
     * 定义了清除图像时使用的值，支持不同的数据类型。
     */
    union ImageClearValue
    {
        glm::vec4 FloatValues; ///< 浮点值（用于浮点格式）
        glm::ivec4 IntValues;  ///< 有符号整数值
        glm::uvec4 UIntValues; ///< 无符号整数值
    };

    /**
     * @class Image
     * @brief 图像抽象基类
     *
     * 这个类提供了图像资源的统一接口，封装了图像的创建、管理和操作。
     * 图像是GPU上存储像素数据的基本单元，可以用作纹理、渲染目标或
     * 计算着色器的存储。
     *
     * 主要功能包括：
     * - 图像的创建和销毁
     * - 尺寸调整和格式管理
     * - 数据上传和下载
     * - Mipmap和多层支持
     * - GPU内存使用统计
     *
     * 具体的实现由各个图形API的派生类提供（如VulkanImage）。
     */
    class Image : public RendererResource
    {
    public:
        /**
         * @brief 虚析构函数
         *
         * 确保派生类能够正确清理GPU资源。
         */
        virtual ~Image() = default;

        /**
         * @brief 调整图像尺寸
         * @param width 新的宽度
         * @param height 新的高度
         *
         * 调整图像的尺寸，可能会重新分配GPU内存。
         */
        virtual void Resize(const uint32_t width, const uint32_t height) = 0;

        /**
         * @brief 使图像失效并重新创建
         *
         * 重新创建图像的GPU资源，通常在规格改变时调用。
         */
        virtual void Invalidate() = 0;

        /**
         * @brief 释放图像资源
         *
         * 立即释放图像占用的GPU资源。
         */
        virtual void Release() = 0;

        /**
         * @brief 获取图像宽度
         * @return 图像宽度（像素）
         */
        virtual uint32_t GetWidth() const = 0;

        /**
         * @brief 获取图像高度
         * @return 图像高度（像素）
         */
        virtual uint32_t GetHeight() const = 0;

        /**
         * @brief 获取图像尺寸
         * @return 图像尺寸（宽度，高度）
         */
        virtual glm::uvec2 GetSize() const = 0;

        /**
         * @brief 检查是否有Mipmap
         * @return 如果图像有多个Mipmap层级则返回true
         */
        virtual bool HasMips() const = 0;

        /**
         * @brief 获取宽高比
         * @return 图像的宽高比（宽度/高度）
         */
        virtual float GetAspectRatio() const = 0;

        /**
         * @brief 获取图像规格配置（可修改）
         * @return 图像规格配置的引用
         */
        virtual ImageSpecification &GetSpecification() = 0;

        /**
         * @brief 获取图像规格配置（只读）
         * @return 图像规格配置的常量引用
         */
        virtual const ImageSpecification &GetSpecification() const = 0;

        /**
         * @brief 获取图像数据缓冲区（只读）
         * @return 图像数据缓冲区
         */
        virtual Buffer GetBuffer() const = 0;

        /**
         * @brief 获取图像数据缓冲区（可修改）
         * @return 图像数据缓冲区的引用
         */
        virtual Buffer &GetBuffer() = 0;

        /**
         * @brief 获取GPU内存使用量
         * @return GPU内存使用量（字节）
         */
        virtual uint64_t GetGPUMemoryUsage() const = 0;

        /**
         * @brief 为每个层创建图像视图
         *
         * 为数组纹理的每个层创建单独的图像视图，
         * 用于单独访问各个层。
         */
        virtual void CreatePerLayerImageViews() = 0;

        /**
         * @brief 获取图像哈希值
         * @return 图像的哈希值，用于缓存和比较
         */
        virtual uint64_t GetHash() const = 0;

        /**
         * @brief 设置图像数据
         * @param buffer 包含图像数据的缓冲区
         *
         * 将缓冲区中的数据上传到图像。
         */
        virtual void SetData(Buffer buffer) = 0;

        /**
         * @brief 将图像数据复制到主机缓冲区
         * @param buffer 目标缓冲区引用
         *
         * 将GPU上的图像数据下载到主机内存缓冲区。
         */
        virtual void CopyToHostBuffer(Buffer &buffer) const = 0;

        // TODO: usage (eg. shader read)
    };

    /**
     * @class Image2D
     * @brief 2D图像类
     *
     * 继承自Image类，专门用于处理2D图像。提供了2D图像特有的
     * 功能和优化。
     */
    class Image2D : public Image
    {
    public:
        /**
         * @brief 创建2D图像实例
         * @param specification 图像规格配置
         * @param buffer 初始数据缓冲区，默认为空
         * @return 创建的2D图像实例智能指针
         *
         * 根据指定的规格创建一个新的2D图像实例。
         */
        static Ref<Image2D> Create(const ImageSpecification &specification, Buffer buffer = Buffer());

        /**
         * @brief 调整图像尺寸（向量版本）
         * @param size 新的尺寸（宽度，高度）
         *
         * 使用向量参数调整图像尺寸。
         */
        virtual void Resize(const glm::uvec2 &size) = 0;

        /**
         * @brief 检查图像是否有效
         * @return 如果图像有效则返回true
         *
         * 检查图像是否已正确创建并可以使用。
         */
        virtual bool IsValid() const = 0;
    };

    /**
     * @namespace Utils
     * @brief 图像工具函数命名空间
     *
     * 包含了各种图像处理和格式转换的工具函数。
     */
    namespace Utils
    {

        /**
         * @brief 获取图像格式的每像素字节数
         * @param format 图像格式
         * @return 每像素的字节数
         *
         * 返回指定图像格式中每个像素占用的字节数。
         */
        inline uint32_t GetImageFormatBPP(ImageFormat format)
        {
            switch (format)
            {
            case ImageFormat::RED8UN:
                return 1; ///< 1字节
            case ImageFormat::RED8UI:
                return 1; ///< 1字节
            case ImageFormat::RED16UI:
                return 2; ///< 2字节
            case ImageFormat::RED32UI:
                return 4; ///< 4字节
            case ImageFormat::RED32F:
                return 4; ///< 4字节
            case ImageFormat::RGB:
            case ImageFormat::SRGB:
                return 3; ///< 3字节
            case ImageFormat::RGBA:
                return 4; ///< 4字节
            case ImageFormat::SRGBA:
                return 4; ///< 4字节
            case ImageFormat::RGBA16F:
                return 2 * 4; ///< 8字节
            case ImageFormat::RGBA32F:
                return 4 * 4; ///< 16字节
            case ImageFormat::B10R11G11UF:
                return 4; ///< 4字节
            }
            ORG_CORE_ASSERT(false);
            return 0;
        }

        /**
         * @brief 检查格式是否基于整数
         * @param format 图像格式
         * @return 如果格式基于整数则返回true
         *
         * 判断指定的图像格式是否使用整数数据类型。
         */
        inline bool IsIntegerBased(const ImageFormat format)
        {
            switch (format)
            {
            case ImageFormat::RED16UI:
            case ImageFormat::RED32UI:
            case ImageFormat::RED8UI:
            case ImageFormat::DEPTH32FSTENCIL8UINT:
                return true;
            case ImageFormat::DEPTH32F:
            case ImageFormat::RED8UN:
            case ImageFormat::RGBA32F:
            case ImageFormat::B10R11G11UF:
            case ImageFormat::RG16F:
            case ImageFormat::RG32F:
            case ImageFormat::RED32F:
            case ImageFormat::RG8:
            case ImageFormat::RGBA:
            case ImageFormat::RGBA16F:
            case ImageFormat::RGB:
            case ImageFormat::SRGB:
            case ImageFormat::SRGBA:
            case ImageFormat::DEPTH24STENCIL8:
                return false;
            }
            ORG_CORE_ASSERT(false);
            return false;
        }

        /**
         * @brief 计算Mipmap层级数量
         * @param width 图像宽度
         * @param height 图像高度
         * @return Mipmap层级数量
         *
         * 根据图像尺寸计算完整的Mipmap链所需的层级数量。
         */
        inline uint32_t CalculateMipCount(uint32_t width, uint32_t height)
        {
            return (uint32_t)glm::floor(glm::log2(glm::min(width, height))) + 1;
        }

        /**
         * @brief 获取图像内存大小
         * @param format 图像格式
         * @param width 图像宽度
         * @param height 图像高度
         * @return 图像占用的内存大小（字节）
         *
         * 计算指定格式和尺寸的图像占用的内存大小。
         */
        inline uint32_t GetImageMemorySize(ImageFormat format, uint32_t width, uint32_t height)
        {
            return width * height * GetImageFormatBPP(format);
        }

        /**
         * @brief 检查是否为深度格式
         * @param format 图像格式
         * @return 如果是深度格式则返回true
         *
         * 判断指定的图像格式是否为深度或深度模板格式。
         */
        inline bool IsDepthFormat(ImageFormat format)
        {
            if (format == ImageFormat::DEPTH24STENCIL8 || format == ImageFormat::DEPTH32F || format == ImageFormat::DEPTH32FSTENCIL8UINT)
                return true;

            return false;
        }

    }

    /**
     * @struct ImageViewSpecification
     * @brief 图像视图规格配置结构体
     *
     * 定义了创建图像视图所需的配置参数。图像视图允许以不同的
     * 方式访问同一个图像资源。
     */
    struct ImageViewSpecification
    {
        Ref<Image2D> Image; ///< 关联的图像对象
        uint32_t Mip = 0;   ///< 要访问的Mipmap层级

        std::string DebugName; ///< 调试名称
    };

    /**
     * @class ImageView
     * @brief 图像视图类
     *
     * 图像视图提供了对图像资源的特定视角访问，允许访问图像的
     * 特定Mipmap层级或数组层。这在渲染管线中用于绑定图像的
     * 特定部分到着色器。
     */
    class ImageView : public RendererResource
    {
    public:
        /**
         * @brief 虚析构函数
         *
         * 确保派生类能够正确清理资源。
         */
        virtual ~ImageView() = default;

        /**
         * @brief 创建图像视图实例
         * @param specification 图像视图规格配置
         * @return 创建的图像视图实例智能指针
         *
         * 根据指定的规格创建一个新的图像视图实例。
         */
        static Ref<ImageView> Create(const ImageViewSpecification &specification);
    };
}

#endif