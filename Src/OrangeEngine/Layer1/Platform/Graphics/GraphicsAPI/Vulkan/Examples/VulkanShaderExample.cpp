/**
 * @file VulkanShaderExample.cpp
 * @brief Vulkan着色器使用示例实现
 */

#ifdef ORANGE_VULKAN_ENABLED

#include "VulkanShaderExample.h"
#include <iostream>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {

            VulkanShaderExample::VulkanShaderExample(VulkanDevice *device)
                : m_device(device), m_vertexShader(nullptr), m_fragmentShader(nullptr)
            {
            }

            VulkanShaderExample::~VulkanShaderExample()
            {
                Cleanup();
            }

            bool VulkanShaderExample::Initialize()
            {
                if (!m_device)
                {
                    std::cerr << "VulkanShaderExample: 设备为空" << std::endl;
                    return false;
                }

                std::cout << "VulkanShaderExample: 开始初始化..." << std::endl;

                // 优先尝试从内置SPIR-V代码创建着色器
                if (DemoCreateFromBuiltinSPIRV())
                {
                    std::cout << "VulkanShaderExample: 从内置SPIR-V代码创建着色器成功" << std::endl;
                    return true;
                }

                // 备选方案：尝试从SPIR-V文件加载
                if (DemoLoadFromSPIRVFile())
                {
                    std::cout << "VulkanShaderExample: 从SPIR-V文件加载着色器成功" << std::endl;
                    return true;
                }

                // 最后尝试：从GLSL文件加载（需要编译器支持）
                if (DemoLoadFromGLSLFile())
                {
                    std::cout << "VulkanShaderExample: 从GLSL文件加载着色器成功" << std::endl;
                    return true;
                }

                std::cerr << "VulkanShaderExample: 所有着色器加载方法都失败了" << std::endl;
                return false;
            }

            void VulkanShaderExample::Cleanup()
            {
                m_vertexShader.reset();
                m_fragmentShader.reset();
                std::cout << "VulkanShaderExample: 清理完成" << std::endl;
            }

            bool VulkanShaderExample::DemoLoadFromSPIRVFile()
            {
                std::cout << "VulkanShaderExample: 尝试从SPIR-V文件加载着色器..." << std::endl;

                // 尝试加载现有的SPIR-V文件
                std::string vertexPath = "Shaders/triangle.vert.spv";
                std::string fragmentPath = "Shaders/triangle.frag.spv";

                try
                {
                    m_vertexShader = VulkanShader::LoadFromSPIRVFile(m_device, vertexPath, ShaderType::Vertex);
                    if (!ValidateShader(m_vertexShader, "顶点着色器（SPIR-V文件）"))
                    {
                        return false;
                    }

                    m_fragmentShader = VulkanShader::LoadFromSPIRVFile(m_device, fragmentPath, ShaderType::Fragment);
                    if (!ValidateShader(m_fragmentShader, "片段着色器（SPIR-V文件）"))
                    {
                        return false;
                    }

                    return true;
                }
                catch (const std::exception &e)
                {
                    std::cout << "VulkanShaderExample: 从SPIR-V文件加载失败: " << e.what() << std::endl;
                    return false;
                }
            }

            bool VulkanShaderExample::DemoCreateFromBuiltinSPIRV()
            {
                std::cout << "VulkanShaderExample: 尝试从内置SPIR-V代码创建着色器..." << std::endl;

                // 获取内置的SPIR-V代码
                auto vertexSpirv = VulkanShaderUtils::GetTriangleVertexShaderSPIRV();
                auto fragmentSpirv = VulkanShaderUtils::GetTriangleFragmentShaderSPIRV();

                // 验证SPIR-V代码
                if (!VulkanShaderUtils::ValidateSPIRVCode(vertexSpirv))
                {
                    std::cerr << "VulkanShaderExample: 内置顶点着色器SPIR-V代码无效" << std::endl;
                    return false;
                }

                if (!VulkanShaderUtils::ValidateSPIRVCode(fragmentSpirv))
                {
                    std::cerr << "VulkanShaderExample: 内置片段着色器SPIR-V代码无效" << std::endl;
                    return false;
                }

                // 创建着色器
                m_vertexShader = VulkanShader::CreateFromSPIRV(m_device, vertexSpirv, ShaderType::Vertex);
                if (!ValidateShader(m_vertexShader, "顶点着色器（内置SPIR-V）"))
                {
                    return false;
                }

                m_fragmentShader = VulkanShader::CreateFromSPIRV(m_device, fragmentSpirv, ShaderType::Fragment);
                if (!ValidateShader(m_fragmentShader, "片段着色器（内置SPIR-V）"))
                {
                    return false;
                }

                std::cout << "VulkanShaderExample: 内置SPIR-V着色器创建成功" << std::endl;
                return true;
            }

            bool VulkanShaderExample::DemoLoadFromGLSLFile()
            {
                std::cout << "VulkanShaderExample: 尝试从GLSL文件加载着色器..." << std::endl;

                // 尝试加载GLSL文件
                std::string vertexPath = "Shaders/triangle.vert";
                std::string fragmentPath = "Shaders/triangle.frag";

                m_vertexShader = VulkanShader::LoadFromGLSLFile(m_device, vertexPath, ShaderType::Vertex);
                if (!ValidateShader(m_vertexShader, "顶点着色器（GLSL文件）"))
                {
                    std::cout << "VulkanShaderExample: GLSL编译器未实现，无法从GLSL文件加载" << std::endl;
                    return false;
                }

                m_fragmentShader = VulkanShader::LoadFromGLSLFile(m_device, fragmentPath, ShaderType::Fragment);
                if (!ValidateShader(m_fragmentShader, "片段着色器（GLSL文件）"))
                {
                    return false;
                }

                return true;
            }

            std::vector<VkPipelineShaderStageCreateInfo> VulkanShaderExample::CreateShaderStages() const
            {
                std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

                if (m_vertexShader)
                {
                    shaderStages.push_back(m_vertexShader->GetStageCreateInfo());
                    std::cout << "VulkanShaderExample: 添加顶点着色器阶段" << std::endl;
                }

                if (m_fragmentShader)
                {
                    shaderStages.push_back(m_fragmentShader->GetStageCreateInfo());
                    std::cout << "VulkanShaderExample: 添加片段着色器阶段" << std::endl;
                }

                std::cout << "VulkanShaderExample: 创建了 " << shaderStages.size() << " 个着色器阶段" << std::endl;
                return shaderStages;
            }

            bool VulkanShaderExample::ValidateShader(std::shared_ptr<VulkanShader> shader, const std::string &shaderName)
            {
                if (!shader)
                {
                    std::cerr << "VulkanShaderExample: " << shaderName << " 创建失败" << std::endl;
                    return false;
                }

                if (shader->GetVkShaderModule() == VK_NULL_HANDLE)
                {
                    std::cerr << "VulkanShaderExample: " << shaderName << " Vulkan模块无效" << std::endl;
                    return false;
                }

                std::cout << "VulkanShaderExample: " << shaderName << " 验证成功" << std::endl;
                std::cout << "  类型: " << VulkanShaderUtils::GetShaderTypeName(shader->GetType()) << std::endl;
                std::cout << "  入口点: " << shader->GetEntryPoint() << std::endl;

                return true;
            }

            // 全局示例函数，方便直接调用
            bool RunVulkanShaderExample(VulkanDevice *device)
            {
                if (!device)
                {
                    std::cerr << "RunVulkanShaderExample: 设备为空" << std::endl;
                    return false;
                }

                std::cout << "========== Vulkan着色器示例开始 ==========" << std::endl;

                VulkanShaderExample example(device);
                bool success = example.Initialize();

                if (success)
                {
                    std::cout << "VulkanShaderExample: 示例运行成功！" << std::endl;

                    // 演示创建着色器阶段信息
                    auto shaderStages = example.CreateShaderStages();
                    std::cout << "VulkanShaderExample: 着色器阶段数量: " << shaderStages.size() << std::endl;
                }
                else
                {
                    std::cerr << "VulkanShaderExample: 示例运行失败" << std::endl;
                }

                std::cout << "========== Vulkan着色器示例结束 ==========" << std::endl;
                return success;
            }

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_ENABLED