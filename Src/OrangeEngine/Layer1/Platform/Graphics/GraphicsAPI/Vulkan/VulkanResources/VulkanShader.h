/**
 * @file VulkanShader.h
 * @brief Vulkan着色器实现
 */

#ifndef ORANGE_VULKAN_SHADER_H
#define ORANGE_VULKAN_SHADER_H

#include "../../../GraphicsInterface/RenderInterface/IRenderResources.h"
#include "../VulkanCommon/VulkanCommon.h"
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <shaderc/shaderc.hpp>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            class VulkanDevice;

            /**
             * @brief 着色器编译选项
             */
            struct ShaderCompileOptions
            {
                bool optimize = true;                                          ///< 是否优化
                bool generateDebugInfo = false;                                ///< 是否生成调试信息
                std::unordered_map<std::string, std::string> macroDefinitions; ///< 宏定义
                std::vector<std::string> includePaths;                         ///< 包含路径
                std::string sourceFileName = "shader";                         ///< 源文件名（用于错误报告）
            };

            /**
             * @brief Vulkan着色器模块实现
             */
            class VulkanShader : public IShaderModule
            {
            public:
                VulkanShader(VulkanDevice *device);
                virtual ~VulkanShader();

                // IShaderModule接口实现
                virtual ShaderType GetType() const override { return m_type; }
                virtual const std::string &GetEntryPoint() const override { return m_entryPoint; }

                // Vulkan特定方法
                VkShaderModule GetVkShaderModule() const { return m_shaderModule; }
                VkPipelineShaderStageCreateInfo GetStageCreateInfo() const;

                // 获取着色器字节码
                const std::vector<uint8_t> &GetBytecode() const { return m_bytecode; }

                // 检查着色器是否有效
                bool IsValid() const { return m_shaderModule != VK_NULL_HANDLE; }

                // 获取着色器大小
                size_t GetSize() const { return m_bytecode.size(); }

                // 初始化方法
                bool Initialize(const ShaderCreateInfo &createInfo);
                void Shutdown();

                // 静态工具方法
                static std::shared_ptr<VulkanShader> LoadFromSPIRVFile(VulkanDevice *device, const std::string &filename, ShaderType type, const std::string &entryPoint = "main");
                static std::shared_ptr<VulkanShader> LoadFromGLSLFile(VulkanDevice *device, const std::string &filename, ShaderType type, const std::string &entryPoint = "main");
                static std::shared_ptr<VulkanShader> CreateFromSPIRV(VulkanDevice *device, const std::vector<uint8_t> &spirvCode, ShaderType type, const std::string &entryPoint = "main");

                // 高级编译方法
                static std::shared_ptr<VulkanShader> CompileFromGLSL(VulkanDevice *device, const std::string &glslCode, ShaderType type, const std::string &entryPoint = "main", const ShaderCompileOptions &options = {});
                static std::shared_ptr<VulkanShader> CompileFromGLSLFile(VulkanDevice *device, const std::string &filename, ShaderType type, const std::string &entryPoint = "main", const ShaderCompileOptions &options = {});

            private:
                VulkanDevice *m_device;
                VkShaderModule m_shaderModule = VK_NULL_HANDLE;
                ShaderType m_type = ShaderType::Vertex;
                std::string m_entryPoint = "main";
                std::vector<uint8_t> m_bytecode;

                // 工具方法
                bool CreateShaderModuleFromCode(const std::vector<uint8_t> &code);
                std::vector<uint8_t> LoadShaderFromFile(const std::string &filename);
                std::string LoadGLSLFromFile(const std::string &filename);
                std::vector<uint8_t> CompileGLSLToSPIRV(const std::string &glslCode, ShaderType type, const std::string &entryPoint);
                std::vector<uint8_t> CompileGLSLToSPIRV(const std::string &glslCode, ShaderType type, const std::string &entryPoint, const ShaderCompileOptions &options);
                VkShaderStageFlagBits ConvertShaderTypeToVulkanStage(ShaderType type) const;

                // Shaderc特定的转换函数
                shaderc_shader_kind ConvertShaderTypeToShadercKind(ShaderType type) const;
            };

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_SHADER_H