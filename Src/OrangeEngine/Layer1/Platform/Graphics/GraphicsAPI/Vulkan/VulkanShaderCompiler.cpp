#include "VulkanShaderCompiler.h"
#include "Orange.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace Orange::Graphics::Vulkan
{

    VulkanShaderCompiler::VulkanShaderCompiler()
        : m_compiler(std::make_unique<shaderc::Compiler>())
    {
        ORG_LOG_INFO("VulkanShaderCompiler initialized");
    }

    VulkanShaderCompiler::~VulkanShaderCompiler()
    {
        ORG_LOG_INFO("VulkanShaderCompiler destroyed");
    }

    ShaderCompileResult VulkanShaderCompiler::CompileFromFile(
        const std::string &filePath,
        const ShaderCompileOptions &options)
    {
        // 尝试先加载预编译的SPIR-V文件
        std::string spirvPath = filePath + ".spv";
        if (Orange::Core::FileSystem::FileExists(spirvPath))
        {
            return LoadSPIRV(spirvPath);
        }

        // 如果没有预编译文件，则动态编译GLSL
        return CompileGLSLFileToSPIRV(filePath, options.stage, options.entryPoint);
    }

    ShaderCompileResult VulkanShaderCompiler::CompileFromSource(
        const std::string &source,
        const ShaderCompileOptions &options)
    {
        // 使用Shaderc编译GLSL源码到SPIR-V
        shaderc::Compiler compiler;
        shaderc::CompileOptions compileOptions;

        // 设置编译选项
        SetupCompileOptions(compileOptions, options);

        shaderc_shader_kind kind = GetShadercStage(options.stage);

        // 编译着色器
        shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
            source,
            kind,
            options.debugName.c_str(),
            options.entryPoint.c_str(),
            compileOptions);

        ShaderCompileResult compileResult;
        if (result.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            compileResult.success = false;
            compileResult.errorMessage = result.GetErrorMessage();
            ORG_LOG_ERROR("Shader compilation failed: {}", compileResult.errorMessage);
            return compileResult;
        }

        // 编译成功，复制SPIR-V数据
        compileResult.success = true;
        compileResult.spirvBytecode.assign(result.cbegin(), result.cend());
        ORG_LOG_INFO("Shader compiled successfully. SPIR-V size: {} bytes", compileResult.spirvBytecode.size() * 4);

        if (!result.GetErrorMessage().empty())
        {
            compileResult.warningMessage = result.GetErrorMessage();
        }

        return compileResult;
    }

    ShaderCompileResult VulkanShaderCompiler::LoadSPIRV(const std::string &filePath)
    {
        ShaderCompileResult result;

        try
        {
            // 使用FileSystem读取SPIR-V文件
            std::vector<uint32_t> spirvData = Orange::Core::FileSystem::ReadBinaryFileAsUint32(filePath);

            if (spirvData.empty())
            {
                result.success = false;
                result.errorMessage = "Failed to read SPIR-V file or file is empty: " + filePath;
                ORG_LOG_ERROR("Failed to read SPIR-V file or file is empty: {}", filePath);
                return result;
            }

            result.success = true;
            result.spirvBytecode = std::move(spirvData);
            ORG_LOG_INFO("Successfully loaded SPIR-V file: {} ({} bytes)", filePath, result.spirvBytecode.size() * 4);

            return result;
        }
        catch (const std::exception &e)
        {
            result.success = false;
            result.errorMessage = "Exception while loading SPIR-V file: " + std::string(e.what());
            ORG_LOG_ERROR("Exception while loading SPIR-V file: {}", e.what());
            return result;
        }
    }

    std::string VulkanShaderCompiler::PreprocessSource(
        const std::string &source,
        const ShaderCompileOptions &options)
    {
        try
        {
            shaderc::CompileOptions compileOptions;
            SetupCompileOptions(compileOptions, options);

            shaderc_shader_kind kind = GetShadercStage(options.stage);

            auto result = m_compiler->PreprocessGlsl(
                source,
                kind,
                options.debugName.c_str(),
                compileOptions);

            if (result.GetCompilationStatus() != shaderc_compilation_status_success)
            {
                std::cerr << "Preprocessing failed: " << result.GetErrorMessage() << std::endl;
                return "";
            }

            return std::string(result.cbegin(), result.cend());
        }
        catch (const std::exception &e)
        {
            std::cerr << "Exception during preprocessing: " << e.what() << std::endl;
            return "";
        }
    }

    bool VulkanShaderCompiler::ValidateSPIRV(const std::vector<uint32_t> &spirvBytecode)
    {
        ORG_LOG_DEBUG("ValidateSPIRV called (not fully implemented yet)");
        // 简单验证：检查SPIR-V魔术数字
        if (spirvBytecode.empty() || spirvBytecode[0] != 0x07230203)
        {
            return false;
        }
        return true;
    }

    std::vector<uint32_t> VulkanShaderCompiler::OptimizeSPIRV(const std::vector<uint32_t> &spirvBytecode)
    {
        ORG_LOG_DEBUG("OptimizeSPIRV called (not implemented yet)");
        // 目前直接返回原始数据，后续可以集成SPIRV-Tools进行优化
        return spirvBytecode;
    }

    void VulkanShaderCompiler::AddIncludePath(const std::string &path)
    {
        m_includePaths.push_back(path);
    }

    void VulkanShaderCompiler::ClearIncludePaths()
    {
        m_includePaths.clear();
    }

    void VulkanShaderCompiler::SetGlobalMacro(const std::string &name, const std::string &value)
    {
        m_globalMacros[name] = value;
    }

    void VulkanShaderCompiler::ClearGlobalMacros()
    {
        m_globalMacros.clear();
    }

    std::string VulkanShaderCompiler::GetVersion() const
    {
        // 返回Shaderc版本信息
        return "Shaderc-Vulkan 1.0";
    }

    std::vector<std::string> VulkanShaderCompiler::GetSupportedLanguages() const
    {
        return {"GLSL", "HLSL"};
    }

    std::string VulkanShaderCompiler::DisassembleSPIRV(const std::vector<uint32_t> &spirvBytecode)
    {
        ORG_LOG_DEBUG("DisassembleSPIRV called (not implemented yet)");
        // 目前返回简单信息，后续可以使用SPIRV-Tools进行反汇编
        return "SPIR-V disassembly not implemented yet. SPIR-V size: " + std::to_string(spirvBytecode.size() * 4) + " bytes";
    }

    ShaderCompileResult VulkanShaderCompiler::CompileGLSLFileToSPIRV(
        const std::string &glslFilePath,
        ShaderStage stage,
        const std::string &entryPoint)
    {
        ShaderCompileResult result;

        try
        {
            ORG_LOG_DEBUG("Reading GLSL file: {}", glslFilePath);

            // 使用FileSystem读取GLSL文件
            std::string source = Orange::Core::FileSystem::ReadTextFile(glslFilePath);

            if (source.empty())
            {
                result.success = false;
                result.errorMessage = "Failed to read GLSL file or file is empty: " + glslFilePath;
                ORG_LOG_ERROR("Failed to read GLSL file or file is empty: {}", glslFilePath);
                return result;
            }

            ORG_LOG_DEBUG("Successfully read {} bytes from {}", source.length(), glslFilePath);

            // 获取文件名用于错误报告
            std::string filename = glslFilePath;
            size_t lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
            {
                filename = filename.substr(lastSlash + 1);
            }

            // 编译GLSL源码
            return CompileGLSLToSPIRV(source, stage, filename, entryPoint);
        }
        catch (const std::exception &e)
        {
            result.success = false;
            result.errorMessage = "Exception while reading GLSL file: " + std::string(e.what());
            ORG_LOG_ERROR("Exception while reading GLSL file: {}", e.what());
            return result;
        }
    }

    ShaderCompileResult VulkanShaderCompiler::CompileGLSLToSPIRV(
        const std::string &glslSource,
        ShaderStage stage,
        const std::string &filename,
        const std::string &entryPoint)
    {
        ORG_LOG_DEBUG("Compiling GLSL to SPIR-V: {}", filename);

        ShaderCompileResult result;

        // 创建Shaderc编译器
        shaderc::Compiler compiler;
        shaderc::CompileOptions options;

        // 设置编译选项
        shaderc_shader_kind kind = GetShadercStage(stage);
        ORG_LOG_DEBUG("Shader stage: {}, Shaderc kind: {}", static_cast<int>(stage), static_cast<int>(kind));
        options.SetOptimizationLevel(shaderc_optimization_level_performance);
        ORG_LOG_DEBUG("Starting compilation...");

        // 编译GLSL到SPIR-V
        shaderc::SpvCompilationResult compilationResult = compiler.CompileGlslToSpv(
            glslSource, kind, filename.c_str(), entryPoint.c_str(), options);

        ORG_LOG_DEBUG("Compilation finished. Status: {}", static_cast<int>(compilationResult.GetCompilationStatus()));

        if (compilationResult.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            result.success = false;
            result.errorMessage = compilationResult.GetErrorMessage();
            ORG_LOG_ERROR("GLSL compilation failed: {}", result.errorMessage);
            return result;
        }

        // 编译成功
        result.success = true;
        std::vector<uint32_t> spirv(compilationResult.cbegin(), compilationResult.cend());
        result.spirvBytecode = std::move(spirv);

        ORG_LOG_INFO("Successfully compiled {} to SPIR-V ({} bytes)", filename, result.spirvBytecode.size() * 4);

        return result;
    }

    // 私有方法实现
    shaderc_shader_kind VulkanShaderCompiler::GetShadercStage(ShaderStage stage)
    {
        switch (stage)
        {
        case ShaderStage::Vertex:
            return shaderc_vertex_shader;
        case ShaderStage::Fragment:
            return shaderc_fragment_shader;
        case ShaderStage::Geometry:
            return shaderc_geometry_shader;
        case ShaderStage::Compute:
            return shaderc_compute_shader;
        case ShaderStage::TessellationControl:
            return shaderc_tess_control_shader;
        case ShaderStage::TessellationEvaluation:
            return shaderc_tess_evaluation_shader;
        default:
            return shaderc_vertex_shader;
        }
    }

    void VulkanShaderCompiler::SetupCompileOptions(shaderc::CompileOptions &options, const ShaderCompileOptions &compileOptions)
    {
        // 设置优化级别
        if (compileOptions.optimization)
        {
            options.SetOptimizationLevel(shaderc_optimization_level_performance);
        }
        else
        {
            options.SetOptimizationLevel(shaderc_optimization_level_zero);
        }

        // 设置调试信息
        if (compileOptions.debugInfo)
        {
            options.SetGenerateDebugInfo();
        }

        // 添加include路径
        for (const auto &path : m_includePaths)
        {
            // TODO: 设置include路径
        }

        for (const auto &path : compileOptions.includePaths)
        {
            // TODO: 设置include路径
        }

        // 添加宏定义
        for (const auto &[name, value] : m_globalMacros)
        {
            options.AddMacroDefinition(name, value);
        }

        for (const auto &[name, value] : compileOptions.macros)
        {
            options.AddMacroDefinition(name, value);
        }

        // 设置目标环境
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
        options.SetTargetSpirv(shaderc_spirv_version_1_0);
    }

} // namespace Orange::Graphics::Vulkan