#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <glm/glm.hpp>
#include <map>

#include "RendererTypes.h"
#include "Image.h"

namespace Orange
{
    class Framebuffer;

    /**
     * @enum FramebufferBlendMode
     * @brief 帧缓冲区混合模式枚举
     *
     * 定义了帧缓冲区支持的各种混合模式，用于控制新绘制的像素
     * 如何与现有像素进行混合。
     */
    enum class FramebufferBlendMode
    {
        None = 0,                 ///< 无混合模式
        OneZero,                  ///< 源因子=1，目标因子=0（完全覆盖）
        SrcAlphaOneMinusSrcAlpha, ///< 标准Alpha混合：源Alpha * 源颜色 + (1-源Alpha) * 目标颜色
        Additive,                 ///< 加法混合：源颜色 + 目标颜色
        Zero_SrcColor             ///< 零源颜色混合：0 * 源颜色 + 源颜色 * 目标颜色
    };

    /**
     * @enum AttachmentLoadOp
     * @brief 附件加载操作枚举
     *
     * 定义了渲染开始时如何处理附件的现有内容。
     */
    enum class AttachmentLoadOp
    {
        Inherit = 0, ///< 继承现有内容（不做任何操作）
        Clear = 1,   ///< 清除附件内容到指定值
        Load = 2     ///< 加载并保留现有内容
    };

    /**
     * @struct FramebufferTextureSpecification
     * @brief 帧缓冲区纹理规格配置结构体
     *
     * 定义了帧缓冲区中单个纹理附件的配置参数，包括格式、
     * 混合设置和加载操作。
     */
    struct FramebufferTextureSpecification
    {
        /**
         * @brief 默认构造函数
         */
        FramebufferTextureSpecification() = default;

        /**
         * @brief 构造函数
         * @param format 纹理格式
         */
        FramebufferTextureSpecification(ImageFormat format) : Format(format) {}

        ImageFormat Format;                                                              ///< 纹理格式
        bool Blend = true;                                                               ///< 是否启用混合
        FramebufferBlendMode BlendMode = FramebufferBlendMode::SrcAlphaOneMinusSrcAlpha; ///< 混合模式
        AttachmentLoadOp LoadOp = AttachmentLoadOp::Inherit;                             ///< 加载操作
                                                                                         // TODO: filtering/wrap
    };

    /**
     * @struct FramebufferAttachmentSpecification
     * @brief 帧缓冲区附件规格配置结构体
     *
     * 包含帧缓冲区所有附件的配置信息，支持多个颜色附件。
     */
    struct FramebufferAttachmentSpecification
    {
        /**
         * @brief 默认构造函数
         */
        FramebufferAttachmentSpecification() = default;

        /**
         * @brief 构造函数
         * @param attachments 附件配置列表
         */
        FramebufferAttachmentSpecification(const std::initializer_list<FramebufferTextureSpecification> &attachments)
            : Attachments(attachments)
        {
        }

        std::vector<FramebufferTextureSpecification> Attachments; ///< 附件配置列表
    };

    /**
     * @struct FramebufferSpecification
     * @brief 帧缓冲区规格配置结构体
     *
     * 包含创建帧缓冲区所需的所有配置参数，定义了帧缓冲区的
     * 尺寸、格式、混合模式和其他属性。
     */
    struct FramebufferSpecification
    {
        float Scale = 1.0f;                              ///< 缩放因子，用于分辨率缩放
        uint32_t Width = 0;                              ///< 帧缓冲区宽度
        uint32_t Height = 0;                             ///< 帧缓冲区高度
        glm::vec4 ClearColor = {0.0f, 0.0f, 0.0f, 1.0f}; ///< 清除颜色（RGBA）
        float DepthClearValue = 0.0f;                    ///< 深度清除值
        bool ClearColorOnLoad = true;                    ///< 加载时是否清除颜色
        bool ClearDepthOnLoad = true;                    ///< 加载时是否清除深度

        FramebufferAttachmentSpecification Attachments; ///< 附件规格配置
        uint32_t Samples = 1;                           ///< 多重采样数量（抗锯齿）

        // TODO: Temp, needs scale
        bool NoResize = false; ///< 是否禁用尺寸调整

        // 主开关（单个附件可以在FramebufferTextureSpecification中禁用）
        bool Blend = true; ///< 是否启用混合
        // None表示使用FramebufferTextureSpecification中的BlendMode
        FramebufferBlendMode BlendMode = FramebufferBlendMode::None; ///< 全局混合模式

        // SwapChainTarget = 屏幕缓冲区（即无帧缓冲区）
        bool SwapChainTarget = false; ///< 是否为交换链目标

        // 是否用于传输操作？
        bool Transfer = false; ///< 是否用于传输操作

        // 注意：这些用于附加多层颜色/深度图像
        Ref<Image2D> ExistingImage;                ///< 现有图像对象
        std::vector<uint32_t> ExistingImageLayers; ///< 现有图像层索引

        // 指定要附加的现有图像，而不是创建新图像
        // 附件索引 -> 图像
        std::map<uint32_t, Ref<Image2D>> ExistingImages; ///< 现有图像映射

        // 目前这将只是用现有帧缓冲区创建一个新的渲染通道
        Ref<Framebuffer> ExistingFramebuffer; ///< 现有帧缓冲区

        std::string DebugName; ///< 调试名称
    };

    /**
     * @class Framebuffer
     * @brief 帧缓冲区抽象基类
     *
     * 这个类提供了帧缓冲区的统一接口，封装了渲染目标的管理和操作。
     * 帧缓冲区是现代渲染管线中的核心组件，用于离屏渲染、后处理效果
     * 和多通道渲染。
     *
     * 主要功能包括：
     * - 渲染目标管理：支持多个颜色附件和深度附件
     * - 动态调整：支持运行时调整尺寸和重新创建
     * - 纹理绑定：将附件作为纹理绑定到着色器
     * - 多重采样：支持MSAA抗锯齿
     * - 回调机制：支持尺寸调整回调
     *
     * 使用场景：
     * - 离屏渲染：阴影映射、反射、折射
     * - 后处理效果：模糊、色调映射、颜色校正
     * - 延迟渲染：G-Buffer管理
     * - 多通道渲染：多个渲染目标同时输出
     *
     * 生命周期：
     * 1. 创建：使用FramebufferSpecification创建实例
     * 2. 绑定：绑定为当前渲染目标
     * 3. 渲染：执行渲染操作
     * 4. 解绑：恢复默认渲染目标
     * 5. 使用：将附件作为纹理使用
     *
     * 具体的实现由各个图形API的派生类提供（如VulkanFramebuffer）。
     */
    class Framebuffer : public RefCounted
    {
    public:
        /**
         * @brief 虚析构函数
         *
         * 确保派生类能够正确清理帧缓冲区资源。
         */
        virtual ~Framebuffer() {}

        /**
         * @brief 绑定帧缓冲区
         *
         * 将此帧缓冲区设置为当前的渲染目标。后续的渲染操作
         * 将输出到此帧缓冲区的附件中。
         */
        virtual void Bind() const = 0;

        /**
         * @brief 解绑帧缓冲区
         *
         * 恢复默认的渲染目标（通常是屏幕缓冲区）。
         */
        virtual void Unbind() const = 0;

        /**
         * @brief 调整帧缓冲区尺寸
         * @param width 新的宽度
         * @param height 新的高度
         * @param forceRecreate 是否强制重新创建，默认为false
         *
         * 调整帧缓冲区及其所有附件的尺寸。这可能涉及重新分配
         * GPU内存和重新创建渲染资源。
         *
         * @note 调整尺寸是一个代价较高的操作
         */
        virtual void Resize(uint32_t width, uint32_t height, bool forceRecreate = false) = 0;

        /**
         * @brief 添加尺寸调整回调
         * @param func 回调函数，接收调整后的帧缓冲区作为参数
         *
         * 注册一个回调函数，当帧缓冲区尺寸发生变化时会被调用。
         * 这对于需要响应帧缓冲区尺寸变化的系统很有用。
         */
        virtual void AddResizeCallback(const std::function<void(Ref<Framebuffer>)> &func) = 0;

        /**
         * @brief 绑定纹理附件
         * @param attachmentIndex 附件索引，默认为0
         * @param slot 纹理槽位，默认为0
         *
         * 将指定的附件作为纹理绑定到指定的纹理槽位，
         * 用于在着色器中采样。
         */
        virtual void BindTexture(uint32_t attachmentIndex = 0, uint32_t slot = 0) const = 0;

        /**
         * @brief 获取帧缓冲区宽度
         * @return 帧缓冲区宽度（像素）
         */
        virtual uint32_t GetWidth() const = 0;

        /**
         * @brief 获取帧缓冲区高度
         * @return 帧缓冲区高度（像素）
         */
        virtual uint32_t GetHeight() const = 0;

        /**
         * @brief 获取渲染器ID
         * @return 渲染器特定的ID
         *
         * 返回底层图形API特定的帧缓冲区标识符。
         */
        virtual RendererID GetRendererID() const = 0;

        /**
         * @brief 获取图像附件
         * @param attachmentIndex 附件索引，默认为0
         * @return 指定索引的图像附件
         *
         * 返回指定索引的颜色附件图像对象。
         */
        virtual Ref<Image2D> GetImage(uint32_t attachmentIndex = 0) const = 0;

        /**
         * @brief 获取颜色附件数量
         * @return 颜色附件的数量
         */
        virtual size_t GetColorAttachmentCount() const = 0;

        /**
         * @brief 检查是否有深度附件
         * @return 如果有深度附件则返回true
         */
        virtual bool HasDepthAttachment() const = 0;

        /**
         * @brief 获取深度图像附件
         * @return 深度附件图像对象
         *
         * 返回深度/模板附件的图像对象。
         */
        virtual Ref<Image2D> GetDepthImage() const = 0;

        /**
         * @brief 获取帧缓冲区规格配置
         * @return 帧缓冲区规格配置的常量引用
         *
         * 返回创建此帧缓冲区时使用的配置信息。
         */
        virtual const FramebufferSpecification &GetSpecification() const = 0;

        /**
         * @brief 创建帧缓冲区实例
         * @param spec 帧缓冲区规格配置
         * @return 创建的帧缓冲区实例智能指针
         *
         * 根据指定的规格创建一个新的帧缓冲区实例。
         * 具体的实现由当前活动的渲染器API决定。
         */
        static Ref<Framebuffer> Create(const FramebufferSpecification &spec);
    };
}

#endif