/**
 * @file VulkanPipeline.cpp
 * @brief Vulkan渲染管线实现
 */

#ifdef ORANGE_VULKAN_ENABLED

#include "VulkanPipeline.h"
#include "VulkanDevice.h"
#include "../VulkanResources/VulkanShader.h"
#include <iostream>
#include <cassert>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {

            // ================================
            // VulkanPipelineLayout实现
            // ================================

            VulkanPipelineLayout::VulkanPipelineLayout(VulkanDevice *device)
                : m_device(device)
            {
            }

            VulkanPipelineLayout::~VulkanPipelineLayout()
            {
                Shutdown();
            }

            void *VulkanPipelineLayout::GetDescriptorSetLayout(uint32_t index) const
            {
                if (index >= m_descriptorSetLayouts.size())
                {
                    return nullptr;
                }
                return m_descriptorSetLayouts[index];
            }

            PushConstantRange VulkanPipelineLayout::GetPushConstantRange(uint32_t index) const
            {
                if (index >= m_pushConstantRanges.size())
                {
                    return {};
                }
                return m_pushConstantRanges[index];
            }

            IRenderDevice *VulkanPipelineLayout::GetDevice() const
            {
                return m_device;
            }

            bool VulkanPipelineLayout::Initialize(const PipelineLayoutCreateInfo &createInfo)
            {
                if (!m_device)
                {
                    std::cerr << "VulkanPipelineLayout: 设备为空" << std::endl;
                    return false;
                }

                // 保存描述符集布局和推送常量范围
                m_descriptorSetLayouts = createInfo.descriptorSetLayouts;
                m_pushConstantRanges = createInfo.pushConstantRanges;

                // 转换推送常量范围
                std::vector<VkPushConstantRange> vkPushConstantRanges;
                for (const auto &range : createInfo.pushConstantRanges)
                {
                    VkPushConstantRange vkRange{};
                    vkRange.stageFlags = static_cast<VkShaderStageFlags>(range.stageFlags);
                    vkRange.offset = range.offset;
                    vkRange.size = range.size;
                    vkPushConstantRanges.push_back(vkRange);
                }

                // 转换描述符集布局
                std::vector<VkDescriptorSetLayout> vkDescriptorSetLayouts;
                for (const auto &layout : createInfo.descriptorSetLayouts)
                {
                    if (layout)
                    {
                        vkDescriptorSetLayouts.push_back(static_cast<VkDescriptorSetLayout>(layout));
                    }
                }

                // 创建管线布局
                VkPipelineLayoutCreateInfo layoutInfo{};
                layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                layoutInfo.setLayoutCount = static_cast<uint32_t>(vkDescriptorSetLayouts.size());
                layoutInfo.pSetLayouts = vkDescriptorSetLayouts.data();
                layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(vkPushConstantRanges.size());
                layoutInfo.pPushConstantRanges = vkPushConstantRanges.data();

                VkResult result = vkCreatePipelineLayout(m_device->GetVkDevice(), &layoutInfo, nullptr, &m_pipelineLayout);
                if (result != VK_SUCCESS)
                {
                    std::cerr << "VulkanPipelineLayout: 创建管线布局失败，错误码: " << result << std::endl;
                    return false;
                }

                std::cout << "VulkanPipelineLayout: 管线布局创建成功" << std::endl;
                return true;
            }

            void VulkanPipelineLayout::Shutdown()
            {
                if (m_pipelineLayout != VK_NULL_HANDLE)
                {
                    vkDestroyPipelineLayout(m_device->GetVkDevice(), m_pipelineLayout, nullptr);
                    m_pipelineLayout = VK_NULL_HANDLE;
                }

                m_descriptorSetLayouts.clear();
                m_pushConstantRanges.clear();
            }

            // ================================
            // VulkanPipeline实现
            // ================================

            VulkanPipeline::VulkanPipeline(VulkanDevice *device)
                : m_device(device)
            {
            }

            VulkanPipeline::~VulkanPipeline()
            {
                Shutdown();
            }

            VkPipelineLayout VulkanPipeline::GetVkPipelineLayout() const
            {
                return m_pipelineLayout ? m_pipelineLayout->GetVkPipelineLayout() : VK_NULL_HANDLE;
            }

            IRenderDevice *VulkanPipeline::GetDevice() const
            {
                return m_device;
            }

            bool VulkanPipeline::InitializeGraphics(const GraphicsPipelineCreateInfo &createInfo)
            {
                if (!m_device)
                {
                    std::cerr << "VulkanPipeline: 设备为空" << std::endl;
                    return false;
                }

                m_type = PipelineType::Graphics;
                m_subpass = createInfo.subpass;

                // 保存渲染通道引用
                m_renderPass = static_cast<IRenderPass *>(createInfo.renderPass);

                // 创建管线布局（如果提供了）
                if (createInfo.pipelineLayout)
                {
                    m_pipelineLayout = static_cast<VulkanPipelineLayout *>(createInfo.pipelineLayout);
                }
                else
                {
                    std::cerr << "VulkanPipeline: 管线布局为空" << std::endl;
                    return false;
                }

                // 收集着色器阶段
                std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
                for (const auto &stage : createInfo.shaderStages)
                {
                    if (stage.shaderModule)
                    {
                        VulkanShader *vulkanShader = static_cast<VulkanShader *>(stage.shaderModule);
                        shaderStages.push_back(vulkanShader->GetStageCreateInfo());
                    }
                }

                if (shaderStages.empty())
                {
                    std::cerr << "VulkanPipeline: 没有有效的着色器阶段" << std::endl;
                    return false;
                }

                // 创建顶点输入状态
                std::vector<VkVertexInputBindingDescription> bindingDescriptions;
                std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
                VkPipelineVertexInputStateCreateInfo vertexInputInfo = CreateVertexInputState(
                    createInfo.vertexInputs, bindingDescriptions, attributeDescriptions);

                // 创建输入装配状态
                VkPipelineInputAssemblyStateCreateInfo inputAssembly = CreateInputAssemblyState(createInfo.inputAssembly);

                // 创建视口状态（使用动态状态）
                VkPipelineViewportStateCreateInfo viewportState{};
                viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                viewportState.viewportCount = 1;
                viewportState.scissorCount = 1;

                // 创建光栅化状态
                VkPipelineRasterizationStateCreateInfo rasterizer = CreateRasterizationState(createInfo.rasterization);

                // 创建多重采样状态
                VkPipelineMultisampleStateCreateInfo multisampling = CreateMultisampleState(createInfo.multisample);

                // 创建深度模板状态
                VkPipelineDepthStencilStateCreateInfo depthStencil = CreateDepthStencilState(createInfo.depthStencil);

                // 创建颜色混合状态
                std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
                VkPipelineColorBlendStateCreateInfo colorBlending = CreateColorBlendState(createInfo.colorBlend, colorBlendAttachments);

                // 创建动态状态
                std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
                VkPipelineDynamicStateCreateInfo dynamicState = CreateDynamicState(createInfo.dynamicState, dynamicStateEnables);

                // 创建图形管线
                VkGraphicsPipelineCreateInfo pipelineInfo{};
                pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
                pipelineInfo.pStages = shaderStages.data();
                pipelineInfo.pVertexInputState = &vertexInputInfo;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &rasterizer;
                pipelineInfo.pMultisampleState = &multisampling;
                pipelineInfo.pDepthStencilState = &depthStencil;
                pipelineInfo.pColorBlendState = &colorBlending;
                pipelineInfo.pDynamicState = &dynamicState;
                pipelineInfo.layout = m_pipelineLayout->GetVkPipelineLayout();
                pipelineInfo.renderPass = static_cast<VkRenderPass>(createInfo.renderPass);
                pipelineInfo.subpass = createInfo.subpass;
                pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
                pipelineInfo.basePipelineIndex = -1;

                VkResult result = vkCreateGraphicsPipelines(m_device->GetVkDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);
                if (result != VK_SUCCESS)
                {
                    std::cerr << "VulkanPipeline: 创建图形管线失败，错误码: " << result << std::endl;
                    return false;
                }

                std::cout << "VulkanPipeline: 图形管线创建成功" << std::endl;
                return true;
            }

            bool VulkanPipeline::InitializeCompute(const ComputePipelineCreateInfo &createInfo)
            {
                if (!m_device)
                {
                    std::cerr << "VulkanPipeline: 设备为空" << std::endl;
                    return false;
                }

                m_type = PipelineType::Compute;

                // 创建管线布局（如果提供了）
                if (createInfo.pipelineLayout)
                {
                    m_pipelineLayout = static_cast<VulkanPipelineLayout *>(createInfo.pipelineLayout);
                }
                else
                {
                    std::cerr << "VulkanPipeline: 管线布局为空" << std::endl;
                    return false;
                }

                // 获取计算着色器阶段信息
                if (!createInfo.computeShader.shaderModule)
                {
                    std::cerr << "VulkanPipeline: 计算着色器为空" << std::endl;
                    return false;
                }

                VulkanShader *computeShader = static_cast<VulkanShader *>(createInfo.computeShader.shaderModule);
                VkPipelineShaderStageCreateInfo shaderStageInfo = computeShader->GetStageCreateInfo();

                // 创建计算管线
                VkComputePipelineCreateInfo pipelineInfo{};
                pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
                pipelineInfo.stage = shaderStageInfo;
                pipelineInfo.layout = m_pipelineLayout->GetVkPipelineLayout();
                pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
                pipelineInfo.basePipelineIndex = -1;

                VkResult result = vkCreateComputePipelines(m_device->GetVkDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);
                if (result != VK_SUCCESS)
                {
                    std::cerr << "VulkanPipeline: 创建计算管线失败，错误码: " << result << std::endl;
                    return false;
                }

                std::cout << "VulkanPipeline: 计算管线创建成功" << std::endl;
                return true;
            }

            void VulkanPipeline::Shutdown()
            {
                if (m_pipeline != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(m_device->GetVkDevice(), m_pipeline, nullptr);
                    m_pipeline = VK_NULL_HANDLE;
                }

                // 注意：不要删除m_pipelineLayout和m_renderPass，它们由外部管理
                m_pipelineLayout = nullptr;
                m_renderPass = nullptr;
            }

            // ================================
            // 工具方法实现
            // ================================

            VkPipelineVertexInputStateCreateInfo VulkanPipeline::CreateVertexInputState(
                const std::vector<VertexInputDescription> &vertexInputs,
                std::vector<VkVertexInputBindingDescription> &bindings,
                std::vector<VkVertexInputAttributeDescription> &attributes)
            {
                bindings.clear();
                attributes.clear();

                for (const auto &input : vertexInputs)
                {
                    // 添加绑定描述
                    VkVertexInputBindingDescription bindingDesc{};
                    bindingDesc.binding = input.binding;
                    bindingDesc.stride = input.stride;
                    bindingDesc.inputRate = input.perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
                    bindings.push_back(bindingDesc);

                    // 添加属性描述
                    for (const auto &attribute : input.attributes)
                    {
                        VkVertexInputAttributeDescription attributeDesc{};
                        attributeDesc.binding = attribute.binding;
                        attributeDesc.location = attribute.location;
                        attributeDesc.format = VK_FORMAT_R32G32B32_SFLOAT; // 简化处理，应该根据实际格式转换
                        attributeDesc.offset = attribute.offset;
                        attributes.push_back(attributeDesc);
                    }
                }

                VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
                vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
                vertexInputInfo.pVertexBindingDescriptions = bindings.data();
                vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
                vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

                return vertexInputInfo;
            }

            VkPipelineInputAssemblyStateCreateInfo VulkanPipeline::CreateInputAssemblyState(const InputAssemblyState &state)
            {
                VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
                inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;

                // 转换图元拓扑
                switch (state.topology)
                {
                case PrimitiveTopology::PointList:
                    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                    break;
                case PrimitiveTopology::LineList:
                    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                    break;
                case PrimitiveTopology::LineStrip:
                    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
                    break;
                case PrimitiveTopology::TriangleList:
                    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                    break;
                case PrimitiveTopology::TriangleStrip:
                    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
                    break;
                default:
                    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                    break;
                }

                inputAssembly.primitiveRestartEnable = state.primitiveRestart ? VK_TRUE : VK_FALSE;

                return inputAssembly;
            }

            VkPipelineRasterizationStateCreateInfo VulkanPipeline::CreateRasterizationState(const RasterizationState &state)
            {
                VkPipelineRasterizationStateCreateInfo rasterizer{};
                rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
                rasterizer.depthClampEnable = state.depthClampEnable ? VK_TRUE : VK_FALSE;
                rasterizer.rasterizerDiscardEnable = state.rasterizerDiscardEnable ? VK_TRUE : VK_FALSE;

                // 转换多边形模式
                switch (state.polygonMode)
                {
                case PolygonMode::Fill:
                    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
                    break;
                case PolygonMode::Line:
                    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
                    break;
                case PolygonMode::Point:
                    rasterizer.polygonMode = VK_POLYGON_MODE_POINT;
                    break;
                default:
                    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
                    break;
                }

                rasterizer.lineWidth = state.lineWidth;

                // 转换面剔除模式
                switch (state.cullMode)
                {
                case CullMode::None:
                    rasterizer.cullMode = VK_CULL_MODE_NONE;
                    break;
                case CullMode::Front:
                    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
                    break;
                case CullMode::Back:
                    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
                    break;
                case CullMode::FrontAndBack:
                    rasterizer.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
                    break;
                default:
                    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
                    break;
                }

                // 转换正面定义
                rasterizer.frontFace = (state.frontFace == FrontFace::Clockwise) ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;

                rasterizer.depthBiasEnable = state.depthBiasEnable ? VK_TRUE : VK_FALSE;
                rasterizer.depthBiasConstantFactor = state.depthBiasConstantFactor;
                rasterizer.depthBiasClamp = state.depthBiasClamp;
                rasterizer.depthBiasSlopeFactor = state.depthBiasSlopeFactor;

                return rasterizer;
            }

            VkPipelineMultisampleStateCreateInfo VulkanPipeline::CreateMultisampleState(const MultisampleState &state)
            {
                VkPipelineMultisampleStateCreateInfo multisampling{};
                multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
                multisampling.sampleShadingEnable = state.sampleShadingEnable ? VK_TRUE : VK_FALSE;
                multisampling.rasterizationSamples = static_cast<VkSampleCountFlagBits>(state.rasterizationSamples);
                multisampling.minSampleShading = state.minSampleShading;
                multisampling.pSampleMask = state.sampleMask.empty() ? nullptr : state.sampleMask.data();
                multisampling.alphaToCoverageEnable = state.alphaToCoverageEnable ? VK_TRUE : VK_FALSE;
                multisampling.alphaToOneEnable = state.alphaToOneEnable ? VK_TRUE : VK_FALSE;

                return multisampling;
            }

            VkPipelineDepthStencilStateCreateInfo VulkanPipeline::CreateDepthStencilState(const DepthStencilState &state)
            {
                VkPipelineDepthStencilStateCreateInfo depthStencil{};
                depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
                depthStencil.depthTestEnable = state.depthTestEnable ? VK_TRUE : VK_FALSE;
                depthStencil.depthWriteEnable = state.depthWriteEnable ? VK_TRUE : VK_FALSE;

                // 转换深度比较操作
                switch (state.depthCompareOp)
                {
                case CompareOp::Never:
                    depthStencil.depthCompareOp = VK_COMPARE_OP_NEVER;
                    break;
                case CompareOp::Less:
                    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
                    break;
                case CompareOp::Equal:
                    depthStencil.depthCompareOp = VK_COMPARE_OP_EQUAL;
                    break;
                case CompareOp::LessOrEqual:
                    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
                    break;
                case CompareOp::Greater:
                    depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER;
                    break;
                case CompareOp::NotEqual:
                    depthStencil.depthCompareOp = VK_COMPARE_OP_NOT_EQUAL;
                    break;
                case CompareOp::GreaterOrEqual:
                    depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
                    break;
                case CompareOp::Always:
                    depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
                    break;
                default:
                    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
                    break;
                }

                depthStencil.depthBoundsTestEnable = state.depthBoundsTestEnable ? VK_TRUE : VK_FALSE;
                depthStencil.minDepthBounds = state.minDepthBounds;
                depthStencil.maxDepthBounds = state.maxDepthBounds;
                depthStencil.stencilTestEnable = state.stencilTestEnable ? VK_TRUE : VK_FALSE;

                // 设置模板操作（简化处理）
                depthStencil.front = {};
                depthStencil.back = {};

                return depthStencil;
            }

            VkPipelineColorBlendStateCreateInfo VulkanPipeline::CreateColorBlendState(
                const ColorBlendState &state,
                std::vector<VkPipelineColorBlendAttachmentState> &attachments)
            {
                attachments.clear();

                for (const auto &attachment : state.attachments)
                {
                    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
                    colorBlendAttachment.colorWriteMask = attachment.colorWriteMask;
                    colorBlendAttachment.blendEnable = attachment.blendEnable ? VK_TRUE : VK_FALSE;

                    if (attachment.blendEnable)
                    {
                        // 简化的混合因子转换
                        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
                        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
                    }

                    attachments.push_back(colorBlendAttachment);
                }

                VkPipelineColorBlendStateCreateInfo colorBlending{};
                colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                colorBlending.logicOpEnable = state.logicOpEnable ? VK_TRUE : VK_FALSE;
                colorBlending.logicOp = VK_LOGIC_OP_COPY;
                colorBlending.attachmentCount = static_cast<uint32_t>(attachments.size());
                colorBlending.pAttachments = attachments.data();
                colorBlending.blendConstants[0] = state.blendConstants[0];
                colorBlending.blendConstants[1] = state.blendConstants[1];
                colorBlending.blendConstants[2] = state.blendConstants[2];
                colorBlending.blendConstants[3] = state.blendConstants[3];

                return colorBlending;
            }

            VkPipelineDynamicStateCreateInfo VulkanPipeline::CreateDynamicState(
                const DynamicState &state,
                std::vector<VkDynamicState> &dynamicStates)
            {
                // 默认添加视口和裁剪区域动态状态
                if (dynamicStates.empty())
                {
                    dynamicStates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
                    dynamicStates.push_back(VK_DYNAMIC_STATE_SCISSOR);
                }

                VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
                dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
                dynamicStateInfo.pDynamicStates = dynamicStates.data();

                return dynamicStateInfo;
            }

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_ENABLED