/**
 * @file VulkanContext.cpp
 * @brief Vulkan渲染上下文实现
 */

#include "VulkanContext.h"
#include "VulkanDevice.h"
#include "VulkanRenderPass.h"
#include "VulkanPipeline.h"
#include "../VulkanResources/VulkanBuffer.h"
#include "../VulkanResources/VulkanTexture.h"
#include "../VulkanSync/VulkanSync.h"
#include <iostream>
#include <algorithm>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            VulkanContext::VulkanContext(VulkanDevice *device)
                : m_device(device)
            {
            }

            VulkanContext::~VulkanContext()
            {
                Shutdown();
            }

            bool VulkanContext::Initialize()
            {
                // 分配命令缓冲区
                VkCommandBufferAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                allocInfo.commandPool = m_device->GetCommandPool();
                allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocInfo.commandBufferCount = 1;

                if (vkAllocateCommandBuffers(m_device->GetVkDevice(), &allocInfo, &m_commandBuffer) != VK_SUCCESS)
                {
                    std::cerr << "VulkanContext::Initialize: 分配命令缓冲区失败" << std::endl;
                    return false;
                }

                std::cout << "VulkanContext::Initialize: 初始化成功" << std::endl;
                return true;
            }

            void VulkanContext::Shutdown()
            {
                if (m_commandBuffer != VK_NULL_HANDLE)
                {
                    vkFreeCommandBuffers(m_device->GetVkDevice(), m_device->GetCommandPool(), 1, &m_commandBuffer);
                    m_commandBuffer = VK_NULL_HANDLE;
                }
                m_isRecording = false;
                m_isInRenderPass = false;
            }

            bool VulkanContext::Begin()
            {
                if (m_isRecording)
                {
                    std::cerr << "VulkanContext::Begin: 命令缓冲区已在记录中" << std::endl;
                    return false;
                }

                VkCommandBufferBeginInfo beginInfo{};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

                if (vkBeginCommandBuffer(m_commandBuffer, &beginInfo) != VK_SUCCESS)
                {
                    std::cerr << "VulkanContext::Begin: 开始记录命令缓冲区失败" << std::endl;
                    return false;
                }

                m_isRecording = true;
                return true;
            }

            bool VulkanContext::End()
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::End: 命令缓冲区未在记录中" << std::endl;
                    return false;
                }

                if (m_isInRenderPass)
                {
                    std::cerr << "VulkanContext::End: 渲染通道未结束" << std::endl;
                    return false;
                }

                if (vkEndCommandBuffer(m_commandBuffer) != VK_SUCCESS)
                {
                    std::cerr << "VulkanContext::End: 结束记录命令缓冲区失败" << std::endl;
                    return false;
                }

                m_isRecording = false;
                return true;
            }

            bool VulkanContext::Submit(const std::vector<ISemaphore *> &waitSemaphores,
                                       const std::vector<ISemaphore *> &signalSemaphores,
                                       IFence *fence)
            {
                if (m_isRecording)
                {
                    std::cerr << "VulkanContext::Submit: 命令缓冲区仍在记录中" << std::endl;
                    return false;
                }

                // 转换信号量
                std::vector<VkSemaphore> vkWaitSemaphores;
                std::vector<VkPipelineStageFlags> waitStages;
                for (auto *semaphore : waitSemaphores)
                {
                    auto *vulkanSemaphore = static_cast<VulkanSemaphore *>(semaphore);
                    vkWaitSemaphores.push_back(vulkanSemaphore->GetVkSemaphore());
                    waitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
                }

                std::vector<VkSemaphore> vkSignalSemaphores;
                for (auto *semaphore : signalSemaphores)
                {
                    auto *vulkanSemaphore = static_cast<VulkanSemaphore *>(semaphore);
                    vkSignalSemaphores.push_back(vulkanSemaphore->GetVkSemaphore());
                }

                VkFence vkFence = VK_NULL_HANDLE;
                if (fence)
                {
                    auto *vulkanFence = static_cast<VulkanFence *>(fence);
                    vkFence = vulkanFence->GetVkFence();
                }

                VkSubmitInfo submitInfo{};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.waitSemaphoreCount = static_cast<uint32_t>(vkWaitSemaphores.size());
                submitInfo.pWaitSemaphores = vkWaitSemaphores.data();
                submitInfo.pWaitDstStageMask = waitStages.data();
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &m_commandBuffer;
                submitInfo.signalSemaphoreCount = static_cast<uint32_t>(vkSignalSemaphores.size());
                submitInfo.pSignalSemaphores = vkSignalSemaphores.data();

                if (vkQueueSubmit(m_device->GetGraphicsQueue(), 1, &submitInfo, vkFence) != VK_SUCCESS)
                {
                    std::cerr << "VulkanContext::Submit: 提交命令缓冲区失败" << std::endl;
                    return false;
                }

                return true;
            }

            bool VulkanContext::Reset()
            {
                if (m_isRecording)
                {
                    std::cerr << "VulkanContext::Reset: 命令缓冲区正在记录中，无法重置" << std::endl;
                    return false;
                }

                if (vkResetCommandBuffer(m_commandBuffer, 0) != VK_SUCCESS)
                {
                    std::cerr << "VulkanContext::Reset: 重置命令缓冲区失败" << std::endl;
                    return false;
                }

                m_isInRenderPass = false;
                return true;
            }

            void VulkanContext::BeginRenderPass(IRenderPass *renderPass,
                                                IFramebuffer *framebuffer,
                                                const std::vector<Color4f> &clearValues,
                                                const Rect2D &renderArea)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::BeginRenderPass: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                if (m_isInRenderPass)
                {
                    std::cerr << "VulkanContext::BeginRenderPass: 已在渲染通道中" << std::endl;
                    return;
                }

                auto *vulkanRenderPass = static_cast<VulkanRenderPass *>(renderPass);
                auto *vulkanFramebuffer = static_cast<VulkanFramebuffer *>(framebuffer);

                std::vector<VkClearValue> vkClearValues;
                ConvertClearValues(clearValues, vkClearValues);

                VkRenderPassBeginInfo renderPassInfo{};
                renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                renderPassInfo.renderPass = vulkanRenderPass->GetVkRenderPass();
                renderPassInfo.framebuffer = vulkanFramebuffer->GetVkFramebuffer();
                renderPassInfo.renderArea = ConvertRect2D(renderArea);
                renderPassInfo.clearValueCount = static_cast<uint32_t>(vkClearValues.size());
                renderPassInfo.pClearValues = vkClearValues.data();

                vkCmdBeginRenderPass(m_commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
                m_isInRenderPass = true;
            }

            void VulkanContext::EndRenderPass()
            {
                if (!m_isInRenderPass)
                {
                    std::cerr << "VulkanContext::EndRenderPass: 未在渲染通道中" << std::endl;
                    return;
                }

                vkCmdEndRenderPass(m_commandBuffer);
                m_isInRenderPass = false;
            }

            void VulkanContext::BindPipeline(IRenderPipeline *pipeline)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::BindPipeline: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                auto *vulkanPipeline = static_cast<VulkanPipeline *>(pipeline);
                VkPipelineBindPoint bindPoint = (vulkanPipeline->GetType() == PipelineType::Compute)
                                                    ? VK_PIPELINE_BIND_POINT_COMPUTE
                                                    : VK_PIPELINE_BIND_POINT_GRAPHICS;

                vkCmdBindPipeline(m_commandBuffer, bindPoint, vulkanPipeline->GetVkPipeline());
            }

            void VulkanContext::BindVertexBuffer(IRenderBuffer *buffer, uint32_t binding, uint64_t offset)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::BindVertexBuffer: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                auto *vulkanBuffer = static_cast<VulkanBuffer *>(buffer);
                VkBuffer vkBuffer = vulkanBuffer->GetVkBuffer();
                VkDeviceSize vkOffset = static_cast<VkDeviceSize>(offset);

                vkCmdBindVertexBuffers(m_commandBuffer, binding, 1, &vkBuffer, &vkOffset);
            }

            void VulkanContext::BindIndexBuffer(IRenderBuffer *buffer, uint32_t indexType, uint64_t offset)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::BindIndexBuffer: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                auto *vulkanBuffer = static_cast<VulkanBuffer *>(buffer);
                VkIndexType vkIndexType = ConvertIndexType(indexType);

                vkCmdBindIndexBuffer(m_commandBuffer, vulkanBuffer->GetVkBuffer(),
                                     static_cast<VkDeviceSize>(offset), vkIndexType);
            }

            void VulkanContext::BindDescriptorSet(IPipelineLayout *layout,
                                                  IDescriptorSet *descriptorSet,
                                                  uint32_t setIndex,
                                                  const std::vector<uint32_t> &dynamicOffsets)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::BindDescriptorSet: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                // 暂时简单实现，后续需要实现描述符集
                std::cout << "VulkanContext::BindDescriptorSet: 暂未实现" << std::endl;
            }

            void VulkanContext::SetViewports(const std::vector<Viewport> &viewports)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::SetViewports: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                std::vector<VkViewport> vkViewports;
                vkViewports.reserve(viewports.size());
                for (const auto &viewport : viewports)
                {
                    vkViewports.push_back(ConvertViewport(viewport));
                }

                vkCmdSetViewport(m_commandBuffer, 0, static_cast<uint32_t>(vkViewports.size()), vkViewports.data());
            }

            void VulkanContext::SetScissors(const std::vector<Rect2D> &scissors)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::SetScissors: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                std::vector<VkRect2D> vkScissors;
                vkScissors.reserve(scissors.size());
                for (const auto &scissor : scissors)
                {
                    vkScissors.push_back(ConvertRect2D(scissor));
                }

                vkCmdSetScissor(m_commandBuffer, 0, static_cast<uint32_t>(vkScissors.size()), vkScissors.data());
            }

            void VulkanContext::SetLineWidth(float lineWidth)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::SetLineWidth: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                vkCmdSetLineWidth(m_commandBuffer, lineWidth);
            }

            void VulkanContext::SetDepthBias(float constantFactor, float clamp, float slopeFactor)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::SetDepthBias: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                vkCmdSetDepthBias(m_commandBuffer, constantFactor, clamp, slopeFactor);
            }

            void VulkanContext::SetBlendConstants(const float blendConstants[4])
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::SetBlendConstants: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                vkCmdSetBlendConstants(m_commandBuffer, blendConstants);
            }

            void VulkanContext::SetStencilReference(uint32_t reference)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::SetStencilReference: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                vkCmdSetStencilReference(m_commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
            }

            void VulkanContext::PushConstants(IPipelineLayout *layout,
                                              ShaderStageFlag stageFlags,
                                              uint32_t offset,
                                              uint32_t size,
                                              const void *data)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::PushConstants: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                auto *vulkanLayout = static_cast<VulkanPipelineLayout *>(layout);
                VkShaderStageFlags vkStageFlags = ConvertShaderStageFlags(stageFlags);

                vkCmdPushConstants(m_commandBuffer, vulkanLayout->GetVkPipelineLayout(),
                                   vkStageFlags, offset, size, data);
            }

            void VulkanContext::Draw(uint32_t vertexCount,
                                     uint32_t instanceCount,
                                     uint32_t firstVertex,
                                     uint32_t firstInstance)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::Draw: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                if (!m_isInRenderPass)
                {
                    std::cerr << "VulkanContext::Draw: 未在渲染通道中" << std::endl;
                    return;
                }

                vkCmdDraw(m_commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
            }

            void VulkanContext::DrawIndexed(uint32_t indexCount,
                                            uint32_t instanceCount,
                                            uint32_t firstIndex,
                                            int32_t vertexOffset,
                                            uint32_t firstInstance)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::DrawIndexed: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                if (!m_isInRenderPass)
                {
                    std::cerr << "VulkanContext::DrawIndexed: 未在渲染通道中" << std::endl;
                    return;
                }

                vkCmdDrawIndexed(m_commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
            }

            void VulkanContext::DrawIndirect(IRenderBuffer *buffer,
                                             uint64_t offset,
                                             uint32_t drawCount,
                                             uint32_t stride)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::DrawIndirect: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                if (!m_isInRenderPass)
                {
                    std::cerr << "VulkanContext::DrawIndirect: 未在渲染通道中" << std::endl;
                    return;
                }

                auto *vulkanBuffer = static_cast<VulkanBuffer *>(buffer);
                vkCmdDrawIndirect(m_commandBuffer, vulkanBuffer->GetVkBuffer(),
                                  static_cast<VkDeviceSize>(offset), drawCount, stride);
            }

            void VulkanContext::DrawIndexedIndirect(IRenderBuffer *buffer,
                                                    uint64_t offset,
                                                    uint32_t drawCount,
                                                    uint32_t stride)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::DrawIndexedIndirect: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                if (!m_isInRenderPass)
                {
                    std::cerr << "VulkanContext::DrawIndexedIndirect: 未在渲染通道中" << std::endl;
                    return;
                }

                auto *vulkanBuffer = static_cast<VulkanBuffer *>(buffer);
                vkCmdDrawIndexedIndirect(m_commandBuffer, vulkanBuffer->GetVkBuffer(),
                                         static_cast<VkDeviceSize>(offset), drawCount, stride);
            }

            void VulkanContext::Dispatch(uint32_t groupCountX,
                                         uint32_t groupCountY,
                                         uint32_t groupCountZ)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::Dispatch: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                if (m_isInRenderPass)
                {
                    std::cerr << "VulkanContext::Dispatch: 计算着色器不能在渲染通道中执行" << std::endl;
                    return;
                }

                vkCmdDispatch(m_commandBuffer, groupCountX, groupCountY, groupCountZ);
            }

            void VulkanContext::DispatchIndirect(IRenderBuffer *buffer, uint64_t offset)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::DispatchIndirect: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                if (m_isInRenderPass)
                {
                    std::cerr << "VulkanContext::DispatchIndirect: 计算着色器不能在渲染通道中执行" << std::endl;
                    return;
                }

                auto *vulkanBuffer = static_cast<VulkanBuffer *>(buffer);
                vkCmdDispatchIndirect(m_commandBuffer, vulkanBuffer->GetVkBuffer(),
                                      static_cast<VkDeviceSize>(offset));
            }

            void VulkanContext::CopyBuffer(IRenderBuffer *srcBuffer,
                                           IRenderBuffer *dstBuffer,
                                           const std::vector<BufferCopyRegion> &regions)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::CopyBuffer: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                if (m_isInRenderPass)
                {
                    std::cerr << "VulkanContext::CopyBuffer: 复制操作不能在渲染通道中执行" << std::endl;
                    return;
                }

                auto *vulkanSrcBuffer = static_cast<VulkanBuffer *>(srcBuffer);
                auto *vulkanDstBuffer = static_cast<VulkanBuffer *>(dstBuffer);

                std::vector<VkBufferCopy> vkRegions;
                vkRegions.reserve(regions.size());
                for (const auto &region : regions)
                {
                    VkBufferCopy vkRegion{};
                    vkRegion.srcOffset = region.srcOffset;
                    vkRegion.dstOffset = region.dstOffset;
                    vkRegion.size = region.size;
                    vkRegions.push_back(vkRegion);
                }

                vkCmdCopyBuffer(m_commandBuffer, vulkanSrcBuffer->GetVkBuffer(),
                                vulkanDstBuffer->GetVkBuffer(),
                                static_cast<uint32_t>(vkRegions.size()), vkRegions.data());
            }

            void VulkanContext::CopyBufferToTexture(IRenderBuffer *srcBuffer,
                                                    IRenderTexture *dstTexture,
                                                    ResourceState dstLayout,
                                                    const std::vector<BufferTextureCopyRegion> &regions)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::CopyBufferToTexture: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                if (m_isInRenderPass)
                {
                    std::cerr << "VulkanContext::CopyBufferToTexture: 复制操作不能在渲染通道中执行" << std::endl;
                    return;
                }

                auto *vulkanSrcBuffer = static_cast<VulkanBuffer *>(srcBuffer);
                auto *vulkanDstTexture = static_cast<VulkanTexture *>(dstTexture);

                std::vector<VkBufferImageCopy> vkRegions;
                vkRegions.reserve(regions.size());
                for (const auto &region : regions)
                {
                    VkBufferImageCopy vkRegion{};
                    vkRegion.bufferOffset = region.bufferOffset;
                    vkRegion.bufferRowLength = region.bufferRowLength;
                    vkRegion.bufferImageHeight = region.bufferImageHeight;
                    vkRegion.imageSubresource.aspectMask = vulkanDstTexture->GetVkImageAspect();
                    vkRegion.imageSubresource.mipLevel = region.mipLevel;
                    vkRegion.imageSubresource.baseArrayLayer = region.baseArrayLayer;
                    vkRegion.imageSubresource.layerCount = region.layerCount;
                    vkRegion.imageOffset = {region.imageOffset.x, region.imageOffset.y, region.imageOffset.z};
                    vkRegion.imageExtent = {region.imageExtent.width, region.imageExtent.height, region.imageExtent.depth};
                    vkRegions.push_back(vkRegion);
                }

                VkImageLayout vkLayout = ConvertResourceStateToImageLayout(dstLayout);
                vkCmdCopyBufferToImage(m_commandBuffer, vulkanSrcBuffer->GetVkBuffer(),
                                       vulkanDstTexture->GetVkImage(), vkLayout,
                                       static_cast<uint32_t>(vkRegions.size()), vkRegions.data());
            }

            void VulkanContext::CopyTextureToBuffer(IRenderTexture *srcTexture,
                                                    ResourceState srcLayout,
                                                    IRenderBuffer *dstBuffer,
                                                    const std::vector<BufferTextureCopyRegion> &regions)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::CopyTextureToBuffer: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                if (m_isInRenderPass)
                {
                    std::cerr << "VulkanContext::CopyTextureToBuffer: 复制操作不能在渲染通道中执行" << std::endl;
                    return;
                }

                auto *vulkanSrcTexture = static_cast<VulkanTexture *>(srcTexture);
                auto *vulkanDstBuffer = static_cast<VulkanBuffer *>(dstBuffer);

                std::vector<VkBufferImageCopy> vkRegions;
                vkRegions.reserve(regions.size());
                for (const auto &region : regions)
                {
                    VkBufferImageCopy vkRegion{};
                    vkRegion.bufferOffset = region.bufferOffset;
                    vkRegion.bufferRowLength = region.bufferRowLength;
                    vkRegion.bufferImageHeight = region.bufferImageHeight;
                    vkRegion.imageSubresource.aspectMask = vulkanSrcTexture->GetVkImageAspect();
                    vkRegion.imageSubresource.mipLevel = region.mipLevel;
                    vkRegion.imageSubresource.baseArrayLayer = region.baseArrayLayer;
                    vkRegion.imageSubresource.layerCount = region.layerCount;
                    vkRegion.imageOffset = {region.imageOffset.x, region.imageOffset.y, region.imageOffset.z};
                    vkRegion.imageExtent = {region.imageExtent.width, region.imageExtent.height, region.imageExtent.depth};
                    vkRegions.push_back(vkRegion);
                }

                VkImageLayout vkLayout = ConvertResourceStateToImageLayout(srcLayout);
                vkCmdCopyImageToBuffer(m_commandBuffer, vulkanSrcTexture->GetVkImage(), vkLayout,
                                       vulkanDstBuffer->GetVkBuffer(),
                                       static_cast<uint32_t>(vkRegions.size()), vkRegions.data());
            }

            void VulkanContext::CopyTexture(IRenderTexture *srcTexture,
                                            ResourceState srcLayout,
                                            IRenderTexture *dstTexture,
                                            ResourceState dstLayout,
                                            const std::vector<TextureCopyRegion> &regions)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::CopyTexture: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                if (m_isInRenderPass)
                {
                    std::cerr << "VulkanContext::CopyTexture: 复制操作不能在渲染通道中执行" << std::endl;
                    return;
                }

                auto *vulkanSrcTexture = static_cast<VulkanTexture *>(srcTexture);
                auto *vulkanDstTexture = static_cast<VulkanTexture *>(dstTexture);

                std::vector<VkImageCopy> vkRegions;
                vkRegions.reserve(regions.size());
                for (const auto &region : regions)
                {
                    VkImageCopy vkRegion{};
                    vkRegion.srcSubresource.aspectMask = vulkanSrcTexture->GetVkImageAspect();
                    vkRegion.srcSubresource.mipLevel = region.srcMipLevel;
                    vkRegion.srcSubresource.baseArrayLayer = region.srcLayer;
                    vkRegion.srcSubresource.layerCount = 1;
                    vkRegion.srcOffset = {region.srcOffset.x, region.srcOffset.y, region.srcOffset.z};
                    vkRegion.dstSubresource.aspectMask = vulkanDstTexture->GetVkImageAspect();
                    vkRegion.dstSubresource.mipLevel = region.dstMipLevel;
                    vkRegion.dstSubresource.baseArrayLayer = region.dstLayer;
                    vkRegion.dstSubresource.layerCount = 1;
                    vkRegion.dstOffset = {region.dstOffset.x, region.dstOffset.y, region.dstOffset.z};
                    vkRegion.extent = {region.extent.width, region.extent.height, region.extent.depth};
                    vkRegions.push_back(vkRegion);
                }

                VkImageLayout vkSrcLayout = ConvertResourceStateToImageLayout(srcLayout);
                VkImageLayout vkDstLayout = ConvertResourceStateToImageLayout(dstLayout);

                vkCmdCopyImage(m_commandBuffer, vulkanSrcTexture->GetVkImage(), vkSrcLayout,
                               vulkanDstTexture->GetVkImage(), vkDstLayout,
                               static_cast<uint32_t>(vkRegions.size()), vkRegions.data());
            }

            void VulkanContext::TextureBarrier(IRenderTexture *texture,
                                               ResourceState oldState,
                                               ResourceState newState)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::TextureBarrier: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                auto *vulkanTexture = static_cast<VulkanTexture *>(texture);

                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = ConvertResourceStateToImageLayout(oldState);
                barrier.newLayout = ConvertResourceStateToImageLayout(newState);
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = vulkanTexture->GetVkImage();
                barrier.subresourceRange.aspectMask = vulkanTexture->GetVkImageAspect();
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = vulkanTexture->GetMipLevels();
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = vulkanTexture->GetArrayLayers();
                barrier.srcAccessMask = ConvertResourceStateToAccessFlags(oldState);
                barrier.dstAccessMask = ConvertResourceStateToAccessFlags(newState);

                VkPipelineStageFlags srcStage = ConvertResourceStateToPipelineStage(oldState);
                VkPipelineStageFlags dstStage = ConvertResourceStateToPipelineStage(newState);

                vkCmdPipelineBarrier(m_commandBuffer, srcStage, dstStage, 0,
                                     0, nullptr, 0, nullptr, 1, &barrier);
            }

            void VulkanContext::BufferBarrier(IRenderBuffer *buffer,
                                              ResourceState oldState,
                                              ResourceState newState)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::BufferBarrier: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                auto *vulkanBuffer = static_cast<VulkanBuffer *>(buffer);

                VkBufferMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                barrier.srcAccessMask = ConvertResourceStateToAccessFlags(oldState);
                barrier.dstAccessMask = ConvertResourceStateToAccessFlags(newState);
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.buffer = vulkanBuffer->GetVkBuffer();
                barrier.offset = 0;
                barrier.size = vulkanBuffer->GetSize();

                VkPipelineStageFlags srcStage = ConvertResourceStateToPipelineStage(oldState);
                VkPipelineStageFlags dstStage = ConvertResourceStateToPipelineStage(newState);

                vkCmdPipelineBarrier(m_commandBuffer, srcStage, dstStage, 0,
                                     0, nullptr, 1, &barrier, 0, nullptr);
            }

            void VulkanContext::GenerateMipmaps(IRenderTexture *texture,
                                                ResourceState oldState,
                                                ResourceState newState)
            {
                if (!m_isRecording)
                {
                    std::cerr << "VulkanContext::GenerateMipmaps: 命令缓冲区未在记录中" << std::endl;
                    return;
                }

                if (m_isInRenderPass)
                {
                    std::cerr << "VulkanContext::GenerateMipmaps: Mipmap生成不能在渲染通道中执行" << std::endl;
                    return;
                }

                auto *vulkanTexture = static_cast<VulkanTexture *>(texture);
                uint32_t mipLevels = vulkanTexture->GetMipLevels();

                if (mipLevels <= 1)
                {
                    return; // 无需生成mipmap
                }

                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.image = vulkanTexture->GetVkImage();
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.subresourceRange.aspectMask = vulkanTexture->GetVkImageAspect();
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = vulkanTexture->GetArrayLayers();
                barrier.subresourceRange.levelCount = 1;

                int32_t mipWidth = static_cast<int32_t>(vulkanTexture->GetWidth());
                int32_t mipHeight = static_cast<int32_t>(vulkanTexture->GetHeight());

                for (uint32_t i = 1; i < mipLevels; i++)
                {
                    barrier.subresourceRange.baseMipLevel = i - 1;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

                    vkCmdPipelineBarrier(m_commandBuffer,
                                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                         0, nullptr, 0, nullptr, 1, &barrier);

                    VkImageBlit blit{};
                    blit.srcOffsets[0] = {0, 0, 0};
                    blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
                    blit.srcSubresource.aspectMask = vulkanTexture->GetVkImageAspect();
                    blit.srcSubresource.mipLevel = i - 1;
                    blit.srcSubresource.baseArrayLayer = 0;
                    blit.srcSubresource.layerCount = vulkanTexture->GetArrayLayers();
                    blit.dstOffsets[0] = {0, 0, 0};
                    blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
                    blit.dstSubresource.aspectMask = vulkanTexture->GetVkImageAspect();
                    blit.dstSubresource.mipLevel = i;
                    blit.dstSubresource.baseArrayLayer = 0;
                    blit.dstSubresource.layerCount = vulkanTexture->GetArrayLayers();

                    vkCmdBlitImage(m_commandBuffer,
                                   vulkanTexture->GetVkImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   vulkanTexture->GetVkImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   1, &blit, VK_FILTER_LINEAR);

                    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    barrier.newLayout = ConvertResourceStateToImageLayout(newState);
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    barrier.dstAccessMask = ConvertResourceStateToAccessFlags(newState);

                    vkCmdPipelineBarrier(m_commandBuffer,
                                         VK_PIPELINE_STAGE_TRANSFER_BIT, ConvertResourceStateToPipelineStage(newState), 0,
                                         0, nullptr, 0, nullptr, 1, &barrier);

                    if (mipWidth > 1)
                        mipWidth /= 2;
                    if (mipHeight > 1)
                        mipHeight /= 2;
                }

                barrier.subresourceRange.baseMipLevel = mipLevels - 1;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = ConvertResourceStateToImageLayout(newState);
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = ConvertResourceStateToAccessFlags(newState);

                vkCmdPipelineBarrier(m_commandBuffer,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, ConvertResourceStateToPipelineStage(newState), 0,
                                     0, nullptr, 0, nullptr, 1, &barrier);
            }

            void VulkanContext::BeginDebugMarker(const char *name, uint32_t color)
            {
                if (!m_isRecording)
                {
                    return;
                }

                // 检查是否支持调试标记扩展
                static PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT = nullptr;
                if (!vkCmdBeginDebugUtilsLabelEXT)
                {
                    vkCmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)
                        vkGetDeviceProcAddr(m_device->GetVkDevice(), "vkCmdBeginDebugUtilsLabelEXT");
                }

                if (vkCmdBeginDebugUtilsLabelEXT)
                {
                    VkDebugUtilsLabelEXT label{};
                    label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
                    label.pLabelName = name;
                    label.color[0] = ((color >> 24) & 0xFF) / 255.0f; // R
                    label.color[1] = ((color >> 16) & 0xFF) / 255.0f; // G
                    label.color[2] = ((color >> 8) & 0xFF) / 255.0f;  // B
                    label.color[3] = (color & 0xFF) / 255.0f;         // A

                    vkCmdBeginDebugUtilsLabelEXT(m_commandBuffer, &label);
                }
            }

            void VulkanContext::EndDebugMarker()
            {
                if (!m_isRecording)
                {
                    return;
                }

                static PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT = nullptr;
                if (!vkCmdEndDebugUtilsLabelEXT)
                {
                    vkCmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)
                        vkGetDeviceProcAddr(m_device->GetVkDevice(), "vkCmdEndDebugUtilsLabelEXT");
                }

                if (vkCmdEndDebugUtilsLabelEXT)
                {
                    vkCmdEndDebugUtilsLabelEXT(m_commandBuffer);
                }
            }

            void VulkanContext::InsertDebugMarker(const char *name, uint32_t color)
            {
                if (!m_isRecording)
                {
                    return;
                }

                static PFN_vkCmdInsertDebugUtilsLabelEXT vkCmdInsertDebugUtilsLabelEXT = nullptr;
                if (!vkCmdInsertDebugUtilsLabelEXT)
                {
                    vkCmdInsertDebugUtilsLabelEXT = (PFN_vkCmdInsertDebugUtilsLabelEXT)
                        vkGetDeviceProcAddr(m_device->GetVkDevice(), "vkCmdInsertDebugUtilsLabelEXT");
                }

                if (vkCmdInsertDebugUtilsLabelEXT)
                {
                    VkDebugUtilsLabelEXT label{};
                    label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
                    label.pLabelName = name;
                    label.color[0] = ((color >> 24) & 0xFF) / 255.0f; // R
                    label.color[1] = ((color >> 16) & 0xFF) / 255.0f; // G
                    label.color[2] = ((color >> 8) & 0xFF) / 255.0f;  // B
                    label.color[3] = (color & 0xFF) / 255.0f;         // A

                    vkCmdInsertDebugUtilsLabelEXT(m_commandBuffer, &label);
                }
            }

            IRenderDevice *VulkanContext::GetDevice() const
            {
                return reinterpret_cast<IRenderDevice *>(m_device);
            }

            // 工具方法实现
            void VulkanContext::ConvertClearValues(const std::vector<Color4f> &clearValues, std::vector<VkClearValue> &vkClearValues)
            {
                vkClearValues.clear();
                vkClearValues.reserve(clearValues.size());

                for (const auto &clearValue : clearValues)
                {
                    VkClearValue vkClearValue{};
                    vkClearValue.color.float32[0] = clearValue.r;
                    vkClearValue.color.float32[1] = clearValue.g;
                    vkClearValue.color.float32[2] = clearValue.b;
                    vkClearValue.color.float32[3] = clearValue.a;
                    vkClearValues.push_back(vkClearValue);
                }
            }

            VkRect2D VulkanContext::ConvertRect2D(const Rect2D &rect)
            {
                VkRect2D vkRect{};
                vkRect.offset.x = rect.offset.x;
                vkRect.offset.y = rect.offset.y;
                vkRect.extent.width = rect.extent.width;
                vkRect.extent.height = rect.extent.height;
                return vkRect;
            }

            VkViewport VulkanContext::ConvertViewport(const Viewport &viewport)
            {
                VkViewport vkViewport{};
                vkViewport.x = viewport.x;
                vkViewport.y = viewport.y;
                vkViewport.width = viewport.width;
                vkViewport.height = viewport.height;
                vkViewport.minDepth = viewport.minDepth;
                vkViewport.maxDepth = viewport.maxDepth;
                return vkViewport;
            }

            VkIndexType VulkanContext::ConvertIndexType(uint32_t indexType)
            {
                switch (indexType)
                {
                case 16:
                    return VK_INDEX_TYPE_UINT16;
                case 32:
                    return VK_INDEX_TYPE_UINT32;
                default:
                    return VK_INDEX_TYPE_UINT16;
                }
            }

            VkShaderStageFlags VulkanContext::ConvertShaderStageFlags(ShaderStageFlag stageFlags)
            {
                VkShaderStageFlags vkFlags = 0;
                uint16_t flags = static_cast<uint16_t>(stageFlags);

                if (flags & static_cast<uint16_t>(ShaderStageFlag::Vertex))
                    vkFlags |= VK_SHADER_STAGE_VERTEX_BIT;
                if (flags & static_cast<uint16_t>(ShaderStageFlag::TessControl))
                    vkFlags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
                if (flags & static_cast<uint16_t>(ShaderStageFlag::TessEvaluation))
                    vkFlags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
                if (flags & static_cast<uint16_t>(ShaderStageFlag::Geometry))
                    vkFlags |= VK_SHADER_STAGE_GEOMETRY_BIT;
                if (flags & static_cast<uint16_t>(ShaderStageFlag::Fragment))
                    vkFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
                if (flags & static_cast<uint16_t>(ShaderStageFlag::Compute))
                    vkFlags |= VK_SHADER_STAGE_COMPUTE_BIT;

                return vkFlags;
            }

            VkPipelineStageFlags VulkanContext::ConvertResourceStateToPipelineStage(ResourceState state)
            {
                switch (state)
                {
                case ResourceState::Common:
                case ResourceState::Unknown:
                    return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                case ResourceState::VertexBuffer:
                    return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
                case ResourceState::IndexBuffer:
                    return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
                case ResourceState::ConstantBuffer:
                    return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                case ResourceState::ShaderRead:
                    return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                case ResourceState::ShaderWrite:
                    return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                case ResourceState::RenderTarget:
                    return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                case ResourceState::DepthStencilRead:
                case ResourceState::DepthStencilWrite:
                    return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                case ResourceState::CopySource:
                case ResourceState::CopyDest:
                    return VK_PIPELINE_STAGE_TRANSFER_BIT;
                case ResourceState::Present:
                    return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                default:
                    return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                }
            }

            VkAccessFlags VulkanContext::ConvertResourceStateToAccessFlags(ResourceState state)
            {
                switch (state)
                {
                case ResourceState::Common:
                case ResourceState::Unknown:
                    return 0;
                case ResourceState::VertexBuffer:
                    return VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
                case ResourceState::IndexBuffer:
                    return VK_ACCESS_INDEX_READ_BIT;
                case ResourceState::ConstantBuffer:
                    return VK_ACCESS_UNIFORM_READ_BIT;
                case ResourceState::ShaderRead:
                    return VK_ACCESS_SHADER_READ_BIT;
                case ResourceState::ShaderWrite:
                    return VK_ACCESS_SHADER_WRITE_BIT;
                case ResourceState::RenderTarget:
                    return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                case ResourceState::DepthStencilRead:
                    return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                case ResourceState::DepthStencilWrite:
                    return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                case ResourceState::CopySource:
                    return VK_ACCESS_TRANSFER_READ_BIT;
                case ResourceState::CopyDest:
                    return VK_ACCESS_TRANSFER_WRITE_BIT;
                case ResourceState::Present:
                    return VK_ACCESS_MEMORY_READ_BIT;
                default:
                    return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
                }
            }

            VkImageLayout VulkanContext::ConvertResourceStateToImageLayout(ResourceState state)
            {
                switch (state)
                {
                case ResourceState::Common:
                case ResourceState::Unknown:
                    return VK_IMAGE_LAYOUT_UNDEFINED;
                case ResourceState::ShaderRead:
                    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                case ResourceState::ShaderWrite:
                    return VK_IMAGE_LAYOUT_GENERAL;
                case ResourceState::RenderTarget:
                    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                case ResourceState::DepthStencilRead:
                    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                case ResourceState::DepthStencilWrite:
                    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                case ResourceState::CopySource:
                    return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                case ResourceState::CopyDest:
                    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                case ResourceState::Present:
                    return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                default:
                    return VK_IMAGE_LAYOUT_GENERAL;
                }
            }

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange
