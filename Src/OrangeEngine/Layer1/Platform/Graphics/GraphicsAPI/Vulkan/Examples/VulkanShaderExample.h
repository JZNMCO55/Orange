/**
 * @file VulkanShaderExample.h
 * @brief Vulkan着色器使用示例
 */

#ifndef ORANGE_VULKAN_SHADER_EXAMPLE_H
#define ORANGE_VULKAN_SHADER_EXAMPLE_H

#ifdef ORANGE_VULKAN_ENABLED

#include "../VulkanResources/VulkanShader.h"
#include "../VulkanResources/VulkanShaderUtils.h"
#include "../VulkanInterface/VulkanDevice.h"
#include <memory>
#include <vector>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            /**
             * @brief Vulkan着色器使用示例类
             */
            class VulkanShaderExample
            {
            public:
                /**
                 * @brief 构造函数
                 * @param device Vulkan设备
                 */
                VulkanShaderExample(VulkanDevice *device);

                /**
                 * @brief 析构函数
                 */
                ~VulkanShaderExample();

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
                 * @brief 演示从SPIR-V文件加载着色器
                 * @return 是否成功
                 */
                bool DemoLoadFromSPIRVFile();

                /**
                 * @brief 演示从内置SPIR-V代码创建着色器
                 * @return 是否成功
                 */
                bool DemoCreateFromBuiltinSPIRV();

                /**
                 * @brief 演示从GLSL文件加载着色器（需要编译器支持）
                 * @return 是否成功
                 */
                bool DemoLoadFromGLSLFile();

                /**
                 * @brief 获取顶点着色器
                 * @return 顶点着色器实例
                 */
                std::shared_ptr<VulkanShader> GetVertexShader() const { return m_vertexShader; }

                /**
                 * @brief 获取片段着色器
                 * @return 片段着色器实例
                 */
                std::shared_ptr<VulkanShader> GetFragmentShader() const { return m_fragmentShader; }

                /**
                 * @brief 创建着色器阶段信息
                 * @return 着色器阶段信息数组
                 */
                std::vector<VkPipelineShaderStageCreateInfo> CreateShaderStages() const;

            private:
                VulkanDevice *m_device;
                std::shared_ptr<VulkanShader> m_vertexShader;
                std::shared_ptr<VulkanShader> m_fragmentShader;

                /**
                 * @brief 验证着色器是否有效
                 * @param shader 着色器实例
                 * @param shaderName 着色器名称（用于日志）
                 * @return 是否有效
                 */
                bool ValidateShader(std::shared_ptr<VulkanShader> shader, const std::string &shaderName);
            };

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_ENABLED

#endif // ORANGE_VULKAN_SHADER_EXAMPLE_H