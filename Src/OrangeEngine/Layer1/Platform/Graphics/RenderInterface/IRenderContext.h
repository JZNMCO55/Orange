/**
 * @file IRenderContext.h
 * @brief 渲染上下文接口，负责命令缓冲区和渲染状态管理
 */

#ifndef ORANGE_IRENDER_CONTEXT_H
#define ORANGE_IRENDER_CONTEXT_H

#include "../RenderCommon/RenderCommon.h"

namespace Orange
{
    namespace Graphics
    {

        // 前向声明
        class IRenderDevice;
        class IRenderPipeline;
        class IRenderPass;
        class IRenderBuffer;
        class IRenderTexture;
        class IRenderSampler;
        class IShaderModule;
        class IDescriptorSet;
        class IPipelineLayout;
        class IFramebuffer;
        class IFence;
        class ISemaphore;

        /**
         * @brief 渲染上下文接口
         *
         * 渲染上下文是执行渲染命令的环境，负责管理命令缓冲区、渲染状态和同步。
         * 它封装了与特定图形API的命令记录和提交逻辑。
         */
        class IRenderContext
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IRenderContext() = default;

            /**
             * @brief 开始记录命令
             * @return 是否成功开始记录
             */
            virtual bool Begin() = 0;

            /**
             * @brief 结束记录命令
             * @return 是否成功结束记录
             */
            virtual bool End() = 0;

            /**
             * @brief 提交命令到GPU执行
             * @param waitSemaphores 等待的信号量数组
             * @param signalSemaphores 需要触发的信号量数组
             * @param fence 完成时触发的栅栏
             * @return 是否成功提交
             */
            virtual bool Submit(const std::vector<ISemaphore *> &waitSemaphores = {},
                                const std::vector<ISemaphore *> &signalSemaphores = {},
                                IFence *fence = nullptr) = 0;

            /**
             * @brief 重置上下文状态，准备下一帧使用
             * @return 是否成功重置
             */
            virtual bool Reset() = 0;

            /**
             * @brief 开始渲染通道
             * @param renderPass 渲染通道
             * @param framebuffer 帧缓冲
             * @param clearValues 清除值数组
             * @param renderArea 渲染区域
             */
            virtual void BeginRenderPass(IRenderPass *renderPass,
                                         IFramebuffer *framebuffer,
                                         const std::vector<Color4f> &clearValues,
                                         const Rect2D &renderArea) = 0;

            /**
             * @brief 结束渲染通道
             */
            virtual void EndRenderPass() = 0;

            /**
             * @brief 绑定图形管线
             * @param pipeline 图形管线
             */
            virtual void BindPipeline(IRenderPipeline *pipeline) = 0;

            /**
             * @brief 绑定顶点缓冲区
             * @param buffer 顶点缓冲区
             * @param binding 绑定点
             * @param offset 偏移量
             */
            virtual void BindVertexBuffer(IRenderBuffer *buffer, uint32_t binding, uint64_t offset = 0) = 0;

            /**
             * @brief 绑定索引缓冲区
             * @param buffer 索引缓冲区
             * @param indexType 索引类型（16位或32位）
             * @param offset 偏移量
             */
            virtual void BindIndexBuffer(IRenderBuffer *buffer, uint32_t indexType, uint64_t offset = 0) = 0;

            /**
             * @brief 绑定描述符集
             * @param layout 管线布局
             * @param descriptorSet 描述符集
             * @param setIndex 描述符集索引
             * @param dynamicOffsets 动态偏移数组
             */
            virtual void BindDescriptorSet(IPipelineLayout *layout,
                                           IDescriptorSet *descriptorSet,
                                           uint32_t setIndex,
                                           const std::vector<uint32_t> &dynamicOffsets = {}) = 0;

            /**
             * @brief 设置视口
             * @param viewports 视口数组
             */
            virtual void SetViewports(const std::vector<Viewport> &viewports) = 0;

            /**
             * @brief 设置裁剪矩形
             * @param scissors 裁剪矩形数组
             */
            virtual void SetScissors(const std::vector<Rect2D> &scissors) = 0;

            /**
             * @brief 设置线宽
             * @param lineWidth 线宽
             */
            virtual void SetLineWidth(float lineWidth) = 0;

            /**
             * @brief 设置深度偏移
             * @param constantFactor 常量因子
             * @param clamp 钳制值
             * @param slopeFactor 斜率因子
             */
            virtual void SetDepthBias(float constantFactor, float clamp, float slopeFactor) = 0;

            /**
             * @brief 设置混合常量
             * @param blendConstants 混合常量数组
             */
            virtual void SetBlendConstants(const float blendConstants[4]) = 0;

            /**
             * @brief 设置模板参考值
             * @param reference 参考值
             */
            virtual void SetStencilReference(uint32_t reference) = 0;

            /**
             * @brief 推送常量
             * @param layout 管线布局
             * @param stageFlags 着色器阶段标志
             * @param offset 偏移量
             * @param size 大小
             * @param data 数据指针
             */
            virtual void PushConstants(IPipelineLayout *layout,
                                       ShaderStageFlag stageFlags,
                                       uint32_t offset,
                                       uint32_t size,
                                       const void *data) = 0;

            /**
             * @brief 绘制
             * @param vertexCount 顶点数量
             * @param instanceCount 实例数量
             * @param firstVertex 起始顶点索引
             * @param firstInstance 起始实例索引
             */
            virtual void Draw(uint32_t vertexCount,
                              uint32_t instanceCount = 1,
                              uint32_t firstVertex = 0,
                              uint32_t firstInstance = 0) = 0;

            /**
             * @brief 索引绘制
             * @param indexCount 索引数量
             * @param instanceCount 实例数量
             * @param firstIndex 起始索引
             * @param vertexOffset 顶点偏移
             * @param firstInstance 起始实例索引
             */
            virtual void DrawIndexed(uint32_t indexCount,
                                     uint32_t instanceCount = 1,
                                     uint32_t firstIndex = 0,
                                     int32_t vertexOffset = 0,
                                     uint32_t firstInstance = 0) = 0;

            /**
             * @brief 间接绘制
             * @param buffer 间接绘制缓冲区
             * @param offset 偏移量
             * @param drawCount 绘制命令数量
             * @param stride 每个绘制命令的步长
             */
            virtual void DrawIndirect(IRenderBuffer *buffer,
                                      uint64_t offset,
                                      uint32_t drawCount,
                                      uint32_t stride) = 0;

            /**
             * @brief 间接索引绘制
             * @param buffer 间接绘制缓冲区
             * @param offset 偏移量
             * @param drawCount 绘制命令数量
             * @param stride 每个绘制命令的步长
             */
            virtual void DrawIndexedIndirect(IRenderBuffer *buffer,
                                             uint64_t offset,
                                             uint32_t drawCount,
                                             uint32_t stride) = 0;

            /**
             * @brief 执行计算着色器
             * @param groupCountX X方向的工作组数量
             * @param groupCountY Y方向的工作组数量
             * @param groupCountZ Z方向的工作组数量
             */
            virtual void Dispatch(uint32_t groupCountX,
                                  uint32_t groupCountY,
                                  uint32_t groupCountZ) = 0;

            /**
             * @brief 间接执行计算着色器
             * @param buffer 间接执行缓冲区
             * @param offset 偏移量
             */
            virtual void DispatchIndirect(IRenderBuffer *buffer,
                                          uint64_t offset) = 0;

            /**
             * @brief 复制缓冲区
             * @param srcBuffer 源缓冲区
             * @param dstBuffer 目标缓冲区
             * @param regions 复制区域数组
             */
            virtual void CopyBuffer(IRenderBuffer *srcBuffer,
                                    IRenderBuffer *dstBuffer,
                                    const std::vector<BufferCopyRegion> &regions) = 0;

            /**
             * @brief 复制缓冲区到纹理
             * @param srcBuffer 源缓冲区
             * @param dstTexture 目标纹理
             * @param dstLayout 目标纹理布局
             * @param regions 复制区域数组
             */
            virtual void CopyBufferToTexture(IRenderBuffer *srcBuffer,
                                             IRenderTexture *dstTexture,
                                             ResourceState dstLayout,
                                             const std::vector<BufferTextureCopyRegion> &regions) = 0;

            /**
             * @brief 复制纹理到缓冲区
             * @param srcTexture 源纹理
             * @param srcLayout 源纹理布局
             * @param dstBuffer 目标缓冲区
             * @param regions 复制区域数组
             */
            virtual void CopyTextureToBuffer(IRenderTexture *srcTexture,
                                             ResourceState srcLayout,
                                             IRenderBuffer *dstBuffer,
                                             const std::vector<BufferTextureCopyRegion> &regions) = 0;

            /**
             * @brief 复制纹理
             * @param srcTexture 源纹理
             * @param srcLayout 源纹理布局
             * @param dstTexture 目标纹理
             * @param dstLayout 目标纹理布局
             * @param regions 复制区域数组
             */
            virtual void CopyTexture(IRenderTexture *srcTexture,
                                     ResourceState srcLayout,
                                     IRenderTexture *dstTexture,
                                     ResourceState dstLayout,
                                     const std::vector<TextureCopyRegion> &regions) = 0;

            /**
             * @brief 资源屏障（状态转换）
             * @param texture 纹理资源
             * @param oldState 旧状态
             * @param newState 新状态
             */
            virtual void TextureBarrier(IRenderTexture *texture,
                                        ResourceState oldState,
                                        ResourceState newState) = 0;

            /**
             * @brief 资源屏障（状态转换）
             * @param buffer 缓冲区资源
             * @param oldState 旧状态
             * @param newState 新状态
             */
            virtual void BufferBarrier(IRenderBuffer *buffer,
                                       ResourceState oldState,
                                       ResourceState newState) = 0;

            /**
             * @brief 生成纹理mipmap
             * @param texture 纹理
             * @param oldState 旧状态
             * @param newState 新状态
             */
            virtual void GenerateMipmaps(IRenderTexture *texture,
                                         ResourceState oldState,
                                         ResourceState newState) = 0;

            /**
             * @brief 开始调试标记
             * @param name 标记名称
             * @param color 标记颜色（RGBA格式，每个分量为0-255）
             */
            virtual void BeginDebugMarker(const char *name, uint32_t color = 0xFFFFFFFF) = 0;

            /**
             * @brief 结束调试标记
             */
            virtual void EndDebugMarker() = 0;

            /**
             * @brief 插入调试标记
             * @param name 标记名称
             * @param color 标记颜色（RGBA格式，每个分量为0-255）
             */
            virtual void InsertDebugMarker(const char *name, uint32_t color = 0xFFFFFFFF) = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备指针
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 获取原生命令缓冲区句柄
             * @return 原生命令缓冲区句柄（如VkCommandBuffer、ID3D12GraphicsCommandList等）
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeCommandBuffer() const = 0;
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_IRENDER_CONTEXT_H