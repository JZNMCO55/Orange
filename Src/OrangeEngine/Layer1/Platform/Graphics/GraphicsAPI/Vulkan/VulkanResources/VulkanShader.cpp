/**
 * @file VulkanShader.cpp
 * @brief Vulkan着色器实现
 */

#include "VulkanShader.h"
#include "../VulkanInterface/VulkanDevice.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <shaderc/shaderc.hpp>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            // Shaderc特定的转换函数
            static shaderc_shader_kind ConvertShaderTypeToShadercKind(ShaderType type)
            {
                switch (type)
                {
                case ShaderType::Vertex:
                    return shaderc_vertex_shader;
                case ShaderType::TessControl:
                    return shaderc_tess_control_shader;
                case ShaderType::TessEvaluation:
                    return shaderc_tess_evaluation_shader;
                case ShaderType::Geometry:
                    return shaderc_geometry_shader;
                case ShaderType::Fragment:
                    return shaderc_fragment_shader;
                case ShaderType::Compute:
                    return shaderc_compute_shader;
                case ShaderType::RayGen:
                    return shaderc_raygen_shader;
                case ShaderType::AnyHit:
                    return shaderc_anyhit_shader;
                case ShaderType::ClosestHit:
                    return shaderc_closesthit_shader;
                case ShaderType::Miss:
                    return shaderc_miss_shader;
                case ShaderType::Intersection:
                    return shaderc_intersection_shader;
                case ShaderType::Callable:
                    return shaderc_callable_shader;
                case ShaderType::Task:
                    return shaderc_task_shader;
                case ShaderType::Mesh:
                    return shaderc_mesh_shader;
                default:
                    return shaderc_vertex_shader;
                }
            }

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
                // 使用shaderc进行GLSL到SPIR-V编译
                shaderc::Compiler compiler;
                shaderc::CompileOptions options;

                // 设置编译选项
                options.SetOptimizationLevel(shaderc_optimization_level_performance);
                options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
                options.SetTargetSpirv(shaderc_spirv_version_1_0);

                // 转换着色器类型
                shaderc_shader_kind shaderKind = ConvertShaderTypeToShadercKind(type);

                // 编译GLSL到SPIR-V
                shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
                    glslCode, shaderKind, "shader", entryPoint.c_str(), options);

                if (result.GetCompilationStatus() != shaderc_compilation_status_success)
                {
                    std::cerr << "GLSL编译错误: " << result.GetErrorMessage() << std::endl;
                    return std::vector<uint8_t>();
                }

                // 转换结果为字节数组
                std::vector<uint32_t> spirvData(result.cbegin(), result.cend());
                std::vector<uint8_t> spirvBytes;
                spirvBytes.resize(spirvData.size() * sizeof(uint32_t));
                std::memcpy(spirvBytes.data(), spirvData.data(), spirvBytes.size());

                std::cout << "GLSL编译成功，生成了 " << spirvBytes.size() << " 字节的SPIR-V代码" << std::endl;
                return spirvBytes;
            }

            std::vector<uint8_t> VulkanShader::CompileGLSLToSPIRV(const std::string &glslCode, ShaderType type, const std::string &entryPoint, const ShaderCompileOptions &options)
            {
                // 使用shaderc进行GLSL到SPIR-V编译
                shaderc::Compiler compiler;
                shaderc::CompileOptions compileOptions;

                // 设置基本编译选项
                if (options.optimize)
                {
                    compileOptions.SetOptimizationLevel(shaderc_optimization_level_performance);
                }
                else
                {
                    compileOptions.SetOptimizationLevel(shaderc_optimization_level_zero);
                }

                if (options.generateDebugInfo)
                {
                    compileOptions.SetGenerateDebugInfo();
                }

                compileOptions.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
                compileOptions.SetTargetSpirv(shaderc_spirv_version_1_0);

                // 添加宏定义
                for (const auto &macro : options.macroDefinitions)
                {
                    compileOptions.AddMacroDefinition(macro.first, macro.second);
                }

                // 设置包含路径（这里需要实现include resolver，暂时跳过）
                // TODO: 实现include resolver

                // 转换着色器类型
                shaderc_shader_kind shaderKind = ConvertShaderTypeToShadercKind(type);

                // 编译GLSL到SPIR-V
                shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
                    glslCode, shaderKind, options.sourceFileName.c_str(), entryPoint.c_str(), compileOptions);

                if (result.GetCompilationStatus() != shaderc_compilation_status_success)
                {
                    std::cerr << "GLSL编译错误 (" << options.sourceFileName << "): " << result.GetErrorMessage() << std::endl;
                    return std::vector<uint8_t>();
                }

                // 转换结果为字节数组
                std::vector<uint32_t> spirvData(result.cbegin(), result.cend());
                std::vector<uint8_t> spirvBytes;
                spirvBytes.resize(spirvData.size() * sizeof(uint32_t));
                std::memcpy(spirvBytes.data(), spirvData.data(), spirvBytes.size());

                std::cout << "GLSL编译成功 (" << options.sourceFileName << ")，生成了 " << spirvBytes.size() << " 字节的SPIR-V代码" << std::endl;
                return spirvBytes;
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

            // 静态工具函数：编译GLSL代码创建着色器
            std::shared_ptr<VulkanShader> VulkanShader::CompileFromGLSL(VulkanDevice *device, const std::string &glslCode, ShaderType type, const std::string &entryPoint, const ShaderCompileOptions &options)
            {
                auto shader = std::make_shared<VulkanShader>(device);

                // 编译GLSL到SPIR-V
                std::vector<uint8_t> spirvCode = shader->CompileGLSLToSPIRV(glslCode, type, entryPoint, options);
                if (spirvCode.empty())
                {
                    return nullptr;
                }

                ShaderCreateInfo createInfo{};
                createInfo.type = type;
                createInfo.language = ShaderLanguage::SPIRV;
                createInfo.code = spirvCode;
                createInfo.entryPoint = entryPoint;
                createInfo.name = options.sourceFileName;

                if (!shader->Initialize(createInfo))
                {
                    return nullptr;
                }

                return shader;
            }

            // 静态工具函数：从GLSL文件编译创建着色器
            std::shared_ptr<VulkanShader> VulkanShader::CompileFromGLSLFile(VulkanDevice *device, const std::string &filename, ShaderType type, const std::string &entryPoint, const ShaderCompileOptions &options)
            {
                auto shader = std::make_shared<VulkanShader>(device);

                // 加载GLSL文件
                std::string glslCode = shader->LoadGLSLFromFile(filename);
                if (glslCode.empty())
                {
                    std::cerr << "无法加载GLSL文件: " << filename << std::endl;
                    return nullptr;
                }

                // 设置源文件名
                ShaderCompileOptions compileOptions = options;
                if (compileOptions.sourceFileName == "shader")
                {
                    compileOptions.sourceFileName = filename;
                }

                // 编译GLSL到SPIR-V
                std::vector<uint8_t> spirvCode = shader->CompileGLSLToSPIRV(glslCode, type, entryPoint, compileOptions);
                if (spirvCode.empty())
                {
                    return nullptr;
                }

                ShaderCreateInfo createInfo{};
                createInfo.type = type;
                createInfo.language = ShaderLanguage::SPIRV;
                createInfo.code = spirvCode;
                createInfo.entryPoint = entryPoint;
                createInfo.filePath = filename;
                createInfo.name = compileOptions.sourceFileName;

                if (!shader->Initialize(createInfo))
                {
                    return nullptr;
                }

                return shader;
            }

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange
