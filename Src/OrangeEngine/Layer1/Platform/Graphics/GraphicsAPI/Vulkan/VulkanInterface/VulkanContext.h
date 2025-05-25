/**
 * @file VulkanContext.h
 * @brief Vulkan渲染上下文实现
 */

#ifndef ORANGE_VULKAN_CONTEXT_H
#define ORANGE_VULKAN_CONTEXT_H

#include "../../../GraphicsInterface/RenderInterface/IRenderContext.h"
#include "../VulkanCommon/VulkanCommon.h"
#include <vector>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            class VulkanDevice;

            /**
             * @brief Vulkan渲染上下文实现
             */
            class VulkanContext : public IRenderContext
            {
            public:
                VulkanContext(VulkanDevice *device);
                virtual ~VulkanContext();

                // IRenderContext接口实现
                virtual bool Begin() override;
                virtual bool End() override;
                virtual bool Submit(const std::vector<ISemaphore *> &waitSemaphores = {},
                                    const std::vector<ISemaphore *> &signalSemaphores = {},
                                    IFence *fence = nullptr) override;
                virtual bool Reset() override;

                // 渲染通道控制
                virtual void BeginRenderPass(IRenderPass *renderPass,
                                             IFramebuffer *framebuffer,
                                             const std::vector<Color4f> &clearValues,
                                             const Rect2D &renderArea) override;
                virtual void EndRenderPass() override;

                // 管线绑定
                virtual void BindPipeline(IRenderPipeline *pipeline) override;

                // 缓冲区绑定
                virtual void BindVertexBuffer(IRenderBuffer *buffer, uint32_t binding, uint64_t offset = 0) override;
                virtual void BindIndexBuffer(IRenderBuffer *buffer, uint32_t indexType, uint64_t offset = 0) override;

                // 描述符集绑定
                virtual void BindDescriptorSet(IPipelineLayout *layout,
                                               IDescriptorSet *descriptorSet,
                                               uint32_t setIndex,
                                               const std::vector<uint32_t> &dynamicOffsets = {}) override;

                // 视口和裁剪
                virtual void SetViewports(const std::vector<Viewport> &viewports) override;
                virtual void SetScissors(const std::vector<Rect2D> &scissors) override;

                // 动态状态设置
                virtual void SetLineWidth(float lineWidth) override;
                virtual void SetDepthBias(float constantFactor, float clamp, float slopeFactor) override;
                virtual void SetBlendConstants(const float blendConstants[4]) override;
                virtual void SetStencilReference(uint32_t reference) override;

                // 推送常量
                virtual void PushConstants(IPipelineLayout *layout,
                                           ShaderStageFlag stageFlags,
                                           uint32_t offset,
                                           uint32_t size,
                                           const void *data) override;

                // 绘制命令
                virtual void Draw(uint32_t vertexCount,
                                  uint32_t instanceCount = 1,
                                  uint32_t firstVertex = 0,
                                  uint32_t firstInstance = 0) override;
                virtual void DrawIndexed(uint32_t indexCount,
                                         uint32_t instanceCount = 1,
                                         uint32_t firstIndex = 0,
                                         int32_t vertexOffset = 0,
                                         uint32_t firstInstance = 0) override;
                virtual void DrawIndirect(IRenderBuffer *buffer,
                                          uint64_t offset,
                                          uint32_t drawCount,
                                          uint32_t stride) override;
                virtual void DrawIndexedIndirect(IRenderBuffer *buffer,
                                                 uint64_t offset,
                                                 uint32_t drawCount,
                                                 uint32_t stride) override;

                // 计算着色器
                virtual void Dispatch(uint32_t groupCountX,
                                      uint32_t groupCountY,
                                      uint32_t groupCountZ) override;
                virtual void DispatchIndirect(IRenderBuffer *buffer,
                                              uint64_t offset) override;

                // 资源复制
                virtual void CopyBuffer(IRenderBuffer *srcBuffer,
                                        IRenderBuffer *dstBuffer,
                                        const std::vector<BufferCopyRegion> &regions) override;
                virtual void CopyBufferToTexture(IRenderBuffer *srcBuffer,
                                                 IRenderTexture *dstTexture,
                                                 ResourceState dstLayout,
                                                 const std::vector<BufferTextureCopyRegion> &regions) override;
                virtual void CopyTextureToBuffer(IRenderTexture *srcTexture,
                                                 ResourceState srcLayout,
                                                 IRenderBuffer *dstBuffer,
                                                 const std::vector<BufferTextureCopyRegion> &regions) override;
                virtual void CopyTexture(IRenderTexture *srcTexture,
                                         ResourceState srcLayout,
                                         IRenderTexture *dstTexture,
                                         ResourceState dstLayout,
                                         const std::vector<TextureCopyRegion> &regions) override;

                // 资源屏障
                virtual void TextureBarrier(IRenderTexture *texture,
                                            ResourceState oldState,
                                            ResourceState newState) override;
                virtual void BufferBarrier(IRenderBuffer *buffer,
                                           ResourceState oldState,
                                           ResourceState newState) override;

                // Mipmap生成
                virtual void GenerateMipmaps(IRenderTexture *texture,
                                             ResourceState oldState,
                                             ResourceState newState) override;

                // 调试标记
                virtual void BeginDebugMarker(const char *name, uint32_t color = 0xFFFFFFFF) override;
                virtual void EndDebugMarker() override;
                virtual void InsertDebugMarker(const char *name, uint32_t color = 0xFFFFFFFF) override;

                // 设备获取
                virtual IRenderDevice *GetDevice() const override;
                virtual void *GetNativeCommandBuffer() const override { return (void *)m_commandBuffer; }

                // Vulkan特定方法
                VkCommandBuffer GetVkCommandBuffer() const { return m_commandBuffer; }

                // 初始化方法
                bool Initialize();
                void Shutdown();

            private:
                VulkanDevice *m_device;
                VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
                bool m_isRecording = false;
                bool m_isInRenderPass = false;

                // 工具方法
                void ConvertClearValues(const std::vector<Color4f> &clearValues, std::vector<VkClearValue> &vkClearValues);
                VkRect2D ConvertRect2D(const Rect2D &rect);
                VkViewport ConvertViewport(const Viewport &viewport);
                VkIndexType ConvertIndexType(uint32_t indexType);
                VkShaderStageFlags ConvertShaderStageFlags(ShaderStageFlag stageFlags);
                VkPipelineStageFlags ConvertResourceStateToPipelineStage(ResourceState state);
                VkAccessFlags ConvertResourceStateToAccessFlags(ResourceState state);
                VkImageLayout ConvertResourceStateToImageLayout(ResourceState state);
            };

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_CONTEXT_H