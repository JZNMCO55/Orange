#ifndef RENDERER_API_H
#define RENDERER_API_H

#include "Core/Base/Ref.h"
#include "RendererCapabilities.h"
#include "RenderCommandBuffer.h"
#include "StorageBufferSet.h"
#include "UniformBufferSet.h"
#include "PipelineCompute.h"
#include "ComputePass.h"
#include "RenderPass.h"
#include "RendererTypes.h"
#include "Material.h"

namespace Orange
{
    /**
     * @enum RendererAPIType
     * @brief 支持的渲染API类型枚举
     *
     * 定义了引擎支持的不同渲染后端。
     * 目前支持Vulkan作为主要渲染API。
     */
    enum class RendererAPIType
    {
        None,  ///< 未选择渲染API
        Vulkan ///< Vulkan渲染API
    };

    /**
     * @enum PrimitiveType
     * @brief 渲染图元类型枚举
     *
     * 定义了可以渲染的不同类型的几何图元。
     */
    enum class PrimitiveType
    {
        None = 0,  ///< 无图元类型
        Triangles, ///< 三角形图元
        Lines      ///< 线段图元
    };

    /**
     * @class RendererAPI
     * @brief 渲染API实现的抽象基类
     *
     * 此类为不同的渲染后端提供统一接口。
     * 它定义了所有必须由具体渲染器实现（如VulkanRendererAPI）实现的基本渲染操作。
     *
     * 该类处理：
     * - 渲染系统的初始化和关闭
     * - 帧管理（开始/结束帧）
     * - 用于调试的GPU性能标记
     * - 渲染通道和计算通道管理
     * - 网格和几何体渲染
     * - 环境映射和光照
     * - 图像操作（清除、复制、传输）
     */
    class RendererAPI
    {
    public:
        /**
         * @brief 初始化渲染API
         *
         * 执行渲染后端的所有必要初始化。
         * 应在应用程序启动时调用一次。
         */
        virtual void Init() = 0;

        /**
         * @brief 关闭渲染API
         *
         * 清理所有资源并关闭渲染后端。
         * 应在应用程序关闭时调用一次。
         */
        virtual void Shutdown() = 0;

        /**
         * @brief 开始新帧
         *
         * 为新的渲染帧准备渲染器。
         * 应在每帧开始时调用。
         */
        virtual void BeginFrame() = 0;

        /**
         * @brief 结束当前帧
         *
         * 完成当前帧并呈现它。
         * 应在每帧结束时调用。
         */
        virtual void EndFrame() = 0;

        /**
         * @brief 开始渲染通道
         * @param renderCommandBuffer 要开始渲染通道的命令缓冲区
         * @param renderPass 要开始的渲染通道
         * @param explicitClear 是否显式清除渲染目标
         *
         * 使用指定配置开始渲染通道。
         */
        virtual void BeginRenderPass(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<RenderPass> renderPass, bool explicitClear = false) = 0;

        /**
         * @brief 结束当前渲染通道
         * @param renderCommandBuffer 要结束渲染通道的命令缓冲区
         *
         * 结束当前活动的渲染通道。
         */
        virtual void EndRenderPass(Ref<RenderCommandBuffer> renderCommandBuffer) = 0;

        /**
         * @brief 渲染四边形图元
         * @param renderCommandBuffer 要渲染到的命令缓冲区
         * @param pipeline 渲染管线
         * @param material 要应用的材质
         * @param transform 变换矩阵
         *
         * 使用指定变换渲染简单的四边形图元。
         */
        virtual void RenderQuad(Ref<RenderCommandBuffer> renderCommandBuffer,
                                Ref<Pipeline> pipeline, Ref<Material> material,
                                const glm::mat4 &transform) = 0;
#ifdef TODO
#pragma region TODO: 计算通道相关
        /**
         * @brief 开始计算通道
         * @param renderCommandBuffer 要开始计算通道的命令缓冲区
         * @param computePass 要开始的计算通道
         *
         * 为GPU计算操作开始计算通道。
         */
        // virtual void BeginComputePass(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<ComputePass> computePass) = 0;

        /**
         * @brief 结束计算通道
         * @param renderCommandBuffer 要结束计算通道的命令缓冲区
         * @param computePass 要结束的计算通道
         *
         * 结束当前活动的计算通道。
         */
        // virtual void EndComputePass(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<ComputePass> computePass) = 0;

        /**
         * @brief 分派计算着色器
         * @param renderCommandBuffer 要分派的命令缓冲区
         * @param computePass 计算通道上下文
         * @param material 包含计算着色器的材质
         * @param workGroups 要分派的工作组数量 (x, y, z)
         * @param constants 可选的常量缓冲区数据
         *
         * 使用指定的工作组维度分派计算着色器。
         */
        // virtual void DispatchCompute(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<ComputePass> computePass, Ref<Material> material, const glm::uvec3 &workGroups, Buffer constants = Buffer()) = 0;

#pragma endregion

#pragma region TODO: 各种渲染
        /**
         * @brief 提交全屏四边形进行渲染
         * @param renderCommandBuffer 要提交到的命令缓冲区
         * @param pipeline 要使用的渲染管线
         * @param material 要应用的材质
         *
         * 渲染全屏四边形，通常用于后处理效果。
         */
        virtual void SubmitFullscreenQuad(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Material> material) = 0;

        /**
         * @brief 提交带着色器覆盖的全屏四边形
         * @param renderCommandBuffer 要提交到的命令缓冲区
         * @param pipeline 要使用的渲染管线
         * @param material 要应用的材质
         * @param vertexShaderOverrides 顶点着色器的覆盖数据
         * @param fragmentShaderOverrides 片段着色器的覆盖数据
         *
         * 使用自定义着色器参数覆盖渲染全屏四边形。
         */
        virtual void SubmitFullscreenQuadWithOverrides(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Material> material, Buffer vertexShaderOverrides, Buffer fragmentShaderOverrides) = 0;

        /**
         * @brief 设置场景环境
         * @param sceneRenderer 场景渲染器实例
         * @param environment 环境贴图
         * @param shadow 阴影贴图纹理
         * @param spotShadow 聚光灯阴影贴图纹理
         *
         * 配置场景渲染的环境设置，包括环境贴图和阴影贴图。
         */
        virtual void SetSceneEnvironment(Ref<SceneRenderer> sceneRenderer,
                                         Ref<Environment> environment,
                                         Ref<Image2D> shadow,
                                         Ref<Image2D> spotShadow) = 0;

        /**
         * @brief 从HDR文件创建环境贴图
         * @param filepath HDR环境文件的路径
         * @return 环境和辐照度立方体贴图的配对
         *
         * 从HDR图像文件创建环境和辐照度立方体贴图。
         */
        virtual std::pair<Ref<TextureCube>, Ref<TextureCube>> CreateEnvironmentMap(const std::string &filepath) = 0;

        /**
         * @brief 创建Preetham天空模型
         * @param turbidity 大气浑浊度
         * @param azimuth 太阳方位角
         * @param inclination 太阳倾斜角
         * @return 生成的天空立方体贴图
         *
         * 使用Preetham大气散射模型生成程序化天空。
         */
        virtual Ref<TextureCube> CreatePreethamSky(float turbidity,
                                                   float azimuth,
                                                   float inclination) = 0;

        /**
         * @brief 渲染静态网格
         * @param renderCommandBuffer 要渲染到的命令缓冲区
         * @param pipeline 渲染管线
         * @param mesh 要渲染的静态网格
         * @param meshSource 网格数据源
         * @param submeshIndex 要渲染的子网格索引
         * @param materialTable 网格的材质表
         * @param transformBuffer 包含变换矩阵的缓冲区
         * @param transformOffset 变换缓冲区的偏移量
         * @param instanceCount 要渲染的实例数量
         *
         * 使用实例化支持渲染静态网格。
         */
        virtual void RenderStaticMesh(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<StaticMesh> mesh, Ref<MeshSource> meshSource, uint32_t submeshIndex, Ref<MaterialTable> materialTable, Ref<VertexBuffer> transformBuffer, uint32_t transformOffset, uint32_t instanceCount) = 0;

        /**
         * @brief 使用实例化渲染子网格
         * @param renderCommandBuffer 要渲染到的命令缓冲区
         * @param pipeline 渲染管线
         * @param mesh 要渲染的网格
         * @param meshSource 网格数据源
         * @param index 子网格索引
         * @param materialTable 材质表
         * @param transformBuffer 变换缓冲区
         * @param transformOffset 变换缓冲区偏移量
         * @param boneTransformsOffset 骨骼变换缓冲区偏移量
         * @param boneTransformStride 骨骼变换之间的步长
         * @param instanceCount 实例数量
         *
         * 渲染支持骨骼动画和实例化的子网格。
         */
        virtual void RenderSubmeshInstanced(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Mesh> mesh, Ref<MeshSource> meshSource, uint32_t index, Ref<MaterialTable> materialTable, Ref<VertexBuffer> transformBuffer, uint32_t transformOffset, uint32_t boneTransformsOffset, uint32_t boneTransformStride, uint32_t instanceCount) = 0;

        /**
         * @brief 使用特定材质渲染网格
         * @param renderCommandBuffer 要渲染到的命令缓冲区
         * @param pipeline 渲染管线
         * @param mesh 要渲染的网格
         * @param meshSource 网格数据源
         * @param submeshIndex 子网格索引
         * @param material 要使用的材质
         * @param transformBuffer 变换缓冲区
         * @param transformOffset 变换缓冲区偏移量
         * @param boneTransformsOffset 骨骼变换缓冲区偏移量
         * @param boneTransformStride 骨骼变换之间的步长
         * @param instanceCount 实例数量
         * @param additionalUniforms 附加统一数据
         *
         * 使用特定材质覆盖和可选附加统一数据渲染网格。
         */
        virtual void RenderMeshWithMaterial(Ref<RenderCommandBuffer> renderCommandBuffer,
                                            Ref<Pipeline> pipeline,
                                            Ref<Mesh> mesh,
                                            Ref<MeshSource> meshSource,
                                            uint32_t submeshIndex,
                                            Ref<Material> material,
                                            Ref<VertexBuffer> transformBuffer,
                                            uint32_t transformOffset,
                                            uint32_t boneTransformsOffset,
                                            uint32_t boneTransformStride,
                                            uint32_t instanceCount,
                                            Buffer additionalUniforms = Buffer()) = 0;

        /**
         * @brief 使用特定材质渲染静态网格
         * @param renderCommandBuffer 要渲染到的命令缓冲区
         * @param pipeline 渲染管线
         * @param staticMesh 要渲染的静态网格
         * @param meshSource 网格数据源
         * @param submeshIndex 子网格索引
         * @param material 要使用的材质
         * @param transformBuffer 变换缓冲区
         * @param transformOffset 变换缓冲区偏移量
         * @param instanceCount 实例数量
         * @param additionalUniforms 附加统一数据
         *
         * 使用特定材质覆盖渲染静态网格。
         */
        virtual void RenderStaticMeshWithMaterial(Ref<RenderCommandBuffer> renderCommandBuffer,
                                                  Ref<Pipeline> pipeline,
                                                  Ref<StaticMesh> staticMesh,
                                                  Ref<MeshSource> meshSource,
                                                  uint32_t submeshIndex,
                                                  Ref<Material> material,
                                                  Ref<VertexBuffer> transformBuffer,
                                                  uint32_t transformOffset,
                                                  uint32_t instanceCount,
                                                  Buffer additionalUniforms = Buffer()) = 0;

        /**
         * @brief 渲染任意几何体
         * @param renderCommandBuffer 要渲染到的命令缓冲区
         * @param pipeline 渲染管线
         * @param material 要应用的材质
         * @param vertexBuffer 顶点数据缓冲区
         * @param indexBuffer 索引数据缓冲区
         * @param transform 变换矩阵
         * @param indexCount 要渲染的索引数量（0 = 全部）
         *
         * 渲染由顶点和索引缓冲区定义的任意几何体。
         */
        virtual void RenderGeometry(Ref<RenderCommandBuffer> renderCommandBuffer,
                                    Ref<Pipeline> pipeline,
                                    Ref<Material> material,
                                    Ref<VertexBuffer> vertexBuffer,
                                    Ref<IndexBuffer> indexBuffer,
                                    const glm::mat4 &transform,
                                    uint32_t indexCount = 0) = 0;
#pragma endregion

#pragma region TODO: 添加GPU性能标记
        /**
         * @brief 插入GPU性能标记
         * @param renderCommandBuffer 要插入标记的命令缓冲区
         * @param label 标记的标签文本
         * @param color 标记可视化的颜色
         *
         * 为GPU调试和性能分析插入单个性能标记。
         */
        // virtual void InsertGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer, const std::string &label, const glm::vec4 &color) = 0;

        /**
         * @brief 开始GPU性能标记区域
         * @param renderCommandBuffer 要开始标记的命令缓冲区
         * @param label 标记区域的标签文本
         * @param markerColor 标记可视化的颜色
         *
         * 开始用于GPU调试和性能分析的性能标记区域。
         * 必须与EndGPUPerfMarker()配对使用。
         */
        // virtual void BeginGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer, const std::string &label, const glm::vec4 &markerColor) = 0;

        /**
         * @brief 结束GPU性能标记区域
         * @param renderCommandBuffer 要结束标记的命令缓冲区
         *
         * 结束由BeginGPUPerfMarker()开始的性能标记区域。
         */
        // virtual void EndGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer) = 0;

        /**
         * @brief 插入GPU性能标记（渲染线程版本）
         * @param renderCommandBuffer 要插入标记的命令缓冲区
         * @param label 标记的标签文本
         * @param color 标记可视化的颜色
         *
         * InsertGPUPerfMarker的渲染线程版本，用于线程安全操作。
         */
        // virtual void RT_InsertGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer, const std::string &label, const glm::vec4 &color) = 0;

        /**
         * @brief 开始GPU性能标记区域（渲染线程版本）
         * @param renderCommandBuffer 要开始标记的命令缓冲区
         * @param label 标记区域的标签文本
         * @param markerColor 标记可视化的颜色
         *
         * BeginGPUPerfMarker的渲染线程版本，用于线程安全操作。
         */
        // virtual void RT_BeginGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer, const std::string &label, const glm::vec4 &markerColor) = 0;

        /**
         * @brief 结束GPU性能标记区域（渲染线程版本）
         * @param renderCommandBuffer 要结束标记的命令缓冲区
         *
         * EndGPUPerfMarker的渲染线程版本，用于线程安全操作。
         */
        // virtual void RT_EndGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer) = 0;
#pragma endregion
#endif // TODO
        /**
         * @brief 获取渲染器能力
         * @return 渲染器能力结构的引用
         *
         * 返回有关当前渲染器的能力和限制的信息。
         */
        virtual RendererCapabilities &GetCapabilities() = 0;

        /**
         * @brief 获取当前渲染API类型
         * @return 当前渲染API类型
         *
         * 返回当前活动的渲染API类型。
         */
        static RendererAPIType Current() { return sCurrentRendererAPI; }

        /**
         * @brief 设置渲染API类型
         * @param api 要设置的渲染API类型
         *
         * 设置活动的渲染API类型。应在初始化期间调用。
         */
        static void SetAPI(RendererAPIType api);

    private:
        /// 当前渲染API类型
        inline static RendererAPIType sCurrentRendererAPI = RendererAPIType::Vulkan;
    };
}

#endif