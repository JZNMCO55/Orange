#ifndef TEXTURE_H
#define TEXTURE_H

#include "Core/Base/Base.h"
#include "Core/Memory/Buffer.h"
#include "Image.h"
#include "glm/glm.hpp"
#include <filesystem>
#include <string>

namespace Orange
{
    /**
     * @struct TextureSpecification
     * @brief 纹理规格配置结构体
     *
     * 定义了创建纹理所需的所有配置参数，包括格式、尺寸、采样参数
     * 和其他属性。这个结构体提供了灵活的纹理配置选项。
     */
    struct TextureSpecification
    {
        ImageFormat Format = ImageFormat::RGBA;              ///< 纹理格式，默认为RGBA
        uint32_t Width = 1;                                  ///< 纹理宽度，默认为1像素
        uint32_t Height = 1;                                 ///< 纹理高度，默认为1像素
        TextureWrap SamplerWrap = TextureWrap::Repeat;       ///< 纹理包装模式，默认为重复
        TextureFilter SamplerFilter = TextureFilter::Linear; ///< 纹理过滤模式，默认为线性过滤

        bool GenerateMips = true;  ///< 是否生成Mipmap，默认为true
        bool Storage = false;      ///< 是否用作存储纹理，默认为false
        bool StoreLocally = false; ///< 是否在本地存储副本，默认为false

        std::string DebugName; ///< 调试名称，用于调试器和性能分析
    };

    /**
     * @class Texture
     * @brief 纹理抽象基类
     *
     * 这个类提供了纹理的统一接口，封装了不同类型纹理的通用功能。
     * 纹理是GPU上存储图像数据的资源，用于渲染时的采样和着色。
     *
     * 主要功能包括：
     * - 纹理绑定：将纹理绑定到着色器槽位
     * - 属性查询：获取纹理的格式、尺寸等信息
     * - Mipmap管理：支持多级渐远纹理
     * - 哈希计算：用于纹理缓存和比较
     * - 类型识别：区分不同类型的纹理
     *
     * 纹理类型：
     * - Texture2D：二维纹理，最常用的纹理类型
     * - TextureCube：立方体纹理，用于环境映射和天空盒
     * - Texture3D：三维纹理，用于体积渲染
     * - TextureArray：纹理数组，用于批量纹理处理
     *
     * 具体的实现由各个图形API的派生类提供（如VulkanTexture）。
     */
    class Texture : public RendererResource
    {
    public:
        /**
         * @brief 虚析构函数
         *
         * 确保派生类能够正确清理纹理资源。
         */
        virtual ~Texture() {}

        /**
         * @brief 绑定纹理到指定槽位
         * @param slot 纹理槽位，默认为0
         *
         * 将此纹理绑定到指定的纹理槽位，使其可以在着色器中被采样。
         * 不同的图形API有不同的纹理槽位限制。
         */
        virtual void Bind(uint32_t slot = 0) const = 0;

        /**
         * @brief 获取纹理格式
         * @return 纹理的图像格式
         */
        virtual ImageFormat GetFormat() const = 0;

        /**
         * @brief 获取纹理宽度
         * @return 纹理宽度（像素）
         */
        virtual uint32_t GetWidth() const = 0;

        /**
         * @brief 获取纹理高度
         * @return 纹理高度（像素）
         */
        virtual uint32_t GetHeight() const = 0;

        /**
         * @brief 获取纹理尺寸
         * @return 纹理尺寸（宽度，高度）
         */
        virtual glm::uvec2 GetSize() const = 0;

        /**
         * @brief 获取Mipmap级别数量
         * @return Mipmap级别的总数
         *
         * 返回纹理包含的Mipmap级别数量。级别0是原始分辨率，
         * 每个后续级别的分辨率都是前一级别的一半。
         */
        virtual uint32_t GetMipLevelCount() const = 0;

        /**
         * @brief 获取指定Mip级别的尺寸
         * @param mip Mip级别索引
         * @return 指定级别的尺寸（宽度，高度）
         *
         * 返回指定Mipmap级别的纹理尺寸。
         */
        virtual std::pair<uint32_t, uint32_t> GetMipSize(uint32_t mip) const = 0;

        /**
         * @brief 获取纹理哈希值
         * @return 纹理的唯一哈希值
         *
         * 返回纹理的哈希值，用于缓存、比较和快速查找。
         */
        virtual uint64_t GetHash() const = 0;

        /**
         * @brief 获取纹理类型
         * @return 纹理类型枚举值
         */
        virtual TextureType GetType() const = 0;
    };

    /**
     * @class Texture2D
     * @brief 二维纹理类
     *
     * 这个类专门处理二维纹理，是最常用的纹理类型。二维纹理用于
     * 存储平面图像数据，如漫反射贴图、法线贴图、高度图等。
     *
     * 主要功能包括：
     * - 多种创建方式：从文件、内存缓冲区或规格创建
     * - 动态修改：支持运行时替换和调整纹理内容
     * - 尺寸调整：支持动态调整纹理尺寸
     * - 内存访问：提供可写缓冲区用于CPU修改
     * - 锁定机制：支持纹理的读写锁定
     * - sRGB支持：支持sRGB颜色空间转换
     *
     * 使用场景：
     * - 漫反射贴图：物体表面的基础颜色
     * - 法线贴图：表面细节的法线信息
     * - 高度图：表面的高度变化信息
     * - 遮罩贴图：各种材质属性的遮罩
     * - UI纹理：用户界面的图像元素
     * - 渲染目标：作为帧缓冲区的颜色附件
     *
     * 生命周期：
     * 1. 创建：通过静态工厂方法创建实例
     * 2. 加载：从文件或缓冲区加载图像数据
     * 3. 使用：绑定到着色器进行采样
     * 4. 修改：可选的动态内容更新
     * 5. 销毁：自动清理GPU资源
     */
    class Texture2D : public Texture
    {
    public:
        /**
         * @brief 创建空的2D纹理
         * @param specification 纹理规格配置
         * @return 创建的纹理实例智能指针
         *
         * 根据指定的规格创建一个空的2D纹理，纹理内容未初始化。
         */
        static Ref<Texture2D> Create(const TextureSpecification &specification);

        /**
         * @brief 从文件创建2D纹理
         * @param specification 纹理规格配置
         * @param filepath 图像文件路径
         * @return 创建的纹理实例智能指针
         *
         * 从指定的图像文件创建2D纹理，支持常见的图像格式。
         */
        static Ref<Texture2D> Create(const TextureSpecification &specification, const std::filesystem::path &filepath);

        /**
         * @brief 从缓冲区创建2D纹理
         * @param specification 纹理规格配置
         * @param imageData 图像数据缓冲区
         * @return 创建的纹理实例智能指针
         *
         * 从内存中的图像数据创建2D纹理。
         */
        static Ref<Texture2D> Create(const TextureSpecification &specification, Buffer imageData);

        /**
         * @brief 从现有纹理创建sRGB纹理
         * @param texture 源纹理
         * @return 重新解释为sRGB的纹理实例
         *
         * 将给定纹理的数据重新解释为sRGB格式，用于颜色空间转换。
         */
        static Ref<Texture2D> CreateFromSRGB(Ref<Texture2D> texture);

        /**
         * @brief 从文件创建纹理内容
         * @param specification 纹理规格配置
         * @param filepath 图像文件路径
         *
         * 为现有纹理实例从文件加载内容。
         */
        virtual void CreateFromFile(const TextureSpecification &specification, const std::filesystem::path &filepath) = 0;

        /**
         * @brief 从缓冲区创建纹理内容
         * @param specification 纹理规格配置
         * @param data 图像数据缓冲区，默认为空
         *
         * 为现有纹理实例从缓冲区加载内容。
         */
        virtual void CreateFromBuffer(const TextureSpecification &specification, Buffer data = Buffer()) = 0;

        /**
         * @brief 从文件替换纹理内容
         * @param specification 纹理规格配置
         * @param filepath 新的图像文件路径
         *
         * 用新的图像文件内容替换现有纹理的数据。
         */
        virtual void ReplaceFromFile(const TextureSpecification &specification, const std::filesystem::path &filepath) = 0;

        /**
         * @brief 调整纹理尺寸
         * @param size 新的纹理尺寸
         *
         * 调整纹理的尺寸，这可能涉及重新分配GPU内存。
         */
        virtual void Resize(const glm::uvec2 &size) = 0;

        /**
         * @brief 调整纹理尺寸
         * @param width 新的宽度
         * @param height 新的高度
         *
         * 调整纹理的尺寸，这可能涉及重新分配GPU内存。
         */
        virtual void Resize(const uint32_t width, const uint32_t height) = 0;

        /**
         * @brief 获取底层图像对象
         * @return 图像对象的智能指针
         *
         * 返回纹理底层的Image2D对象，用于更底层的操作。
         */
        virtual Ref<Image2D> GetImage() const = 0;

        /**
         * @brief 锁定纹理
         *
         * 锁定纹理以进行CPU访问，防止GPU同时访问。
         * 锁定期间纹理不应被GPU使用。
         */
        virtual void Lock() = 0;

        /**
         * @brief 解锁纹理
         *
         * 解锁纹理，允许GPU重新访问。必须与Lock()配对使用。
         */
        virtual void Unlock() = 0;

        /**
         * @brief 获取可写缓冲区
         * @return 可写的内存缓冲区
         *
         * 返回一个可写的缓冲区，允许CPU直接修改纹理数据。
         * 通常需要先调用Lock()。
         */
        virtual Buffer GetWriteableBuffer() = 0;

        /**
         * @brief 检查纹理是否已加载
         * @return 如果纹理已完全加载则返回true
         *
         * 检查纹理数据是否已成功加载到GPU内存中。
         */
        virtual bool Loaded() const = 0;

        /**
         * @brief 获取纹理文件路径
         * @return 纹理源文件的路径
         *
         * 返回创建此纹理时使用的文件路径（如果有）。
         */
        virtual const std::filesystem::path &GetPath() const = 0;

        /**
         * @brief 获取纹理类型
         * @return 纹理类型（Texture2D）
         */
        virtual TextureType GetType() const override { return TextureType::Texture2D; }
#ifdef TODO
        /**
         * @brief 获取静态资产类型
         * @return 资产类型（Texture）
         */
        static AssetType GetStaticType() { return AssetType::Texture; }

        /**
         * @brief 获取资产类型
         * @return 资产类型（Texture）
         */
        virtual AssetType GetAssetType() const override { return GetStaticType(); }
#endif
    };

    /**
     * @class TextureCube
     * @brief 立方体纹理类
     *
     * 这个类专门处理立方体纹理，由6个面组成的纹理，主要用于
     * 环境映射、天空盒和反射效果。立方体纹理提供了360度的
     * 环境信息，是现代渲染中重要的技术。
     *
     * 主要功能包括：
     * - 6面纹理管理：管理立方体的6个面
     * - 环境映射：用于物体表面的环境反射
     * - 天空盒渲染：作为场景的背景环境
     * - IBL支持：基于图像的光照计算
     *
     * 立方体纹理的6个面：
     * - +X (Right)：右面
     * - -X (Left)：左面
     * - +Y (Top)：上面
     * - -Y (Bottom)：下面
     * - +Z (Front)：前面
     * - -Z (Back)：后面
     *
     * 使用场景：
     * - 天空盒：场景的背景环境
     * - 环境反射：物体表面的环境映射
     * - IBL：基于图像的环境光照
     * - 全景图像：360度全景内容显示
     *
     * 采样方式：
     * 使用3D方向向量进行采样，GPU会自动选择合适的面
     * 并进行双线性插值。
     */
    class TextureCube : public Texture
    {
    public:
        /**
         * @brief 创建立方体纹理
         * @param specification 纹理规格配置
         * @param imageData 图像数据缓冲区，默认为空
         * @return 创建的立方体纹理实例智能指针
         *
         * 根据指定的规格创建立方体纹理。如果提供了图像数据，
         * 将用于初始化纹理内容。
         */
        static Ref<TextureCube> Create(const TextureSpecification &specification, Buffer imageData = Buffer());

        /**
         * @brief 获取纹理类型
         * @return 纹理类型（TextureCube）
         */
        virtual TextureType GetType() const override { return TextureType::TextureCube; }
#ifdef TODO
        /**
         * @brief 获取静态资产类型
         * @return 资产类型（EnvMap）
         */
        static AssetType GetStaticType() { return AssetType::EnvMap; }

        /**
         * @brief 获取资产类型
         * @return 资产类型（EnvMap）
         */
        virtual AssetType GetAssetType() const override { return GetStaticType(); }
#endif
    };
}

#endif