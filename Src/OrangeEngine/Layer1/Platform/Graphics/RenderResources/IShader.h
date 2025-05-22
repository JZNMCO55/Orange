/**
 * @file IShader.h
 * @brief 着色器接口定义
 */

#ifndef ORANGE_ISHADER_H
#define ORANGE_ISHADER_H

#include "../RenderCommon/RenderCommon.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace Orange
{
    namespace Graphics
    {

        // 前向声明
        class IRenderDevice;

        /**
         * @brief 着色器反射信息
         */
        struct ShaderReflection
        {
            struct ResourceBinding
            {
                uint32_t binding;        ///< 绑定点
                uint32_t set;            ///< 描述符集索引
                DescriptorType type;     ///< 描述符类型
                uint32_t count;          ///< 数组大小（如果是数组）
                ShaderStageFlags stages; ///< 着色器阶段标志
                std::string name;        ///< 资源名称
            };

            struct PushConstantBlock
            {
                uint32_t offset;         ///< 偏移量
                uint32_t size;           ///< 大小
                ShaderStageFlags stages; ///< 着色器阶段标志
                std::string name;        ///< 块名称
            };

            struct VertexInput
            {
                uint32_t location;   ///< 位置
                uint32_t binding;    ///< 绑定点
                VertexFormat format; ///< 顶点格式
                uint32_t offset;     ///< 偏移量
                std::string name;    ///< 输入名称
            };

            std::vector<ResourceBinding> resourceBindings;                ///< 资源绑定列表
            std::vector<PushConstantBlock> pushConstantBlocks;            ///< 推送常量块列表
            std::vector<VertexInput> vertexInputs;                        ///< 顶点输入列表（仅顶点着色器）
            std::unordered_map<std::string, std::string> specializations; ///< 特化常量
        };

        /**
         * @brief 着色器模块接口
         *
         * 着色器模块封装了编译后的着色器代码。
         */
        class IShader
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IShader() = default;

            /**
             * @brief 获取着色器类型
             * @return 着色器类型
             */
            virtual ShaderType GetType() const = 0;

            /**
             * @brief 获取着色器源代码语言
             * @return 着色器语言
             */
            virtual ShaderLanguage GetLanguage() const = 0;

            /**
             * @brief 获取着色器入口点
             * @return 着色器入口点
             */
            virtual const std::string &GetEntryPoint() const = 0;

            /**
             * @brief 获取着色器源代码
             * @return 着色器源代码
             */
            virtual const std::string &GetSource() const = 0;

            /**
             * @brief 获取着色器编译后的字节码
             * @return 着色器字节码
             */
            virtual const std::vector<uint8_t> &GetBytecode() const = 0;

            /**
             * @brief 是否有效
             * @return 是否有效
             */
            virtual bool IsValid() const = 0;

            /**
             * @brief 获取编译错误信息
             * @return 编译错误信息
             */
            virtual const std::string &GetErrorMessage() const = 0;

            /**
             * @brief 获取着色器反射信息
             * @return 着色器反射信息
             */
            virtual const ShaderReflection &GetReflection() const = 0;

            /**
             * @brief 重新编译着色器
             * @param source 新的源代码（如果为空则使用现有源代码）
             * @return 是否成功编译
             */
            virtual bool Recompile(const std::string &source = "") = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 获取原生着色器模块句柄
             * @return 原生着色器模块句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeShaderModule() const = 0;
        };

        /**
         * @brief 创建着色器描述
         */
        struct ShaderCreateDesc
        {
            ShaderType type = ShaderType::Vertex;                 ///< 着色器类型
            ShaderLanguage language = ShaderLanguage::GLSL;       ///< 着色器语言
            std::string source;                                   ///< 着色器源代码
            std::vector<uint8_t> bytecode;                        ///< 着色器字节码（优先于源代码）
            std::string entryPoint = "main";                      ///< 入口点函数名
            std::unordered_map<std::string, std::string> defines; ///< 预处理宏定义
            std::string includePath;                              ///< 包含路径
            bool optimize = true;                                 ///< 是否优化
            bool debug = false;                                   ///< 是否包含调试信息
            bool reflect = true;                                  ///< 是否生成反射信息
            const char *debugName = nullptr;                      ///< 调试名称

            /**
             * @brief 从源代码创建着色器描述
             * @param shaderType 着色器类型
             * @param shaderSource 着色器源代码
             * @param shaderLanguage 着色器语言
             * @param shaderEntryPoint 入口点函数名
             * @return 着色器创建描述
             */
            static ShaderCreateDesc FromSource(
                ShaderType shaderType,
                const std::string &shaderSource,
                ShaderLanguage shaderLanguage = ShaderLanguage::GLSL,
                const std::string &shaderEntryPoint = "main")
            {
                ShaderCreateDesc desc;
                desc.type = shaderType;
                desc.language = shaderLanguage;
                desc.source = shaderSource;
                desc.entryPoint = shaderEntryPoint;
                return desc;
            }

            /**
             * @brief 从文件创建着色器描述
             * @param shaderType 着色器类型
             * @param filename 文件名
             * @param shaderLanguage 着色器语言
             * @param shaderEntryPoint 入口点函数名
             * @return 着色器创建描述
             */
            static ShaderCreateDesc FromFile(
                ShaderType shaderType,
                const std::string &filename,
                ShaderLanguage shaderLanguage = ShaderLanguage::GLSL,
                const std::string &shaderEntryPoint = "main")
            {
                // 注意：这里只是描述，实际的文件加载会在实现中进行
                ShaderCreateDesc desc;
                desc.type = shaderType;
                desc.language = shaderLanguage;
                desc.source = filename; // 实现中会将这个解释为文件名
                desc.entryPoint = shaderEntryPoint;
                return desc;
            }

            /**
             * @brief 从预编译的字节码创建着色器描述
             * @param shaderType 着色器类型
             * @param shaderBytecode 着色器字节码
             * @param shaderEntryPoint 入口点函数名
             * @return 着色器创建描述
             */
            static ShaderCreateDesc FromBytecode(
                ShaderType shaderType,
                const std::vector<uint8_t> &shaderBytecode,
                const std::string &shaderEntryPoint = "main")
            {
                ShaderCreateDesc desc;
                desc.type = shaderType;
                desc.bytecode = shaderBytecode;
                desc.entryPoint = shaderEntryPoint;
                return desc;
            }

            /**
             * @brief 添加预处理宏定义
             * @param name 宏名称
             * @param value 宏值
             * @return 自身引用，便于链式调用
             */
            ShaderCreateDesc &AddDefine(const std::string &name, const std::string &value = "")
            {
                defines[name] = value;
                return *this;
            }

            /**
             * @brief 设置包含路径
             * @param path 包含路径
             * @return 自身引用，便于链式调用
             */
            ShaderCreateDesc &SetIncludePath(const std::string &path)
            {
                includePath = path;
                return *this;
            }

            /**
             * @brief 设置是否优化
             * @param enableOptimize 是否优化
             * @return 自身引用，便于链式调用
             */
            ShaderCreateDesc &SetOptimize(bool enableOptimize)
            {
                optimize = enableOptimize;
                return *this;
            }

            /**
             * @brief 设置是否包含调试信息
             * @param enableDebug 是否包含调试信息
             * @return 自身引用，便于链式调用
             */
            ShaderCreateDesc &SetDebug(bool enableDebug)
            {
                debug = enableDebug;
                return *this;
            }
        };

        /**
         * @brief 着色器程序接口
         *
         * 着色器程序包含多个着色器阶段，用于创建渲染管线。
         */
        class IShaderProgram
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IShaderProgram() = default;

            /**
             * @brief 添加着色器
             * @param shader 着色器
             * @return 是否成功添加
             */
            virtual bool AddShader(IShader *shader) = 0;

            /**
             * @brief 获取特定类型的着色器
             * @param type 着色器类型
             * @return 着色器，如果不存在则返回nullptr
             */
            virtual IShader *GetShader(ShaderType type) const = 0;

            /**
             * @brief 获取所有着色器
             * @return 着色器映射表（类型到着色器）
             */
            virtual const std::unordered_map<ShaderType, IShader *> &GetShaders() const = 0;

            /**
             * @brief 检查程序是否有效
             * @param errorMessage 如果无效，输出错误信息
             * @return 是否有效
             */
            virtual bool IsValid(std::string *errorMessage = nullptr) const = 0;

            /**
             * @brief 获取程序反射信息
             * @return 程序反射信息
             */
            virtual const ShaderReflection &GetReflection() const = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;
        };

        /**
         * @brief 创建着色器程序描述
         */
        struct ShaderProgramCreateDesc
        {
            std::unordered_map<ShaderType, IShader *> shaders; ///< 着色器映射表
            const char *debugName = nullptr;                   ///< 调试名称

            /**
             * @brief 添加着色器
             * @param shader 着色器
             * @return 自身引用，便于链式调用
             */
            ShaderProgramCreateDesc &AddShader(IShader *shader)
            {
                if (shader)
                {
                    shaders[shader->GetType()] = shader;
                }
                return *this;
            }
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_ISHADER_H