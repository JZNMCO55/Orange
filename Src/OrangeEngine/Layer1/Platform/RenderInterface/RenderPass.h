#ifndef RENDER_PASS_H
#define RENDER_PASS_H

#include "Core/Base/Ref.h"
#include "Core/Base/Base.h"
#include "Framebuffer.h"
#include "UniformBufferSet.h"
#include "StorageBufferSet.h"
#include "Texture.h"
#include "Pipeline.h"

namespace Orange
{
    /**
     * @struct RenderPassSpecification
     * @brief 渲染通道规格配置结构体
     *
     * 定义了创建渲染通道所需的配置参数，包括关联的渲染管线、
     * 调试信息和性能标记。
     */
    struct RenderPassSpecification
    {
        Ref<Pipeline> Pipeline; ///< 关联的渲染管线，定义渲染状态和着色器
        std::string DebugName;  ///< 调试名称，用于调试器和性能分析工具
        glm::vec4 MarkerColor;  ///< 标记颜色，用于GPU调试器中的可视化标识
    };

    /**
     * @class RenderPass
     * @brief 渲染通道抽象基类
     *
     * 这个类提供了渲染通道的统一接口，封装了一组相关的渲染操作。
     * 渲染通道管理输入资源、输出目标和渲染状态，是现代渲染架构中
     * 的核心组件。
     *
     * 渲染通道的生命周期：
     * 1. 创建：使用RenderPassSpecification创建实例
     * 2. 配置：设置输入资源（缓冲区、纹理等）
     * 3. 验证：检查配置的有效性
     * 4. 烘焙：预处理和优化渲染状态
     * 5. 准备：为渲染做最终准备
     * 6. 执行：在渲染循环中使用
     *
     * 支持的输入资源类型：
     * - 统一缓冲区（UniformBuffer/UniformBufferSet）
     * - 存储缓冲区（StorageBuffer/StorageBufferSet）
     * - 2D纹理（Texture2D）
     * - 立方体纹理（TextureCube）
     * - 2D图像（Image2D）
     *
     * 具体的实现由各个图形API的派生类提供（如VulkanRenderPass）。
     */
    class RenderPass : public RefCounted
    {
    public:
        /**
         * @brief 虚析构函数
         *
         * 确保派生类能够正确清理渲染资源。
         */
        virtual ~RenderPass() = default;

        /**
         * @brief 获取渲染通道规格配置（可修改）
         * @return 渲染通道规格配置的引用
         *
         * 返回渲染通道的配置信息，允许运行时修改。
         */
        virtual RenderPassSpecification &GetSpecification() = 0;

        /**
         * @brief 获取渲染通道规格配置（只读）
         * @return 渲染通道规格配置的常量引用
         *
         * 返回渲染通道的配置信息，只读访问。
         */
        virtual const RenderPassSpecification &GetSpecification() const = 0;

        /**
         * @brief 设置统一缓冲区集合输入
         * @param name 输入资源名称，对应着色器中的绑定点
         * @param uniformBufferSet 统一缓冲区集合对象
         *
         * 将统一缓冲区集合绑定到指定的输入槽。统一缓冲区集合
         * 通常用于管理多帧缓冲的统一变量数据。
         */
        virtual void SetInput(std::string_view name, Ref<UniformBufferSet> uniformBufferSet) = 0;

        /**
         * @brief 设置统一缓冲区输入
         * @param name 输入资源名称，对应着色器中的绑定点
         * @param uniformBuffer 统一缓冲区对象
         *
         * 将单个统一缓冲区绑定到指定的输入槽。
         */
        virtual void SetInput(std::string_view name, Ref<UniformBuffer> uniformBuffer) = 0;

        /**
         * @brief 设置存储缓冲区集合输入
         * @param name 输入资源名称，对应着色器中的绑定点
         * @param storageBufferSet 存储缓冲区集合对象
         *
         * 将存储缓冲区集合绑定到指定的输入槽。存储缓冲区集合
         * 用于管理可读写的结构化数据。
         */
        virtual void SetInput(std::string_view name, Ref<StorageBufferSet> storageBufferSet) = 0;

        /**
         * @brief 设置存储缓冲区输入
         * @param name 输入资源名称，对应着色器中的绑定点
         * @param storageBuffer 存储缓冲区对象
         *
         * 将单个存储缓冲区绑定到指定的输入槽。
         */
        virtual void SetInput(std::string_view name, Ref<StorageBuffer> storageBuffer) = 0;

        /**
         * @brief 设置2D纹理输入
         * @param name 输入资源名称，对应着色器中的采样器
         * @param texture 2D纹理对象
         *
         * 将2D纹理绑定到指定的采样器槽。
         */
        virtual void SetInput(std::string_view name, Ref<Texture2D> texture) = 0;

        /**
         * @brief 设置立方体纹理输入
         * @param name 输入资源名称，对应着色器中的立方体采样器
         * @param textureCube 立方体纹理对象
         *
         * 将立方体纹理绑定到指定的立方体采样器槽。
         */
        virtual void SetInput(std::string_view name, Ref<TextureCube> textureCube) = 0;

        /**
         * @brief 设置2D图像输入
         * @param name 输入资源名称，对应着色器中的图像单元
         * @param image 2D图像对象
         *
         * 将2D图像绑定到指定的图像单元，用于计算着色器的读写操作。
         */
        virtual void SetInput(std::string_view name, Ref<Image2D> image) = 0;

        /**
         * @brief 获取输出图像
         * @param index 输出附件索引
         * @return 指定索引的输出图像对象
         *
         * 返回渲染通道指定索引的颜色输出附件。
         */
        virtual Ref<Image2D> GetOutput(uint32_t index) = 0;

        /**
         * @brief 获取深度输出
         * @return 深度输出图像对象
         *
         * 返回渲染通道的深度/模板输出附件。
         */
        virtual Ref<Image2D> GetDepthOutput() = 0;

        /**
         * @brief 获取第一个描述符集索引
         * @return 第一个描述符集的索引
         *
         * 返回此渲染通道使用的第一个描述符集索引，
         * 用于Vulkan等API的描述符集管理。
         */
        virtual uint32_t GetFirstSetIndex() const = 0;

        /**
         * @brief 获取关联的渲染管线
         * @return 渲染管线对象智能指针
         *
         * 返回渲染通道当前关联的渲染管线。
         */
        virtual Ref<Pipeline> GetPipeline() const = 0;

        /**
         * @brief 获取目标帧缓冲区
         * @return 帧缓冲区对象智能指针
         *
         * 返回渲染通道的目标帧缓冲区，包含所有输出附件。
         */
        virtual Ref<Framebuffer> GetTargetFramebuffer() const = 0;

        /**
         * @brief 验证渲染通道配置
         * @return 如果配置有效则返回true
         *
         * 检查渲染通道的配置是否有效，包括：
         * - 所有必需的输入资源是否已设置
         * - 输入资源格式是否与着色器兼容
         * - 输出附件配置是否正确
         */
        virtual bool Validate() = 0;

        /**
         * @brief 烘焙渲染通道
         *
         * 预处理和优化渲染通道的状态，包括：
         * - 创建描述符集布局
         * - 分配和更新描述符集
         * - 优化渲染状态转换
         *
         * 烘焙操作通常在渲染循环开始前执行一次。
         */
        virtual void Bake() = 0;

        /**
         * @brief 检查是否已烘焙
         * @return 如果渲染通道已烘焙则返回true
         *
         * 检查渲染通道是否已完成烘焙过程。
         */
        virtual bool Baked() const = 0;

        /**
         * @brief 准备渲染通道
         *
         * 为即将到来的渲染操作做最终准备，包括：
         * - 更新动态资源
         * - 设置渲染状态
         * - 准备命令缓冲区
         *
         * 此方法在每帧渲染前调用。
         */
        virtual void Prepare() = 0;

        /**
         * @brief 创建渲染通道实例
         * @param spec 渲染通道规格配置
         * @return 创建的渲染通道实例智能指针
         *
         * 根据指定的规格创建一个新的渲染通道实例。
         * 具体的实现由当前活动的渲染器API决定。
         */
        static Ref<RenderPass> Create(const RenderPassSpecification &spec);
    };
}

#endif