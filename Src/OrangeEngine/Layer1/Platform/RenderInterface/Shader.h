#ifndef SHADER_H
#define SHADER_H

#include "Core/Base/Base.h"
#include "Core/Base/Ref.h"
#include "Core/Memory/Buffer.h"
#include "ShaderUniform.h"

#include <filesystem>
#include <string>
#include <glm/glm.hpp>

namespace Orange
{
    /**
     * @namespace ShaderUtils
     * @brief 着色器工具函数命名空间
     */
    namespace ShaderUtils
    {
        /**
         * @enum SourceLang
         * @brief 着色器源语言枚举
         *
         * 定义了支持的着色器源代码语言类型。
         */
        enum class SourceLang
        {
            NONE, ///< 无语言类型
            GLSL, ///< OpenGL着色器语言
            HLSL, ///< DirectX高级着色器语言
        };
    }

    /**
     * @enum ShaderUniformType
     * @brief 着色器统一变量类型枚举
     *
     * 定义了着色器中支持的统一变量数据类型。
     */
    enum class ShaderUniformType
    {
        None = 0, ///< 无类型
        Bool,     ///< 布尔类型
        Int,      ///< 整数类型
        UInt,     ///< 无符号整数类型
        Float,    ///< 浮点数类型
        Vec2,     ///< 2D向量类型
        Vec3,     ///< 3D向量类型
        Vec4,     ///< 4D向量类型
        Mat3,     ///< 3x3矩阵类型
        Mat4,     ///< 4x4矩阵类型
        IVec2,    ///< 2D整数向量类型
        IVec3,    ///< 3D整数向量类型
        IVec4     ///< 4D整数向量类型
    };

    /**
     * @class ShaderUniform
     * @brief 着色器统一变量类
     *
     * 表示着色器中的一个统一变量，包含名称、类型、大小和偏移信息。
     */
    class ShaderUniform
    {
    public:
        /**
         * @brief 默认构造函数
         */
        ShaderUniform() = default;

        /**
         * @brief 构造函数
         * @param name 统一变量名称
         * @param type 统一变量类型
         * @param size 统一变量大小
         * @param offset 统一变量在缓冲区中的偏移
         */
        ShaderUniform(std::string name, ShaderUniformType type, uint32_t size, uint32_t offset);

        /**
         * @brief 获取统一变量名称
         * @return 统一变量名称的常量引用
         */
        const std::string &GetName() const { return m_Name; }

        /**
         * @brief 获取统一变量类型
         * @return 统一变量类型
         */
        ShaderUniformType GetType() const { return m_Type; }

        /**
         * @brief 获取统一变量大小
         * @return 统一变量大小（字节）
         */
        uint32_t GetSize() const { return m_Size; }

        /**
         * @brief 获取统一变量偏移
         * @return 统一变量在缓冲区中的偏移（字节）
         */
        uint32_t GetOffset() const { return m_Offset; }

        /**
         * @brief 将统一变量类型转换为字符串
         * @param type 统一变量类型
         * @return 类型对应的字符串视图
         */
        static constexpr std::string_view UniformTypeToString(ShaderUniformType type);

#ifdef TODO
        /**
         * @brief 序列化统一变量
         * @param serializer 序列化器
         * @param instance 要序列化的实例
         */
        static void Serialize(StreamWriter *serializer, const ShaderUniform &instance)
        {
            serializer->WriteString(instance.m_Name);
            serializer->WriteRaw(instance.m_Type);
            serializer->WriteRaw(instance.m_Size);
            serializer->WriteRaw(instance.m_Offset);
        }

        /**
         * @brief 反序列化统一变量
         * @param deserializer 反序列化器
         * @param instance 要反序列化的实例
         */
        static void Deserialize(StreamReader *deserializer, ShaderUniform &instance)
        {
            deserializer->ReadString(instance.m_Name);
            deserializer->ReadRaw(instance.m_Type);
            deserializer->ReadRaw(instance.m_Size);
            deserializer->ReadRaw(instance.m_Offset);
        }
#endif

    private:
        std::string m_Name;                                 ///< 统一变量名称
        ShaderUniformType m_Type = ShaderUniformType::None; ///< 统一变量类型
        uint32_t m_Size = 0;                                ///< 统一变量大小
        uint32_t m_Offset = 0;                              ///< 统一变量偏移
    };

    /**
     * @struct ShaderUniformBuffer
     * @brief 着色器统一缓冲区结构体
     *
     * 描述着色器中的统一缓冲区对象，包含缓冲区信息和其中的统一变量。
     */
    struct ShaderUniformBuffer
    {
        std::string Name;                    ///< 缓冲区名称
        uint32_t Index;                      ///< 缓冲区索引
        uint32_t BindingPoint;               ///< 绑定点
        uint32_t Size;                       ///< 缓冲区大小
        uint32_t RendererID;                 ///< 渲染器ID
        std::vector<ShaderUniform> Uniforms; ///< 缓冲区中的统一变量列表
    };

    /**
     * @struct ShaderStorageBuffer
     * @brief 着色器存储缓冲区结构体
     *
     * 描述着色器中的存储缓冲区对象，用于大容量数据存储。
     */
    struct ShaderStorageBuffer
    {
        std::string Name;      ///< 缓冲区名称
        uint32_t Index;        ///< 缓冲区索引
        uint32_t BindingPoint; ///< 绑定点
        uint32_t Size;         ///< 缓冲区大小
        uint32_t RendererID;   ///< 渲染器ID
                               // std::vector<ShaderUniform> Uniforms;
    };

    /**
     * @struct ShaderBuffer
     * @brief 着色器缓冲区结构体
     *
     * 通用的着色器缓冲区描述，包含缓冲区信息和统一变量映射。
     */
    struct ShaderBuffer
    {
        std::string Name;                                        ///< 缓冲区名称
        uint32_t Size = 0;                                       ///< 缓冲区大小
        std::unordered_map<std::string, ShaderUniform> Uniforms; ///< 统一变量映射

#ifdef TODO
        /**
         * @brief 序列化着色器缓冲区
         * @param serializer 序列化器
         * @param instance 要序列化的实例
         */
        static void Serialize(StreamWriter *serializer, const ShaderBuffer &instance)
        {
            serializer->WriteString(instance.Name);
            serializer->WriteRaw(instance.Size);
            serializer->WriteMap(instance.Uniforms);
        }

        /**
         * @brief 反序列化着色器缓冲区
         * @param deserializer 反序列化器
         * @param instance 要反序列化的实例
         */
        static void Deserialize(StreamReader *deserializer, ShaderBuffer &instance)
        {
            deserializer->ReadString(instance.Name);
            deserializer->ReadRaw(instance.Size);
            deserializer->ReadMap(instance.Uniforms);
        }
#endif
    };

    /**
     * @class Shader
     * @brief 着色器抽象基类
     *
     * 这个类提供了着色器的统一接口，封装了着色器的编译、加载、
     * 管理和使用。着色器是GPU程序的抽象，定义了顶点、片段、
     * 几何和计算等着色阶段的行为。
     *
     * 主要功能包括：
     * - 着色器编译：支持GLSL和HLSL源代码编译
     * - 热重载：支持运行时重新编译和加载
     * - 统一变量管理：自动解析和管理着色器参数
     * - 资源绑定：管理纹理、缓冲区等资源绑定
     * - 宏定义：支持编译时宏定义和条件编译
     * - 缓存系统：支持编译结果缓存和快速加载
     *
     * 着色器类型支持：
     * - 顶点着色器：处理顶点变换和属性
     * - 片段着色器：处理像素着色和光照
     * - 几何着色器：处理图元生成和变换
     * - 计算着色器：处理通用计算任务
     * - 曲面细分着色器：处理曲面细分
     *
     * 跨平台支持：
     * - 自动检测目标图形API
     * - 源代码语言转换（GLSL ↔ HLSL）
     * - 平台特定优化和兼容性处理
     *
     * 具体的实现由各个图形API的派生类提供（如VulkanShader）。
     */
    class Shader : public RefCounted
    {
    public:
        /**
         * @typedef ShaderReloadedCallback
         * @brief 着色器重载回调函数类型
         */
        using ShaderReloadedCallback = std::function<void()>;

        /**
         * @brief 重新加载着色器
         * @param forceCompile 是否强制重新编译，默认为false
         *
         * 重新加载着色器源代码并重新编译。这对于着色器开发和
         * 调试非常有用，允许在不重启应用程序的情况下更新着色器。
         */
        virtual void Reload(bool forceCompile = false) = 0;

        /**
         * @brief 重新加载着色器（渲染线程）
         * @param forceCompile 是否强制重新编译
         *
         * 在渲染线程中重新加载着色器。RT_前缀表示这是渲染线程
         * (Render Thread)专用的方法。
         */
        virtual void RT_Reload(bool forceCompile) = 0;

        /**
         * @brief 获取着色器哈希值
         * @return 着色器的哈希值
         *
         * 返回着色器的唯一哈希值，用于缓存、比较和识别。
         */
        virtual size_t GetHash() const = 0;

        /**
         * @brief 获取着色器名称
         * @return 着色器名称的常量引用
         */
        virtual const std::string &GetName() const = 0;

        /**
         * @brief 设置宏定义
         * @param name 宏名称
         * @param value 宏值
         *
         * 设置着色器编译时的宏定义，用于条件编译和代码变体生成。
         */
        virtual void SetMacro(const std::string &name, const std::string &value) = 0;

        /**
         * @brief 从文件创建着色器
         * @param filepath 着色器文件路径
         * @param forceCompile 是否强制重新编译，默认为false
         * @param disableOptimization 是否禁用优化，默认为false
         * @return 创建的着色器实例智能指针
         *
         * 从指定的文件路径加载和编译着色器。
         */
        static Ref<Shader> Create(const std::string &filepath, bool forceCompile = false, bool disableOptimization = false);

        /**
         * @brief 从着色器包加载着色器
         * @param filepath 着色器包文件路径
         * @param forceCompile 是否强制重新编译，默认为false
         * @param disableOptimization 是否禁用优化，默认为false
         * @return 创建的着色器实例智能指针
         *
         * 从预编译的着色器包文件加载着色器，提供更快的加载速度。
         */
        static Ref<Shader> LoadFromShaderPack(const std::string &filepath, bool forceCompile = false, bool disableOptimization = false);

        /**
         * @brief 从字符串创建着色器
         * @param source 着色器源代码字符串
         * @return 创建的着色器实例智能指针
         *
         * 从内存中的源代码字符串直接编译着色器。
         */
        static Ref<Shader> CreateFromString(const std::string &source);

        /**
         * @brief 获取着色器缓冲区映射
         * @return 着色器缓冲区映射的常量引用
         *
         * 返回着色器中定义的所有缓冲区信息。
         */
        virtual const std::unordered_map<std::string, ShaderBuffer> &GetShaderBuffers() const = 0;

        /**
         * @brief 获取着色器资源映射
         * @return 着色器资源映射的常量引用
         *
         * 返回着色器中定义的所有资源（纹理、图像等）信息。
         */
        virtual const std::unordered_map<std::string, ShaderResourceDeclaration> &GetResources() const = 0;

        /**
         * @brief 添加着色器重载回调
         * @param callback 回调函数
         *
         * 注册一个回调函数，当着色器重新加载时会被调用。
         */
        virtual void AddShaderReloadedCallback(const ShaderReloadedCallback &callback) = 0;

        /**
         * @brief 获取着色器目录路径
         * @return 着色器目录路径
         *
         * 返回着色器文件的默认搜索目录。
         */
        static constexpr const char *GetShaderDirectoryPath()
        {
            return "Resources/Shaders/";
        }
    };

    class ShaderPack;

    /**
     * @class ShaderLibrary
     * @brief 着色器库管理类
     *
     * 这个类负责管理和组织多个着色器实例，提供着色器的集中管理、
     * 加载和访问功能。着色器库简化了着色器资源的管理，支持
     * 批量加载和按需访问。
     *
     * 主要功能包括：
     * - 着色器集合管理：统一管理多个着色器实例
     * - 批量加载：支持从目录或着色器包批量加载
     * - 名称映射：通过名称快速访问着色器
     * - 生命周期管理：自动管理着色器的创建和销毁
     * - 着色器包支持：支持预编译的着色器包格式
     *
     * 使用场景：
     * - 应用程序启动时批量加载所有着色器
     * - 运行时按需加载特定着色器
     * - 着色器资源的集中管理和访问
     * - 着色器包的统一处理
     *
     * 注意：此类最终应该由资产管理器处理。
     */
    class ShaderLibrary : public RefCounted
    {
    public:
        /**
         * @brief 构造函数
         *
         * 创建一个空的着色器库实例。
         */
        ShaderLibrary();

        /**
         * @brief 析构函数
         *
         * 清理着色器库中的所有着色器资源。
         */
        ~ShaderLibrary();

        /**
         * @brief 添加着色器到库中
         * @param shader 要添加的着色器实例
         *
         * 将一个已创建的着色器实例添加到库中，使用着色器的名称作为键。
         */
        void Add(const Ref<Shader> &shader);

        /**
         * @brief 加载着色器文件
         * @param path 着色器文件路径
         * @param forceCompile 是否强制重新编译，默认为false
         * @param disableOptimization 是否禁用优化，默认为false
         *
         * 从指定路径加载着色器文件，并添加到库中。
         */
        void Load(std::string_view path, bool forceCompile = false, bool disableOptimization = false);

        /**
         * @brief 加载着色器文件（指定名称）
         * @param name 着色器名称
         * @param path 着色器文件路径
         *
         * 从指定路径加载着色器文件，并使用指定的名称添加到库中。
         */
        void Load(std::string_view name, const std::string &path);

        /**
         * @brief 加载着色器包
         * @param path 着色器包文件路径
         *
         * 从着色器包文件批量加载多个着色器。
         */
        void LoadShaderPack(const std::filesystem::path &path);

        /**
         * @brief 获取着色器
         * @param name 着色器名称
         * @return 着色器实例的常量引用
         *
         * 根据名称从库中获取着色器实例。
         */
        const Ref<Shader> &Get(const std::string &name) const;

        /**
         * @brief 获取着色器库大小
         * @return 库中着色器的数量
         */
        size_t GetSize() const { return m_Shaders.size(); }

        /**
         * @brief 获取着色器映射（可修改）
         * @return 着色器映射的引用
         */
        std::unordered_map<std::string, Ref<Shader>> &GetShaders() { return m_Shaders; }

        /**
         * @brief 获取着色器映射（只读）
         * @return 着色器映射的常量引用
         */
        const std::unordered_map<std::string, Ref<Shader>> &GetShaders() const { return m_Shaders; }

    private:
        std::unordered_map<std::string, Ref<Shader>> m_Shaders; ///< 着色器映射
        Ref<ShaderPack> m_ShaderPack;                           ///< 着色器包实例
    };
}

#endif