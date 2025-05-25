/**
 * @file VulkanRenderPass.cpp
 * @brief Vulkan渲染通道和帧缓冲区实现
 */

#include "VulkanRenderPass.h"
#include "VulkanDevice.h"
#include "../VulkanCommon/VulkanCommon.h"
#include <iostream>
#include <cassert>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {

            // ================================
            // VulkanRenderPass实现
            // ================================

            VulkanRenderPass::VulkanRenderPass(VulkanDevice *device)
                : m_device(device)
            {
            }

            VulkanRenderPass::~VulkanRenderPass()
            {
                Shutdown();
            }

            AttachmentDescription VulkanRenderPass::GetAttachmentDescription(uint32_t index) const
            {
                if (index >= m_attachments.size())
                {
                    return {};
                }
                return m_attachments[index];
            }

            SubpassDescription VulkanRenderPass::GetSubpassDescription(uint32_t index) const
            {
                if (index >= m_subpasses.size())
                {
                    return {};
                }
                return m_subpasses[index];
            }

            IFramebuffer *VulkanRenderPass::CreateFramebuffer(const FramebufferCreateInfo &createInfo)
            {
                if (!m_device)
                {
                    std::cerr << "VulkanRenderPass: 设备为空" << std::endl;
                    return nullptr;
                }

                VulkanFramebuffer *framebuffer = new VulkanFramebuffer(m_device);
                if (!framebuffer->Initialize(createInfo, m_renderPass))
                {
                    delete framebuffer;
                    return nullptr;
                }

                return framebuffer;
            }

            bool VulkanRenderPass::Initialize(const RenderPassCreateInfo &createInfo)
            {
                if (!m_device)
                {
                    std::cerr << "VulkanRenderPass: 设备为空" << std::endl;
                    return false;
                }

                // 保存创建信息
                m_attachments = createInfo.attachments;
                m_subpasses = createInfo.subpasses;
                m_dependencies = createInfo.dependencies;

                // 转换附件描述
                std::vector<VkAttachmentDescription> vkAttachments;
                for (const auto &attachment : createInfo.attachments)
                {
                    vkAttachments.push_back(ConvertAttachmentDescription(attachment));
                }

                // 转换子通道描述
                std::vector<VkSubpassDescription> vkSubpasses;
                std::vector<std::vector<VkAttachmentReference>> colorRefsStorage;
                std::vector<std::vector<VkAttachmentReference>> resolveRefsStorage;
                std::vector<VkAttachmentReference> depthRefsStorage;

                colorRefsStorage.resize(createInfo.subpasses.size());
                resolveRefsStorage.resize(createInfo.subpasses.size());
                depthRefsStorage.resize(createInfo.subpasses.size());

                for (size_t i = 0; i < createInfo.subpasses.size(); ++i)
                {
                    VkSubpassDescription vkSubpass = ConvertSubpassDescription(
                        createInfo.subpasses[i],
                        colorRefsStorage[i],
                        resolveRefsStorage[i],
                        depthRefsStorage[i]);
                    vkSubpasses.push_back(vkSubpass);
                }

                // 转换子通道依赖
                std::vector<VkSubpassDependency> vkDependencies;
                for (const auto &dependency : createInfo.dependencies)
                {
                    vkDependencies.push_back(ConvertSubpassDependency(dependency));
                }

                // 创建渲染通道
                VkRenderPassCreateInfo renderPassInfo{};
                renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
                renderPassInfo.attachmentCount = static_cast<uint32_t>(vkAttachments.size());
                renderPassInfo.pAttachments = vkAttachments.data();
                renderPassInfo.subpassCount = static_cast<uint32_t>(vkSubpasses.size());
                renderPassInfo.pSubpasses = vkSubpasses.data();
                renderPassInfo.dependencyCount = static_cast<uint32_t>(vkDependencies.size());
                renderPassInfo.pDependencies = vkDependencies.data();

                VkResult result = vkCreateRenderPass(m_device->GetVkDevice(), &renderPassInfo, nullptr, &m_renderPass);
                if (result != VK_SUCCESS)
                {
                    std::cerr << "VulkanRenderPass: 创建渲染通道失败，错误码: " << result << std::endl;
                    return false;
                }

                std::cout << "VulkanRenderPass: 渲染通道创建成功，附件数量: " << vkAttachments.size()
                          << "，子通道数量: " << vkSubpasses.size() << std::endl;
                return true;
            }

            void VulkanRenderPass::Shutdown()
            {
                if (m_renderPass != VK_NULL_HANDLE)
                {
                    vkDestroyRenderPass(m_device->GetVkDevice(), m_renderPass, nullptr);
                    m_renderPass = VK_NULL_HANDLE;
                }

                m_attachments.clear();
                m_subpasses.clear();
                m_dependencies.clear();
            }

            VkAttachmentDescription VulkanRenderPass::ConvertAttachmentDescription(const AttachmentDescription &desc)
            {
                VkAttachmentDescription vkAttachment{};
                vkAttachment.format = ToVulkanFormat(desc.format);
                vkAttachment.samples = static_cast<VkSampleCountFlagBits>(desc.samples);

                // 转换加载操作
                vkAttachment.loadOp = desc.loadOp ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
                vkAttachment.storeOp = desc.storeOp ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;

                // 转换模板操作
                vkAttachment.stencilLoadOp = desc.stencilLoadOp ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
                vkAttachment.stencilStoreOp = desc.stencilStoreOp ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;

                // 转换布局
                vkAttachment.initialLayout = ConvertResourceStateToImageLayout(desc.initialLayout);
                vkAttachment.finalLayout = ConvertResourceStateToImageLayout(desc.finalLayout);

                return vkAttachment;
            }

            VkSubpassDescription VulkanRenderPass::ConvertSubpassDescription(
                const SubpassDescription &desc,
                std::vector<VkAttachmentReference> &colorRefs,
                std::vector<VkAttachmentReference> &resolveRefs,
                VkAttachmentReference &depthRef)
            {
                VkSubpassDescription vkSubpass{};
                vkSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

                // 转换颜色附件引用
                colorRefs.clear();
                for (const auto &colorAttachment : desc.colorAttachments)
                {
                    VkAttachmentReference colorRef{};
                    colorRef.attachment = colorAttachment.attachment;
                    colorRef.layout = ConvertResourceStateToImageLayout(colorAttachment.layout);
                    colorRefs.push_back(colorRef);
                }
                vkSubpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
                vkSubpass.pColorAttachments = colorRefs.empty() ? nullptr : colorRefs.data();

                // 转换解析附件引用
                resolveRefs.clear();
                for (const auto &resolveAttachment : desc.resolveAttachments)
                {
                    VkAttachmentReference resolveRef{};
                    resolveRef.attachment = resolveAttachment.attachment;
                    resolveRef.layout = ConvertResourceStateToImageLayout(resolveAttachment.layout);
                    resolveRefs.push_back(resolveRef);
                }
                vkSubpass.pResolveAttachments = resolveRefs.empty() ? nullptr : resolveRefs.data();

                // 转换深度模板附件引用
                if (desc.depthStencilAttachment.attachment != UINT32_MAX)
                {
                    depthRef.attachment = desc.depthStencilAttachment.attachment;
                    depthRef.layout = ConvertResourceStateToImageLayout(desc.depthStencilAttachment.layout);
                    vkSubpass.pDepthStencilAttachment = &depthRef;
                }
                else
                {
                    vkSubpass.pDepthStencilAttachment = nullptr;
                }

                // 输入附件和保留附件暂时不实现
                vkSubpass.inputAttachmentCount = 0;
                vkSubpass.pInputAttachments = nullptr;
                vkSubpass.preserveAttachmentCount = 0;
                vkSubpass.pPreserveAttachments = nullptr;

                return vkSubpass;
            }

            VkSubpassDependency VulkanRenderPass::ConvertSubpassDependency(const SubpassDependency &dep)
            {
                VkSubpassDependency vkDependency{};
                vkDependency.srcSubpass = dep.srcSubpass;
                vkDependency.dstSubpass = dep.dstSubpass;
                vkDependency.srcStageMask = ConvertResourceStateToPipelineStage(dep.srcStageMask);
                vkDependency.dstStageMask = ConvertResourceStateToPipelineStage(dep.dstStageMask);
                vkDependency.srcAccessMask = ConvertResourceStateToAccessFlags(dep.srcAccessMask);
                vkDependency.dstAccessMask = ConvertResourceStateToAccessFlags(dep.dstAccessMask);
                vkDependency.dependencyFlags = dep.byRegion ? VK_DEPENDENCY_BY_REGION_BIT : 0;

                return vkDependency;
            }

            // ================================
            // VulkanFramebuffer实现
            // ================================

            VulkanFramebuffer::VulkanFramebuffer(VulkanDevice *device)
                : m_device(device)
            {
            }

            VulkanFramebuffer::~VulkanFramebuffer()
            {
                Shutdown();
            }

            IRenderTexture *VulkanFramebuffer::GetAttachment(uint32_t index) const
            {
                if (index >= m_attachments.size())
                {
                    return nullptr;
                }
                return m_attachments[index];
            }

            bool VulkanFramebuffer::Initialize(const FramebufferCreateInfo &createInfo, VkRenderPass renderPass)
            {
                if (!m_device)
                {
                    std::cerr << "VulkanFramebuffer: 设备为空" << std::endl;
                    return false;
                }

                if (renderPass == VK_NULL_HANDLE)
                {
                    std::cerr << "VulkanFramebuffer: 渲染通道为空" << std::endl;
                    return false;
                }

                // 保存基本信息
                m_width = createInfo.width;
                m_height = createInfo.height;
                m_layers = createInfo.layers;

                // 收集附件视图
                std::vector<VkImageView> attachmentViews;
                for (const auto &attachment : createInfo.attachments)
                {
                    if (attachment)
                    {
                        // 这里需要从IRenderTexture获取VkImageView
                        // 暂时使用void*转换，实际应该通过接口获取
                        VkImageView imageView = static_cast<VkImageView>(attachment);
                        attachmentViews.push_back(imageView);

                        // 保存附件引用
                        m_attachments.push_back(static_cast<IRenderTexture *>(attachment));
                    }
                }

                if (attachmentViews.empty())
                {
                    std::cerr << "VulkanFramebuffer: 没有有效的附件" << std::endl;
                    return false;
                }

                // 创建帧缓冲区
                VkFramebufferCreateInfo framebufferInfo{};
                framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                framebufferInfo.renderPass = renderPass;
                framebufferInfo.attachmentCount = static_cast<uint32_t>(attachmentViews.size());
                framebufferInfo.pAttachments = attachmentViews.data();
                framebufferInfo.width = m_width;
                framebufferInfo.height = m_height;
                framebufferInfo.layers = m_layers;

                VkResult result = vkCreateFramebuffer(m_device->GetVkDevice(), &framebufferInfo, nullptr, &m_framebuffer);
                if (result != VK_SUCCESS)
                {
                    std::cerr << "VulkanFramebuffer: 创建帧缓冲区失败，错误码: " << result << std::endl;
                    return false;
                }

                std::cout << "VulkanFramebuffer: 帧缓冲区创建成功，尺寸: " << m_width << "x" << m_height
                          << "，附件数量: " << attachmentViews.size() << std::endl;
                return true;
            }

            void VulkanFramebuffer::Shutdown()
            {
                if (m_framebuffer != VK_NULL_HANDLE)
                {
                    vkDestroyFramebuffer(m_device->GetVkDevice(), m_framebuffer, nullptr);
                    m_framebuffer = VK_NULL_HANDLE;
                }

                // 清理附件引用（不删除实际对象，由外部管理）
                m_attachments.clear();
                m_width = 0;
                m_height = 0;
                m_layers = 1;
            }

            // ================================
            // 工具函数实现
            // ================================

            VkImageLayout ConvertResourceStateToImageLayout(ResourceState state)
            {
                switch (state)
                {
                case ResourceState::Undefined:
                    return VK_IMAGE_LAYOUT_UNDEFINED;
                case ResourceState::Common:
                    return VK_IMAGE_LAYOUT_GENERAL;
                case ResourceState::RenderTarget:
                    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                case ResourceState::DepthStencilWrite:
                case ResourceState::DepthStencilRead:
                    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                case ResourceState::ShaderRead:
                    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                case ResourceState::Present:
                    return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                case ResourceState::CopySource:
                    return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                case ResourceState::CopyDest:
                    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                default:
                    return VK_IMAGE_LAYOUT_GENERAL;
                }
            }

            VkPipelineStageFlags ConvertResourceStateToPipelineStage(ResourceState state)
            {
                switch (state)
                {
                case ResourceState::Common:
                    return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                case ResourceState::RenderTarget:
                    return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                case ResourceState::DepthStencilWrite:
                case ResourceState::DepthStencilRead:
                    return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                case ResourceState::ShaderRead:
                    return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                case ResourceState::Present:
                    return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                case ResourceState::CopySource:
                case ResourceState::CopyDest:
                    return VK_PIPELINE_STAGE_TRANSFER_BIT;
                default:
                    return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                }
            }

            VkAccessFlags ConvertResourceStateToAccessFlags(ResourceState state)
            {
                switch (state)
                {
                case ResourceState::Common:
                    return 0;
                case ResourceState::RenderTarget:
                    return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                case ResourceState::DepthStencilWrite:
                    return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                case ResourceState::DepthStencilRead:
                    return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                case ResourceState::ShaderRead:
                    return VK_ACCESS_SHADER_READ_BIT;
                case ResourceState::Present:
                    return 0;
                case ResourceState::CopySource:
                    return VK_ACCESS_TRANSFER_READ_BIT;
                case ResourceState::CopyDest:
                    return VK_ACCESS_TRANSFER_WRITE_BIT;
                default:
                    return 0;
                }
            }

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange