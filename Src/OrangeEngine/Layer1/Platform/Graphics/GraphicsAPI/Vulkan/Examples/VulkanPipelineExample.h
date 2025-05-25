/**
 * @file VulkanPipelineExample.h
 * @brief Vulkan渲染管线使用示例
 */

#ifndef ORANGE_VULKAN_PIPELINE_EXAMPLE_H
#define ORANGE_VULKAN_PIPELINE_EXAMPLE_H

#ifdef ORANGE_VULKAN_ENABLED

#include "../VulkanInterface/VulkanPipeline.h"
#include "../VulkanInterface/VulkanDevice.h"
#include "../VulkanInterface/VulkanRenderPass.h"
#include "../VulkanResources/VulkanShader.h"
#include <memory>
#include <vector>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            /**
             * @brief Vulkan渲染管线使用示例类
             */
            class VulkanPipelineExample
            {
            public:
                /**
                 * @brief 构造函数
                 * @param device Vulkan设备
                 */
                VulkanPipelineExample(VulkanDevice *device);

                /**
                 * @brief 析构函数
                 */
                ~VulkanPipelineExample();

                /**
                 * @brief 初始化示例
                 * @return 是否成功
                 */
                bool Initialize();

                /**
                 * @brief 清理资源
                 */
                void Cleanup();

                /**
                 * @brief 演示创建基本的三角形渲染管线
                 * @return 是否成功
                 */
                bool DemoCreateTrianglePipeline();

                /**
                 * @brief 演示创建计算管线
                 * @return 是否成功
                 */
                bool DemoCreateComputePipeline();

                /**
                 * @brief 演示管线布局创建
                 * @return 是否成功
                 */
                bool DemoCreatePipelineLayout();

                /**
                 * @brief 获取图形管线
                 * @return 图形管线实例
                 */
                std::shared_ptr<VulkanPipeline> GetGraphicsPipeline() const { return m_graphicsPipeline; }

                /**
                 * @brief 获取计算管线
                 * @return 计算管线实例
                 */
                std::shared_ptr<VulkanPipeline> GetComputePipeline() const { return m_computePipeline; }

                /**
                 * @brief 获取管线布局
                 * @return 管线布局实例
                 */
                std::shared_ptr<VulkanPipelineLayout> GetPipelineLayout() const { return m_pipelineLayout; }

            private:
                VulkanDevice *m_device;
                std::shared_ptr<VulkanPipelineLayout> m_pipelineLayout;
                std::shared_ptr<VulkanPipeline> m_graphicsPipeline;
                std::shared_ptr<VulkanPipeline> m_computePipeline;
                std::shared_ptr<VulkanRenderPass> m_renderPass;
                std::shared_ptr<VulkanShader> m_vertexShader;
                std::shared_ptr<VulkanShader> m_fragmentShader;
                std::shared_ptr<VulkanShader> m_computeShader;

                /**
                 * @brief 创建测试用的渲染通道
                 * @return 是否成功
                 */
                bool CreateTestRenderPass();

                /**
                 * @brief 创建测试用的着色器
                 * @return 是否成功
                 */
                bool CreateTestShaders();

                /**
                 * @brief 验证管线是否有效
                 * @param pipeline 管线实例
                 * @param pipelineName 管线名称（用于日志）
                 * @return 是否有效
                 */
                bool ValidatePipeline(std::shared_ptr<VulkanPipeline> pipeline, const std::string &pipelineName);
            };

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_ENABLED

#endif // ORANGE_VULKAN_PIPELINE_EXAMPLE_H