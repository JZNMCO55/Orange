/**
 * @file VulkanRenderPass.h
 * @brief Vulkan渲染通道和帧缓冲区实现
 */

#ifndef ORANGE_VULKAN_RENDER_PASS_H
#define ORANGE_VULKAN_RENDER_PASS_H

#include "../../../GraphicsInterface/RenderInterface/IRenderPass.h"
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
             * @brief Vulkan渲染通道实现
             */
            class VulkanRenderPass : public IRenderPass
            {
            public:
                VulkanRenderPass(VulkanDevice *device);
                virtual ~VulkanRenderPass();

                // IRenderPass接口实现
                virtual uint32_t GetAttachmentCount() const override { return static_cast<uint32_t>(m_attachments.size()); }
                virtual AttachmentDescription GetAttachmentDescription(uint32_t index) const override;
                virtual uint32_t GetSubpassCount() const override { return static_cast<uint32_t>(m_subpasses.size()); }
                virtual SubpassDescription GetSubpassDescription(uint32_t index) const override;
                virtual IFramebuffer *CreateFramebuffer(const FramebufferCreateInfo &createInfo) override;

                // Vulkan特定方法
                VkRenderPass GetVkRenderPass() const { return m_renderPass; }

                // 初始化方法
                bool Initialize(const RenderPassCreateInfo &createInfo);
                void Shutdown();

            private:
                VulkanDevice *m_device;
                VkRenderPass m_renderPass = VK_NULL_HANDLE;
                std::vector<AttachmentDescription> m_attachments;
                std::vector<SubpassDescription> m_subpasses;
                std::vector<SubpassDependency> m_dependencies;

                // 工具方法
                VkAttachmentDescription ConvertAttachmentDescription(const AttachmentDescription &desc);
                VkSubpassDescription ConvertSubpassDescription(const SubpassDescription &desc,
                                                               std::vector<VkAttachmentReference> &colorRefs,
                                                               std::vector<VkAttachmentReference> &resolveRefs,
                                                               VkAttachmentReference &depthRef);
                VkSubpassDependency ConvertSubpassDependency(const SubpassDependency &dep);
            };

            /**
             * @brief Vulkan帧缓冲区实现
             */
            class VulkanFramebuffer : public IFramebuffer
            {
            public:
                VulkanFramebuffer(VulkanDevice *device);
                virtual ~VulkanFramebuffer();

                // IFramebuffer接口实现
                virtual uint32_t GetWidth() const override { return m_width; }
                virtual uint32_t GetHeight() const override { return m_height; }
                virtual uint32_t GetLayers() const override { return m_layers; }
                virtual uint32_t GetAttachmentCount() const override { return static_cast<uint32_t>(m_attachments.size()); }
                virtual IRenderTexture *GetAttachment(uint32_t index) const override;

                // Vulkan特定方法
                VkFramebuffer GetVkFramebuffer() const { return m_framebuffer; }

                // 初始化方法
                bool Initialize(const FramebufferCreateInfo &createInfo, VkRenderPass renderPass);
                void Shutdown();

            private:
                VulkanDevice *m_device;
                VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
                uint32_t m_width = 0;
                uint32_t m_height = 0;
                uint32_t m_layers = 1;
                std::vector<IRenderTexture *> m_attachments;
            };

            // ================================
            // 工具函数声明
            // ================================

            /**
             * @brief 将ResourceState转换为VkImageLayout
             * @param state 资源状态
             * @return Vulkan图像布局
             */
            VkImageLayout ConvertResourceStateToImageLayout(ResourceState state);

            /**
             * @brief 将ResourceState转换为VkPipelineStageFlags
             * @param state 资源状态
             * @return Vulkan管线阶段标志
             */
            VkPipelineStageFlags ConvertResourceStateToPipelineStage(ResourceState state);

            /**
             * @brief 将ResourceState转换为VkAccessFlags
             * @param state 资源状态
             * @return Vulkan访问标志
             */
            VkAccessFlags ConvertResourceStateToAccessFlags(ResourceState state);

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_RENDER_PASS_H