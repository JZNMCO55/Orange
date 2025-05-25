/**
 * @file VulkanShader.cpp
 * @brief Vulkan着色器实现
 */

#ifdef ORANGE_VULKAN_ENABLED

#include "VulkanShader.h"
#include "../VulkanInterface/VulkanDevice.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {

            VulkanShader::VulkanShader(VulkanDevice *device)
                : m_device(device), m_shaderModule(VK_NULL_HANDLE), m_type(ShaderType::Vertex), m_entryPoint("main")
            {
            }

            VulkanShader::~VulkanShader()
            {
                Shutdown();
            }

            bool VulkanShader::Initialize(const ShaderCreateInfo &createInfo)
            {
                if (!m_device)
                {
                    return false;
                }

                m_type = createInfo.type;
                m_entryPoint = createInfo.entryPoint;

                // 根据着色器语言类型处理代码
                std::vector<uint8_t> spirvCode;

                if (createInfo.language == ShaderLanguage::SPIRV)
                {
                    // 直接使用提供的SPIR-V代码
                    spirvCode = createInfo.code;
                }
                else if (createInfo.language == ShaderLanguage::GLSL)
                {
                    // 编译GLSL到SPIR-V
                    if (!createInfo.filePath.empty())
                    {
                        // 从文件加载GLSL代码
                        std::string glslCode = LoadGLSLFromFile(createInfo.filePath);
                        if (glslCode.empty())
                        {
                            return false;
                        }
                        spirvCode = CompileGLSLToSPIRV(glslCode, createInfo.type, createInfo.entryPoint);
                    }
                    else if (!createInfo.code.empty())
                    {
                        // 使用提供的GLSL代码
                        std::string glslCode(createInfo.code.begin(), createInfo.code.end());
                        spirvCode = CompileGLSLToSPIRV(glslCode, createInfo.type, createInfo.entryPoint);
                    }
                    else
                    {
                        return false;
                    }
                }
                else
                {
                    // 不支持的着色器语言
                    return false;
                }

                if (spirvCode.empty())
                {
                    return false;
                }

                // 保存字节码
                m_bytecode = spirvCode;

                // 创建Vulkan着色器模块
                return CreateShaderModuleFromCode(spirvCode);
            }

            void VulkanShader::Shutdown()
            {
                if (m_shaderModule != VK_NULL_HANDLE)
                {
                    vkDestroyShaderModule(m_device->GetVkDevice(), m_shaderModule, nullptr);
                    m_shaderModule = VK_NULL_HANDLE;
                }

                m_bytecode.clear();
            }

            VkPipelineShaderStageCreateInfo VulkanShader::GetStageCreateInfo() const
            {
                VkPipelineShaderStageCreateInfo stageInfo{};
                stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stageInfo.stage = ConvertShaderTypeToVulkanStage(m_type);
                stageInfo.module = m_shaderModule;
                stageInfo.pName = m_entryPoint.c_str();
                // stageInfo.pSpecializationInfo = nullptr; // 可以用于着色器特化

                return stageInfo;
            }

            bool VulkanShader::CreateShaderModuleFromCode(const std::vector<uint8_t> &code)
            {
                if (code.empty() || code.size() % 4 != 0)
                {
                    return false; // SPIR-V代码必须是4字节对齐的
                }

                VkShaderModuleCreateInfo createInfo{};
                createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                createInfo.codeSize = code.size();
                createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

                VkResult result = vkCreateShaderModule(m_device->GetVkDevice(), &createInfo, nullptr, &m_shaderModule);
                return result == VK_SUCCESS;
            }

            std::vector<uint8_t> VulkanShader::LoadShaderFromFile(const std::string &filename)
            {
                std::ifstream file(filename, std::ios::ate | std::ios::binary);

                if (!file.is_open())
                {
                    throw std::runtime_error("无法打开着色器文件: " + filename);
                }

                size_t fileSize = static_cast<size_t>(file.tellg());
                std::vector<uint8_t> buffer(fileSize);

                file.seekg(0);
                file.read(reinterpret_cast<char *>(buffer.data()), fileSize);
                file.close();

                return buffer;
            }

            std::string VulkanShader::LoadGLSLFromFile(const std::string &filename)
            {
                std::ifstream file(filename);
                if (!file.is_open())
                {
                    std::cerr << "无法打开GLSL文件: " << filename << std::endl;
                    return "";
                }

                std::string content;
                std::string line;
                while (std::getline(file, line))
                {
                    content += line + "\n";
                }

                file.close();
                return content;
            }

            std::vector<uint8_t> VulkanShader::CompileGLSLToSPIRV(const std::string &glslCode, ShaderType type, const std::string &entryPoint)
            {
                // 简化版本：这里应该使用glslang或其他GLSL编译器来编译GLSL到SPIR-V
                // 现在返回空向量，表示编译失败
                // 在实际项目中，你需要集成glslang库或使用预编译的SPIR-V代码

                std::cerr << "GLSL到SPIR-V编译尚未实现。请使用预编译的SPIR-V代码或集成glslang库。" << std::endl;
                std::cerr << "着色器类型: " << static_cast<int>(type) << ", 入口点: " << entryPoint << std::endl;

                // 作为临时解决方案，你可以：
                // 1. 使用外部工具（如glslc）预编译GLSL到SPIR-V
                // 2. 集成glslang库进行运行时编译
                // 3. 直接提供SPIR-V字节码

                return std::vector<uint8_t>();
            }

            VkShaderStageFlagBits VulkanShader::ConvertShaderTypeToVulkanStage(ShaderType type) const
            {
                switch (type)
                {
                case ShaderType::Vertex:
                    return VK_SHADER_STAGE_VERTEX_BIT;
                case ShaderType::TessControl:
                    return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
                case ShaderType::TessEvaluation:
                    return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
                case ShaderType::Geometry:
                    return VK_SHADER_STAGE_GEOMETRY_BIT;
                case ShaderType::Fragment:
                    return VK_SHADER_STAGE_FRAGMENT_BIT;
                case ShaderType::Compute:
                    return VK_SHADER_STAGE_COMPUTE_BIT;
                case ShaderType::RayGen:
                    return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
                case ShaderType::AnyHit:
                    return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
                case ShaderType::ClosestHit:
                    return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
                case ShaderType::Miss:
                    return VK_SHADER_STAGE_MISS_BIT_KHR;
                case ShaderType::Intersection:
                    return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
                case ShaderType::Callable:
                    return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
                case ShaderType::Task:
                    return VK_SHADER_STAGE_TASK_BIT_NV;
                case ShaderType::Mesh:
                    return VK_SHADER_STAGE_MESH_BIT_NV;
                default:
                    return VK_SHADER_STAGE_VERTEX_BIT;
                }
            }

            // 静态工具函数：从SPIR-V文件加载着色器
            std::shared_ptr<VulkanShader> VulkanShader::LoadFromSPIRVFile(VulkanDevice *device, const std::string &filename, ShaderType type, const std::string &entryPoint)
            {
                auto shader = std::make_shared<VulkanShader>(device);

                try
                {
                    auto bytecode = shader->LoadShaderFromFile(filename);

                    ShaderCreateInfo createInfo{};
                    createInfo.type = type;
                    createInfo.language = ShaderLanguage::SPIRV;
                    createInfo.code = bytecode;
                    createInfo.entryPoint = entryPoint;
                    createInfo.filePath = filename;

                    if (!shader->Initialize(createInfo))
                    {
                        return nullptr;
                    }

                    return shader;
                }
                catch (const std::exception &e)
                {
                    std::cerr << "加载SPIR-V着色器失败: " << e.what() << std::endl;
                    return nullptr;
                }
            }

            // 静态工具函数：从GLSL文件加载着色器
            std::shared_ptr<VulkanShader> VulkanShader::LoadFromGLSLFile(VulkanDevice *device, const std::string &filename, ShaderType type, const std::string &entryPoint)
            {
                auto shader = std::make_shared<VulkanShader>(device);

                ShaderCreateInfo createInfo{};
                createInfo.type = type;
                createInfo.language = ShaderLanguage::GLSL;
                createInfo.entryPoint = entryPoint;
                createInfo.filePath = filename;

                if (!shader->Initialize(createInfo))
                {
                    return nullptr;
                }

                return shader;
            }

            // 静态工具函数：从内存中的SPIR-V代码创建着色器
            std::shared_ptr<VulkanShader> VulkanShader::CreateFromSPIRV(VulkanDevice *device, const std::vector<uint8_t> &spirvCode, ShaderType type, const std::string &entryPoint)
            {
                auto shader = std::make_shared<VulkanShader>(device);

                ShaderCreateInfo createInfo{};
                createInfo.type = type;
                createInfo.language = ShaderLanguage::SPIRV;
                createInfo.code = spirvCode;
                createInfo.entryPoint = entryPoint;

                if (!shader->Initialize(createInfo))
                {
                    return nullptr;
                }

                return shader;
            }

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_ENABLED
