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

                // 视口和裁剪
                virtual void SetViewports(const std::vector<Viewport> &viewports) override;
                virtual void SetScissors(const std::vector<Rect2D> &scissors) override;

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
            };

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_CONTEXT_H