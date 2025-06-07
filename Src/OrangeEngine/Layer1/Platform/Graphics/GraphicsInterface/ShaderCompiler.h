#ifndef ORANGE_GRAPHICS_SHADERCOMPILER_H
#define ORANGE_GRAPHICS_SHADERCOMPILER_H

#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>
#include "RenderDevice.h" // 为了使用ShaderStage枚举

namespace Orange::Graphics
{

    /**
     * @brief 着色器编译选项
     */
    struct ShaderCompileOptions
    {
        ShaderStage stage = ShaderStage::Vertex;
        std::string entryPoint = "main";
        std::vector<std::string> includePaths;
        std::unordered_map<std::string, std::string> macros;
        bool optimization = true;
        bool debugInfo = false;
        std::string debugName;
    };

    /**
     * @brief 着色器编译结果
     */
    struct ShaderCompileResult
    {
        bool success = false;
        std::vector<uint32_t> spirvBytecode;
        std::string errorMessage;
        std::string warningMessage;
    };

    /**
     * @brief 着色器编译器接口
     * 提供着色器编译功能，将GLSL/HLSL编译为SPIR-V字节码
     */
    class ShaderCompiler
    {
    public:
        virtual ~ShaderCompiler() = default;

        /**
         * @brief 从文件编译着色器
         * @param filePath 着色器文件路径
         * @param options 编译选项
         * @return 编译结果
         */
        virtual ShaderCompileResult CompileFromFile(
            const std::string &filePath,
            const ShaderCompileOptions &options) = 0;

        /**
         * @brief 从源代码编译着色器
         * @param source 着色器源代码
         * @param options 编译选项
         * @return 编译结果
         */
        virtual ShaderCompileResult CompileFromSource(
            const std::string &source,
            const ShaderCompileOptions &options) = 0;

        /**
         * @brief 预处理着色器源代码
         * @param source 源代码
         * @param options 编译选项
         * @return 预处理后的源代码
         */
        virtual std::string PreprocessSource(
            const std::string &source,
            const ShaderCompileOptions &options) = 0;

        /**
         * @brief 验证SPIR-V字节码
         * @param spirvBytecode SPIR-V字节码
         * @return 验证成功返回true
         */
        virtual bool ValidateSPIRV(const std::vector<uint32_t> &spirvBytecode) = 0;

        /**
         * @brief 优化SPIR-V字节码
         * @param spirvBytecode 输入的SPIR-V字节码
         * @return 优化后的SPIR-V字节码
         */
        virtual std::vector<uint32_t> OptimizeSPIRV(const std::vector<uint32_t> &spirvBytecode) = 0;

        /**
         * @brief 添加include路径
         * @param path include路径
         */
        virtual void AddIncludePath(const std::string &path) = 0;

        /**
         * @brief 清除所有include路径
         */
        virtual void ClearIncludePaths() = 0;

        /**
         * @brief 设置全局宏定义
         * @param name 宏名称
         * @param value 宏值
         */
        virtual void SetGlobalMacro(const std::string &name, const std::string &value) = 0;

        /**
         * @brief 清除所有全局宏定义
         */
        virtual void ClearGlobalMacros() = 0;

        /**
         * @brief 获取编译器版本信息
         * @return 版本信息字符串
         */
        virtual std::string GetVersion() const = 0;

        /**
         * @brief 获取支持的着色器语言列表
         * @return 支持的语言列表
         */
        virtual std::vector<std::string> GetSupportedLanguages() const = 0;

        /**
         * @brief 反编译SPIR-V为可读的汇编代码
         * @param spirvBytecode SPIR-V字节码
         * @return 汇编代码字符串
         */
        virtual std::string DisassembleSPIRV(const std::vector<uint32_t> &spirvBytecode) = 0;
    };

} // namespace Orange::Graphics

#endif // ORANGE_GRAPHICS_ShaderCompiler_H