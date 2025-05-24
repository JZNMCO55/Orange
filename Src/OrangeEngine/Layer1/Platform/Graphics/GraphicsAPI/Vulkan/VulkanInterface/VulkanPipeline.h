/**
 * @file VulkanPipeline.h
 * @brief Vulkan渲染管线实现
 */

#ifndef ORANGE_VULKAN_PIPELINE_H
#define ORANGE_VULKAN_PIPELINE_H

#include "../../../GraphicsInterface/RenderInterface/IRenderPipeline.h"
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
             * @brief Vulkan管线布局实现
             */
            class VulkanPipelineLayout : public IPipelineLayout
            {
            public:
                VulkanPipelineLayout(VulkanDevice *device);
                virtual ~VulkanPipelineLayout();

                // IPipelineLayout接口实现
                virtual uint32_t GetDescriptorSetLayoutCount() const override { return static_cast<uint32_t>(m_descriptorSetLayouts.size()); }
                virtual void *GetDescriptorSetLayout(uint32_t index) const override;
                virtual uint32_t GetPushConstantRangeCount() const override { return static_cast<uint32_t>(m_pushConstantRanges.size()); }
                virtual PushConstantRange GetPushConstantRange(uint32_t index) const override;

                // Vulkan特定方法
                VkPipelineLayout GetVkPipelineLayout() const { return m_pipelineLayout; }

                // 初始化方法
                bool Initialize(const PipelineLayoutCreateInfo &createInfo);
                void Shutdown();

            private:
                VulkanDevice *m_device;
                VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
                std::vector<void *> m_descriptorSetLayouts;
                std::vector<PushConstantRange> m_pushConstantRanges;
            };

            /**
             * @brief Vulkan渲染管线实现
             */
            class VulkanPipeline : public IRenderPipeline
            {
            public:
                VulkanPipeline(VulkanDevice *device);
                virtual ~VulkanPipeline();

                // IRenderPipeline接口实现
                virtual PipelineType GetType() const override { return m_type; }
                virtual uint32_t GetSubpass() const override { return m_subpass; }

                // Vulkan特定方法
                VkPipeline GetVkPipeline() const { return m_pipeline; }
                VkPipelineLayout GetVkPipelineLayout() const;

                // 初始化方法
                bool InitializeGraphics(const GraphicsPipelineCreateInfo &createInfo);
                bool InitializeCompute(const ComputePipelineCreateInfo &createInfo);
                void Shutdown();

            private:
                VulkanDevice *m_device;
                VkPipeline m_pipeline = VK_NULL_HANDLE;
                VulkanPipelineLayout *m_pipelineLayout = nullptr;
                PipelineType m_type = PipelineType::Graphics;
                uint32_t m_subpass = 0;

                // 工具方法
                VkPipelineVertexInputStateCreateInfo CreateVertexInputState(
                    const std::vector<VertexInputDescription> &vertexInputs,
                    std::vector<VkVertexInputBindingDescription> &bindings,
                    std::vector<VkVertexInputAttributeDescription> &attributes);
                VkPipelineInputAssemblyStateCreateInfo CreateInputAssemblyState(const InputAssemblyState &state);
                VkPipelineRasterizationStateCreateInfo CreateRasterizationState(const RasterizationState &state);
                VkPipelineMultisampleStateCreateInfo CreateMultisampleState(const MultisampleState &state);
                VkPipelineDepthStencilStateCreateInfo CreateDepthStencilState(const DepthStencilState &state);
                VkPipelineColorBlendStateCreateInfo CreateColorBlendState(
                    const ColorBlendState &state,
                    std::vector<VkPipelineColorBlendAttachmentState> &attachments);
                VkPipelineDynamicStateCreateInfo CreateDynamicState(
                    const DynamicState &state,
                    std::vector<VkDynamicState> &dynamicStates);
            };

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_PIPELINE_H