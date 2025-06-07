#ifndef ORANGE_VULKAN_VULKANSHADERCOMPILER_H
#define ORANGE_VULKAN_VULKANSHADERCOMPILER_H

#include "../../GraphicsInterface/ShaderCompiler.h"
#include "Layer1/Core/FileSystem/FileSystem.h"
#include <shaderc/shaderc.hpp>
#include <memory>

namespace Orange::Graphics::Vulkan
{

    /**
     * @brief Vulkan着色器编译器实现
     * 使用Shaderc库将GLSL编译为SPIR-V字节码
     */
    class VulkanShaderCompiler : public ShaderCompiler
    {
    public:
        VulkanShaderCompiler();
        ~VulkanShaderCompiler() override;

        // ShaderCompiler接口实现
        ShaderCompileResult CompileFromFile(
            const std::string &filePath,
            const ShaderCompileOptions &options) override;

        ShaderCompileResult CompileFromSource(
            const std::string &source,
            const ShaderCompileOptions &options) override;

        // 新增：加载预编译的SPIR-V文件
        ShaderCompileResult LoadSPIRV(const std::string &filePath);

        // 新增：从 GLSL 文件编译并返回 SPIR-V 字节码
        ShaderCompileResult CompileGLSLFileToSPIRV(
            const std::string &glslFilePath,
            ShaderStage stage,
            const std::string &entryPoint = "main");

        // 新增：从 GLSL 源码编译并返回 SPIR-V 字节码
        ShaderCompileResult CompileGLSLToSPIRV(
            const std::string &glslSource,
            ShaderStage stage,
            const std::string &filename,
            const std::string &entryPoint = "main");

        std::string PreprocessSource(
            const std::string &source,
            const ShaderCompileOptions &options) override;

        bool ValidateSPIRV(const std::vector<uint32_t> &spirvBytecode) override;
        std::vector<uint32_t> OptimizeSPIRV(const std::vector<uint32_t> &spirvBytecode) override;
        void AddIncludePath(const std::string &path) override;
        void ClearIncludePaths() override;
        void SetGlobalMacro(const std::string &name, const std::string &value) override;
        void ClearGlobalMacros() override;
        std::string GetVersion() const override;
        std::vector<std::string> GetSupportedLanguages() const override;
        std::string DisassembleSPIRV(const std::vector<uint32_t> &spirvBytecode) override;

    private:
        // 辅助函数
        shaderc_shader_kind GetShadercStage(ShaderStage stage);
        void SetupCompileOptions(shaderc::CompileOptions &options, const ShaderCompileOptions &compileOptions);

        // 成员变量
        std::unique_ptr<shaderc::Compiler> m_compiler;
        std::vector<std::string> m_includePaths;
        std::unordered_map<std::string, std::string> m_globalMacros;
    };

} // namespace Orange::Graphics::Vulkan

#endif // ORANGE_VULKAN_VULKANSHADERCOMPILER_H