/**
 * @file IRenderCommandEncoder.h
 * @brief 渲染命令编码器接口定义
 */

#ifndef ORANGE_IRENDER_COMMAND_ENCODER_H
#define ORANGE_IRENDER_COMMAND_ENCODER_H

#include "CommandCommon.h"

namespace Orange
{
    namespace Graphics
    {
        // 前向声明
        class IRenderDevice;
        class IRenderCommandBuffer;
        class IRenderPipeline;
        class IRenderPass;
        class IFramebuffer;
        class IBuffer;
        class ITexture;
        class ITextureView;
        class ISampler;
        class IShader;
        class IShaderProgram;
        class IDescriptorSet;
        class IEvent;

        /**
         * @brief 基础命令编码器接口
         *
         * 提供所有命令编码器通用的功能
         */
        class ICommandEncoder
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~ICommandEncoder() = default;

            /**
             * @brief 获取编码器类型
             * @return 编码器类型
             */
            virtual CommandEncoderType GetType() const = 0;

            /**
             * @brief 获取所属命令缓冲区
             * @return 命令缓冲区
             */
            virtual IRenderCommandBuffer *GetCommandBuffer() const = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 结束编码
             * @return 是否成功结束
             */
            virtual bool End() = 0;

            /**
             * @brief 插入调试标记开始
             * @param markerName 标记名称
             * @param color 标记颜色，RGBA格式
             */
            virtual void BeginDebugMarker(const char *markerName, const float color[4] = nullptr) = 0;

            /**
             * @brief 插入调试标记结束
             */
            virtual void EndDebugMarker() = 0;

            /**
             * @brief 插入调试标记
             * @param markerName 标记名称
             * @param color 标记颜色，RGBA格式
             */
            virtual void InsertDebugMarker(const char *markerName, const float color[4] = nullptr) = 0;

            /**
             * @brief 管线屏障
             * @param srcStageMask 源管线阶段
             * @param dstStageMask 目标管线阶段
             * @param dependencyFlags 依赖标志
             */
            virtual void PipelineBarrier(
                PipelineStageFlags srcStageMask,
                PipelineStageFlags dstStageMask,
                DependencyFlags dependencyFlags = static_cast<DependencyFlags>(DependencyFlagBits::ByRegion)) = 0;

            /**
             * @brief 缓冲区内存屏障
             * @param buffer 缓冲区
             * @param srcStageMask 源管线阶段
             * @param dstStageMask 目标管线阶段
             * @param srcAccessMask 源访问掩码
             * @param dstAccessMask 目标访问掩码
             * @param offset 偏移
             * @param size 大小，0表示整个缓冲区
             */
            virtual void BufferMemoryBarrier(
                IBuffer *buffer,
                PipelineStageFlags srcStageMask,
                PipelineStageFlags dstStageMask,
                AccessFlags srcAccessMask,
                AccessFlags dstAccessMask,
                uint64_t offset = 0,
                uint64_t size = 0) = 0;

            /**
             * @brief 纹理内存屏障
             * @param texture 纹理
             * @param srcStageMask 源管线阶段
             * @param dstStageMask 目标管线阶段
             * @param srcAccessMask 源访问掩码
             * @param dstAccessMask 目标访问掩码
             * @param oldLayout 旧布局
             * @param newLayout 新布局
             * @param subresourceRange 子资源范围
             */
            virtual void TextureMemoryBarrier(
                ITexture *texture,
                PipelineStageFlags srcStageMask,
                PipelineStageFlags dstStageMask,
                AccessFlags srcAccessMask,
                AccessFlags dstAccessMask,
                ImageLayout oldLayout,
                ImageLayout newLayout,
                const TextureSubresourceRange &subresourceRange) = 0;

            /**
             * @brief 等待事件
             * @param event 事件
             * @param srcStageMask 源管线阶段
             * @param dstStageMask 目标管线阶段
             * @param dependencyFlags 依赖标志
             */
            virtual void WaitEvent(
                IEvent *event,
                PipelineStageFlags srcStageMask,
                PipelineStageFlags dstStageMask,
                DependencyFlags dependencyFlags = static_cast<DependencyFlags>(DependencyFlagBits::ByRegion)) = 0;

            /**
             * @brief 触发事件
             * @param event 事件
             * @param stageMask 管线阶段
             */
            virtual void SetEvent(IEvent *event, PipelineStageFlags stageMask) = 0;

            /**
             * @brief 重置事件
             * @param event 事件
             * @param stageMask 管线阶段
             */
            virtual void ResetEvent(IEvent *event, PipelineStageFlags stageMask) = 0;
        };

        /**
         * @brief 转移命令编码器接口
         *
         * 用于资源转移操作，如复制、清除、填充等
         */
        class ITransferCommandEncoder : public ICommandEncoder
        {
        public:
            /**
             * @brief 复制缓冲区
             * @param srcBuffer 源缓冲区
             * @param dstBuffer 目标缓冲区
             * @param regions 复制区域数组
             * @param regionCount 区域数量
             */
            virtual void CopyBuffer(
                IBuffer *srcBuffer,
                IBuffer *dstBuffer,
                const BufferCopyRegion *regions,
                uint32_t regionCount) = 0;

            /**
             * @brief 复制纹理
             * @param srcTexture 源纹理
             * @param srcLayout 源布局
             * @param dstTexture 目标纹理
             * @param dstLayout 目标布局
             * @param regions 复制区域数组
             * @param regionCount 区域数量
             */
            virtual void CopyTexture(
                ITexture *srcTexture,
                ImageLayout srcLayout,
                ITexture *dstTexture,
                ImageLayout dstLayout,
                const TextureCopyRegion *regions,
                uint32_t regionCount) = 0;

            /**
             * @brief 从缓冲区复制到纹理
             * @param srcBuffer 源缓冲区
             * @param dstTexture 目标纹理
             * @param dstLayout 目标布局
             * @param regions 复制区域数组
             * @param regionCount 区域数量
             */
            virtual void CopyBufferToTexture(
                IBuffer *srcBuffer,
                ITexture *dstTexture,
                ImageLayout dstLayout,
                const BufferTextureCopyRegion *regions,
                uint32_t regionCount) = 0;

            /**
             * @brief 从纹理复制到缓冲区
             * @param srcTexture 源纹理
             * @param srcLayout 源布局
             * @param dstBuffer 目标缓冲区
             * @param regions 复制区域数组
             * @param regionCount 区域数量
             */
            virtual void CopyTextureToBuffer(
                ITexture *srcTexture,
                ImageLayout srcLayout,
                IBuffer *dstBuffer,
                const BufferTextureCopyRegion *regions,
                uint32_t regionCount) = 0;

            /**
             * @brief 填充缓冲区
             * @param buffer 缓冲区
             * @param offset 偏移
             * @param size 大小
             * @param data 填充数据
             */
            virtual void FillBuffer(
                IBuffer *buffer,
                uint64_t offset,
                uint64_t size,
                uint32_t data) = 0;

            /**
             * @brief 更新缓冲区
             * @param buffer 缓冲区
             * @param offset 偏移
             * @param size 大小
             * @param data 更新数据
             */
            virtual void UpdateBuffer(
                IBuffer *buffer,
                uint64_t offset,
                uint64_t size,
                const void *data) = 0;

            /**
             * @brief 生成Mipmap
             * @param texture 纹理
             * @param srcLayout 源布局
             * @param dstLayout 目标布局
             * @param subresourceRange 子资源范围
             */
            virtual void GenerateMipmaps(
                ITexture *texture,
                ImageLayout srcLayout,
                ImageLayout dstLayout,
                const TextureSubresourceRange &subresourceRange) = 0;

            /**
             * @brief 解析多采样纹理
             * @param srcTexture 源纹理（多采样）
             * @param srcLayout 源布局
             * @param dstTexture 目标纹理（单采样）
             * @param dstLayout 目标布局
             * @param format 像素格式
             * @param regions 解析区域数组
             * @param regionCount 区域数量
             */
            virtual void ResolveTexture(
                ITexture *srcTexture,
                ImageLayout srcLayout,
                ITexture *dstTexture,
                ImageLayout dstLayout,
                PixelFormat format,
                const TextureResolveRegion *regions,
                uint32_t regionCount) = 0;
        };

        /**
         * @brief 计算命令编码器接口
         *
         * 用于计算着色器相关操作
         */
        class IComputeCommandEncoder : public ICommandEncoder
        {
        public:
            /**
             * @brief 绑定计算管线
             * @param pipeline 计算管线
             */
            virtual void BindPipeline(IRenderPipeline *pipeline) = 0;

            /**
             * @brief 绑定描述符集
             * @param layout 管线布局
             * @param descriptorSet 描述符集
             * @param firstSet 第一个集合索引
             * @param dynamicOffsets 动态偏移数组
             * @param dynamicOffsetCount 动态偏移数量
             */
            virtual void BindDescriptorSets(
                IRenderPipeline *pipeline,
                IDescriptorSet *descriptorSet,
                uint32_t firstSet = 0,
                const uint32_t *dynamicOffsets = nullptr,
                uint32_t dynamicOffsetCount = 0) = 0;

            /**
             * @brief 推送常量
             * @param pipeline 管线
             * @param stageFlags 着色器阶段
             * @param offset 偏移
             * @param size 大小
             * @param values 值
             */
            virtual void PushConstants(
                IRenderPipeline *pipeline,
                ShaderStageFlags stageFlags,
                uint32_t offset,
                uint32_t size,
                const void *values) = 0;

            /**
             * @brief 分派计算工作组
             * @param groupCountX X方向工作组数量
             * @param groupCountY Y方向工作组数量
             * @param groupCountZ Z方向工作组数量
             */
            virtual void Dispatch(
                uint32_t groupCountX,
                uint32_t groupCountY,
                uint32_t groupCountZ) = 0;

            /**
             * @brief 通过间接缓冲区分派计算工作组
             * @param buffer 间接缓冲区
             * @param offset 偏移
             */
            virtual void DispatchIndirect(
                IBuffer *buffer,
                uint64_t offset) = 0;
        };

        /**
         * @brief 图形命令编码器接口
         *
         * 用于图形渲染相关操作
         */
        class IGraphicsCommandEncoder : public ICommandEncoder
        {
        public:
            /**
             * @brief 开始渲染通道
             * @param renderPass 渲染通道
             * @param framebuffer 帧缓冲
             * @param renderArea 渲染区域
             * @param clearValues 清除值数组
             * @param clearValueCount 清除值数量
             * @param useSecondaryCommandBuffers 是否使用次要命令缓冲区
             */
            virtual void BeginRenderPass(
                IRenderPass *renderPass,
                IFramebuffer *framebuffer,
                const Rect2D &renderArea,
                const ClearValue *clearValues,
                uint32_t clearValueCount,
                bool useSecondaryCommandBuffers = false) = 0;

            /**
             * @brief 结束渲染通道
             */
            virtual void EndRenderPass() = 0;

            /**
             * @brief 下一个子通道
             */
            virtual void NextSubpass() = 0;

            /**
             * @brief 绑定图形管线
             * @param pipeline 图形管线
             */
            virtual void BindPipeline(IRenderPipeline *pipeline) = 0;

            /**
             * @brief 绑定描述符集
             * @param layout 管线布局
             * @param descriptorSet 描述符集
             * @param firstSet 第一个集合索引
             * @param dynamicOffsets 动态偏移数组
             * @param dynamicOffsetCount 动态偏移数量
             */
            virtual void BindDescriptorSets(
                IRenderPipeline *pipeline,
                IDescriptorSet *descriptorSet,
                uint32_t firstSet = 0,
                const uint32_t *dynamicOffsets = nullptr,
                uint32_t dynamicOffsetCount = 0) = 0;

            /**
             * @brief 推送常量
             * @param pipeline 管线
             * @param stageFlags 着色器阶段
             * @param offset 偏移
             * @param size 大小
             * @param values 值
             */
            virtual void PushConstants(
                IRenderPipeline *pipeline,
                ShaderStageFlags stageFlags,
                uint32_t offset,
                uint32_t size,
                const void *values) = 0;

            /**
             * @brief 绑定索引缓冲区
             * @param buffer 索引缓冲区
             * @param offset 偏移
             * @param indexType 索引类型
             */
            virtual void BindIndexBuffer(
                IBuffer *buffer,
                uint64_t offset,
                IndexType indexType) = 0;

            /**
             * @brief 绑定顶点缓冲区
             * @param firstBinding 第一个绑定点
             * @param bindingCount 绑定数量
             * @param buffers 缓冲区数组
             * @param offsets 偏移数组
             */
            virtual void BindVertexBuffers(
                uint32_t firstBinding,
                uint32_t bindingCount,
                IBuffer **buffers,
                const uint64_t *offsets) = 0;

            /**
             * @brief 设置视口
             * @param firstViewport 第一个视口索引
             * @param viewportCount 视口数量
             * @param viewports 视口数组
             */
            virtual void SetViewports(
                uint32_t firstViewport,
                uint32_t viewportCount,
                const Viewport *viewports) = 0;

            /**
             * @brief 设置裁剪矩形
             * @param firstScissor 第一个裁剪矩形索引
             * @param scissorCount 裁剪矩形数量
             * @param scissors 裁剪矩形数组
             */
            virtual void SetScissors(
                uint32_t firstScissor,
                uint32_t scissorCount,
                const Rect2D *scissors) = 0;

            /**
             * @brief 设置线宽
             * @param lineWidth 线宽
             */
            virtual void SetLineWidth(float lineWidth) = 0;

            /**
             * @brief 设置深度偏移
             * @param depthBiasConstantFactor 常量因子
             * @param depthBiasClamp 钳制值
             * @param depthBiasSlopeFactor 斜率因子
             */
            virtual void SetDepthBias(
                float depthBiasConstantFactor,
                float depthBiasClamp,
                float depthBiasSlopeFactor) = 0;

            /**
             * @brief 设置混合常量
             * @param blendConstants 混合常量，RGBA格式
             */
            virtual void SetBlendConstants(const float blendConstants[4]) = 0;

            /**
             * @brief 设置深度边界
             * @param minDepthBounds 最小深度边界
             * @param maxDepthBounds 最大深度边界
             */
            virtual void SetDepthBounds(float minDepthBounds, float maxDepthBounds) = 0;

            /**
             * @brief 设置模板参考值
             * @param face 面
             * @param reference 参考值
             */
            virtual void SetStencilReference(StencilFaceFlags face, uint32_t reference) = 0;

            /**
             * @brief 绘制
             * @param vertexCount 顶点数量
             * @param instanceCount 实例数量
             * @param firstVertex 第一个顶点索引
             * @param firstInstance 第一个实例索引
             */
            virtual void Draw(
                uint32_t vertexCount,
                uint32_t instanceCount = 1,
                uint32_t firstVertex = 0,
                uint32_t firstInstance = 0) = 0;

            /**
             * @brief 绘制索引
             * @param indexCount 索引数量
             * @param instanceCount 实例数量
             * @param firstIndex 第一个索引
             * @param vertexOffset 顶点偏移
             * @param firstInstance 第一个实例索引
             */
            virtual void DrawIndexed(
                uint32_t indexCount,
                uint32_t instanceCount = 1,
                uint32_t firstIndex = 0,
                int32_t vertexOffset = 0,
                uint32_t firstInstance = 0) = 0;

            /**
             * @brief 间接绘制
             * @param buffer 间接缓冲区
             * @param offset 偏移
             * @param drawCount 绘制数量
             * @param stride 步长
             */
            virtual void DrawIndirect(
                IBuffer *buffer,
                uint64_t offset,
                uint32_t drawCount,
                uint32_t stride) = 0;

            /**
             * @brief 间接绘制索引
             * @param buffer 间接缓冲区
             * @param offset 偏移
             * @param drawCount 绘制数量
             * @param stride 步长
             */
            virtual void DrawIndexedIndirect(
                IBuffer *buffer,
                uint64_t offset,
                uint32_t drawCount,
                uint32_t stride) = 0;
        };

        /**
         * @brief 命令编码器工厂接口（通常作为IRenderCommandBuffer的一部分）
         */
        class IRenderCommandEncoderFactory
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IRenderCommandEncoderFactory() = default;

            /**
             * @brief 创建图形命令编码器
             * @param commandBuffer 命令缓冲区
             * @return 图形命令编码器，失败返回nullptr
             */
            virtual IGraphicsCommandEncoder *CreateGraphicsEncoder(IRenderCommandBuffer *commandBuffer) = 0;

            /**
             * @brief 创建计算命令编码器
             * @param commandBuffer 命令缓冲区
             * @return 计算命令编码器，失败返回nullptr
             */
            virtual IComputeCommandEncoder *CreateComputeEncoder(IRenderCommandBuffer *commandBuffer) = 0;

            /**
             * @brief 创建转移命令编码器
             * @param commandBuffer 命令缓冲区
             * @return 转移命令编码器，失败返回nullptr
             */
            virtual ITransferCommandEncoder *CreateTransferEncoder(IRenderCommandBuffer *commandBuffer) = 0;
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_IRENDER_COMMAND_ENCODER_H