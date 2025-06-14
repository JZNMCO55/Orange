#ifndef PIPELINE_H
#define PIPELINE_H

#include "Core/Base/Ref.h"
#include "Shader.h"
#include "VertexBuffer.h"
#include "Framebuffer.h"
#include "UniformBuffer.h"

namespace Orange
{
    /**
     * @enum PrimitiveTopology
     * @brief 图元拓扑类型枚举
     *
     * 定义了渲染时使用的几何图元类型，决定了顶点数据如何被解释和连接。
     */
    enum class PrimitiveTopology
    {
        None = 0,      ///< 无图元类型
        Points,        ///< 点图元
        Lines,         ///< 线段图元
        Triangles,     ///< 三角形图元
        LineStrip,     ///< 线条带图元
        TriangleStrip, ///< 三角形带图元
        TriangleFan    ///< 三角形扇图元
    };

    /**
     * @enum DepthCompareOperator
     * @brief 深度比较操作符枚举
     *
     * 定义了深度测试时使用的比较操作，用于确定片段是否应该被渲染。
     */
    enum class DepthCompareOperator
    {
        None = 0,       ///< 无深度比较
        Never,          ///< 永不通过
        NotEqual,       ///< 不等于
        Less,           ///< 小于
        LessOrEqual,    ///< 小于等于
        Greater,        ///< 大于
        GreaterOrEqual, ///< 大于等于
        Equal,          ///< 等于
        Always,         ///< 总是通过
    };

    /**
     * @struct PipelineSpecification
     * @brief 渲染管线规格配置结构体
     *
     * 包含了创建渲染管线所需的所有配置参数，包括着色器、顶点布局、
     * 渲染状态等设置。
     */
    struct PipelineSpecification
    {
        Ref<Shader> Shader;                                                        ///< 使用的着色器程序
        Ref<Framebuffer> TargetFramebuffer;                                        ///< 目标帧缓冲区
        VertexBufferLayout Layout;                                                 ///< 顶点缓冲区布局
        VertexBufferLayout InstanceLayout;                                         ///< 实例化数据布局
        VertexBufferLayout BoneInfluenceLayout;                                    ///< 骨骼影响数据布局
        PrimitiveTopology Topology = PrimitiveTopology::Triangles;                 ///< 图元拓扑类型（默认三角形）
        DepthCompareOperator DepthOperator = DepthCompareOperator::GreaterOrEqual; ///< 深度比较操作符
        bool BackfaceCulling = true;                                               ///< 是否启用背面剔除
        bool DepthTest = true;                                                     ///< 是否启用深度测试
        bool DepthWrite = true;                                                    ///< 是否启用深度写入
        bool Wireframe = false;                                                    ///< 是否启用线框模式
        float LineWidth = 1.0f;                                                    ///< 线宽（线框模式下使用）

        std::string DebugName; ///< 调试名称
    };

    /**
     * @struct PipelineStatistics
     * @brief 渲染管线统计信息结构体
     *
     * 包含了渲染管线执行过程中的各种统计数据，用于性能分析和调试。
     */
    struct PipelineStatistics
    {
        uint64_t InputAssemblyVertices = 0;     ///< 输入装配阶段处理的顶点数
        uint64_t InputAssemblyPrimitives = 0;   ///< 输入装配阶段处理的图元数
        uint64_t VertexShaderInvocations = 0;   ///< 顶点着色器调用次数
        uint64_t ClippingInvocations = 0;       ///< 裁剪阶段调用次数
        uint64_t ClippingPrimitives = 0;        ///< 裁剪阶段处理的图元数
        uint64_t FragmentShaderInvocations = 0; ///< 片段着色器调用次数
        uint64_t ComputeShaderInvocations = 0;  ///< 计算着色器调用次数

        // TODO: 当我们有细分着色器时添加细分着色器统计
    };

    /**
     * @enum PipelineStage
     * @brief 渲染管线阶段标志枚举
     *
     * 与Vulkan的VkPipelineStageFlagBits相同，定义了渲染管线的各个执行阶段。
     * 注意：这是一个位域枚举，可以进行位运算组合。
     */
    enum class PipelineStage
    {
        None = 0,                                 ///< 无阶段
        TopOfPipe = 0x00000001,                   ///< 管线顶部
        DrawIndirect = 0x00000002,                ///< 间接绘制
        VertexInput = 0x00000004,                 ///< 顶点输入
        VertexShader = 0x00000008,                ///< 顶点着色器
        TesselationControlShader = 0x00000010,    ///< 细分控制着色器
        TesselationEvaluationShader = 0x00000020, ///< 细分评估着色器
        GeometryShader = 0x00000040,              ///< 几何着色器
        FragmentShader = 0x00000080,              ///< 片段着色器
        EarlyFragmentTests = 0x00000100,          ///< 早期片段测试
        LateFragmentTests = 0x00000200,           ///< 后期片段测试
        ColorAttachmentOutput = 0x00000400,       ///< 颜色附件输出
        ComputeShader = 0x00000800,               ///< 计算着色器
        Transfer = 0x00001000,                    ///< 传输操作
        BottomOfPipe = 0x00002000,                ///< 管线底部
        Host = 0x00004000,                        ///< 主机端
        AllGraphics = 0x00008000,                 ///< 所有图形阶段
        AllCommands = 0x00010000                  ///< 所有命令
    };

    /**
     * @enum ResourceAccessFlags
     * @brief 资源访问标志枚举
     *
     * 与Vulkan的VkAccessFlagBits相同，定义了资源的访问类型。
     * 注意：这是一个位域枚举，可以进行位运算组合。
     */
    enum class ResourceAccessFlags
    {
        None = 0,                                 ///< 无访问
        IndirectCommandRead = 0x00000001,         ///< 间接命令读取
        IndexRead = 0x00000002,                   ///< 索引读取
        VertexAttributeRead = 0x00000004,         ///< 顶点属性读取
        UniformRead = 0x00000008,                 ///< 统一缓冲区读取
        InputAttachmentRead = 0x00000010,         ///< 输入附件读取
        ShaderRead = 0x00000020,                  ///< 着色器读取
        ShaderWrite = 0x00000040,                 ///< 着色器写入
        ColorAttachmentRead = 0x00000080,         ///< 颜色附件读取
        ColorAttachmentWrite = 0x00000100,        ///< 颜色附件写入
        DepthStencilAttachmentRead = 0x00000200,  ///< 深度模板附件读取
        DepthStencilAttachmentWrite = 0x00000400, ///< 深度模板附件写入
        TransferRead = 0x00000800,                ///< 传输读取
        TransferWrite = 0x00001000,               ///< 传输写入
        HostRead = 0x00002000,                    ///< 主机读取
        HostWrite = 0x00004000,                   ///< 主机写入
        MemoryRead = 0x00008000,                  ///< 内存读取
        MemoryWrite = 0x00010000,                 ///< 内存写入
    };

    /**
     * @class Pipeline
     * @brief 渲染管线抽象基类
     *
     * 这个类提供了渲染管线的统一接口，封装了图形渲染管线的创建、配置和管理。
     * 渲染管线定义了从顶点数据到最终像素输出的完整渲染流程，包括：
     * - 顶点处理和变换
     * - 图元装配和光栅化
     * - 片段着色和混合
     * - 深度测试和模板测试
     *
     * 具体的实现由各个图形API的派生类提供（如VulkanPipeline）。
     */
    class Pipeline : public RefCounted
    {
    public:
        /**
         * @brief 虚析构函数
         *
         * 确保派生类能够正确清理资源。
         */
        virtual ~Pipeline() = default;

        /**
         * @brief 获取管线规格配置（可修改版本）
         * @return 管线规格配置的引用
         *
         * 返回当前管线的配置规格，允许修改配置参数。
         */
        virtual PipelineSpecification &GetSpecification() = 0;

        /**
         * @brief 获取管线规格配置（只读版本）
         * @return 管线规格配置的常量引用
         *
         * 返回当前管线的配置规格，只允许读取不允许修改。
         */
        virtual const PipelineSpecification &GetSpecification() const = 0;

        /**
         * @brief 使管线失效并重新创建
         *
         * 当管线配置发生变化时，调用此方法重新创建底层的图形API管线对象。
         * 这通常在着色器重新编译或渲染状态改变时需要调用。
         */
        virtual void Invalidate() = 0;

        /**
         * @brief 获取管线使用的着色器
         * @return 着色器的智能指针
         *
         * 返回当前管线绑定的着色器程序。
         */
        virtual Ref<Shader> GetShader() const = 0;

        /**
         * @brief 创建渲染管线实例
         * @param spec 管线规格配置
         * @return 创建的管线实例智能指针
         *
         * 根据指定的配置创建一个新的渲染管线实例。
         * 具体的实现类型取决于当前使用的渲染API。
         */
        static Ref<Pipeline> Create(const PipelineSpecification &spec);
    };
}

#endif