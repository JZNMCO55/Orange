#ifndef VERTEX_BUFFER_H
#define VERTEX_BUFFER_H

#include "Core/Base/Ref.h"
#include "Core/Base/Assert.h"
#include "RendererTypes.h"
#include "Core/Base/Logger.h"

namespace Orange
{
    /**
     * @enum ShaderDataType
     * @brief 着色器数据类型枚举
     *
     * 定义了着色器中使用的各种数据类型，用于描述顶点属性的数据格式。
     * 这些类型对应于GLSL和HLSL中的基本数据类型。
     */
    enum class ShaderDataType
    {
        None = 0, ///< 无类型
        Float,    ///< 单精度浮点数
        Float2,   ///< 2D浮点向量
        Float3,   ///< 3D浮点向量
        Float4,   ///< 4D浮点向量
        Mat3,     ///< 3x3浮点矩阵
        Mat4,     ///< 4x4浮点矩阵
        Int,      ///< 32位整数
        Int2,     ///< 2D整数向量
        Int3,     ///< 3D整数向量
        Int4,     ///< 4D整数向量
        Bool      ///< 布尔值
    };

    /**
     * @brief 获取着色器数据类型的字节大小
     * @param type 着色器数据类型
     * @return 该类型的字节大小
     *
     * 返回指定着色器数据类型在内存中占用的字节数。
     * 这个函数用于计算顶点缓冲区的布局和偏移量。
     */
    static uint32_t ShaderDataTypeSize(ShaderDataType type)
    {
        switch (type)
        {
        case ShaderDataType::Float:
            return 4; ///< float: 4字节
        case ShaderDataType::Float2:
            return 4 * 2; ///< vec2: 8字节
        case ShaderDataType::Float3:
            return 4 * 3; ///< vec3: 12字节
        case ShaderDataType::Float4:
            return 4 * 4; ///< vec4: 16字节
        case ShaderDataType::Mat3:
            return 4 * 3 * 3; ///< mat3: 36字节
        case ShaderDataType::Mat4:
            return 4 * 4 * 4; ///< mat4: 64字节
        case ShaderDataType::Int:
            return 4; ///< int: 4字节
        case ShaderDataType::Int2:
            return 4 * 2; ///< ivec2: 8字节
        case ShaderDataType::Int3:
            return 4 * 3; ///< ivec3: 12字节
        case ShaderDataType::Int4:
            return 4 * 4; ///< ivec4: 16字节
        case ShaderDataType::Bool:
            return 1; ///< bool: 1字节
        }

        ORG_CORE_ASSERT(false, "Unknown ShaderDataType!");
        return 0;
    }

    /**
     * @struct VertexBufferElement
     * @brief 顶点缓冲区元素结构体
     *
     * 描述顶点缓冲区中单个属性的信息，包括名称、类型、大小、
     * 偏移量和是否需要归一化等属性。
     */
    struct VertexBufferElement
    {
        std::string Name;    ///< 属性名称（如"Position"、"Color"等）
        ShaderDataType Type; ///< 数据类型
        uint32_t Size;       ///< 数据大小（字节）
        uint32_t Offset;     ///< 在顶点中的偏移量（字节）
        bool Normalized;     ///< 是否需要归一化到[0,1]或[-1,1]范围

        /**
         * @brief 默认构造函数
         */
        VertexBufferElement() = default;

        /**
         * @brief 构造函数
         * @param type 数据类型
         * @param name 属性名称
         * @param normalized 是否归一化，默认为false
         *
         * 创建一个顶点缓冲区元素，自动计算数据大小。
         */
        VertexBufferElement(ShaderDataType type, const std::string &name, bool normalized = false)
            : Name(name), Type(type), Size(ShaderDataTypeSize(type)), Offset(0), Normalized(normalized)
        {
        }

        /**
         * @brief 获取组件数量
         * @return 该数据类型包含的组件数量
         *
         * 返回数据类型包含的基本组件数量。例如，Float3返回3，Mat4返回16。
         * 这个信息用于设置顶点属性指针。
         */
        uint32_t GetComponentCount() const
        {
            switch (Type)
            {
            case ShaderDataType::Float:
                return 1; ///< 1个float组件
            case ShaderDataType::Float2:
                return 2; ///< 2个float组件
            case ShaderDataType::Float3:
                return 3; ///< 3个float组件
            case ShaderDataType::Float4:
                return 4; ///< 4个float组件
            case ShaderDataType::Mat3:
                return 3 * 3; ///< 9个float组件
            case ShaderDataType::Mat4:
                return 4 * 4; ///< 16个float组件
            case ShaderDataType::Int:
                return 1; ///< 1个int组件
            case ShaderDataType::Int2:
                return 2; ///< 2个int组件
            case ShaderDataType::Int3:
                return 3; ///< 3个int组件
            case ShaderDataType::Int4:
                return 4; ///< 4个int组件
            case ShaderDataType::Bool:
                return 1; ///< 1个bool组件
            }

            ORG_CORE_ASSERT(false, "Unknown ShaderDataType!");
            return 0;
        }
    };

    /**
     * @class VertexBufferLayout
     * @brief 顶点缓冲区布局类
     *
     * 管理顶点缓冲区的布局信息，包括所有顶点属性的定义和排列。
     * 布局定义了每个顶点的数据结构，包括位置、颜色、纹理坐标等属性
     * 在内存中的组织方式。
     */
    class VertexBufferLayout
    {
    public:
        /**
         * @brief 默认构造函数
         *
         * 创建一个空的顶点缓冲区布局。
         */
        VertexBufferLayout() {}

        /**
         * @brief 初始化列表构造函数
         * @param elements 顶点缓冲区元素的初始化列表
         *
         * 使用初始化列表创建顶点缓冲区布局，自动计算偏移量和步长。
         * 例如：VertexBufferLayout({
         *     {ShaderDataType::Float3, "Position"},
         *     {ShaderDataType::Float4, "Color"}
         * });
         */
        VertexBufferLayout(const std::initializer_list<VertexBufferElement> &elements)
            : m_Elements(elements)
        {
            CalculateOffsetsAndStride();
        }

        /**
         * @brief 获取顶点步长
         * @return 单个顶点的总字节大小
         *
         * 返回单个顶点数据的总字节大小，即从一个顶点到下一个顶点的字节距离。
         */
        uint32_t GetStride() const { return m_Stride; }

        /**
         * @brief 获取所有元素
         * @return 顶点缓冲区元素的常量引用
         *
         * 返回布局中所有顶点属性元素的列表。
         */
        const std::vector<VertexBufferElement> &GetElements() const { return m_Elements; }

        /**
         * @brief 获取元素数量
         * @return 顶点属性的数量
         *
         * 返回布局中定义的顶点属性数量。
         */
        uint32_t GetElementCount() const { return (uint32_t)m_Elements.size(); }

        /**
         * @brief 获取开始迭代器（可修改）
         * @return 指向第一个元素的迭代器
         */
        [[nodiscard]] std::vector<VertexBufferElement>::iterator begin() { return m_Elements.begin(); }

        /**
         * @brief 获取结束迭代器（可修改）
         * @return 指向最后一个元素之后的迭代器
         */
        [[nodiscard]] std::vector<VertexBufferElement>::iterator end() { return m_Elements.end(); }

        /**
         * @brief 获取开始迭代器（只读）
         * @return 指向第一个元素的常量迭代器
         */
        [[nodiscard]] std::vector<VertexBufferElement>::const_iterator begin() const { return m_Elements.begin(); }

        /**
         * @brief 获取结束迭代器（只读）
         * @return 指向最后一个元素之后的常量迭代器
         */
        [[nodiscard]] std::vector<VertexBufferElement>::const_iterator end() const { return m_Elements.end(); }

    private:
        /**
         * @brief 计算偏移量和步长
         *
         * 根据元素列表计算每个属性在顶点中的偏移量和整个顶点的步长。
         * 这个方法在构造函数中自动调用。
         */
        void CalculateOffsetsAndStride()
        {
            uint32_t offset = 0;
            m_Stride = 0;
            for (auto &element : m_Elements)
            {
                element.Offset = offset;
                offset += element.Size;
                m_Stride += element.Size;
            }
        }

    private:
        std::vector<VertexBufferElement> m_Elements; ///< 顶点属性元素列表
        uint32_t m_Stride = 0;                       ///< 顶点步长（字节）
    };

    /**
     * @enum VertexBufferUsage
     * @brief 顶点缓冲区使用模式枚举
     *
     * 定义了顶点缓冲区的使用模式，影响GPU内存的分配策略和性能特性。
     */
    enum class VertexBufferUsage
    {
        None = 0,   ///< 未指定使用模式
        Static = 1, ///< 静态使用：数据很少或从不更改，优化读取性能
        Dynamic = 2 ///< 动态使用：数据经常更改，优化写入性能
    };

    /**
     * @class VertexBuffer
     * @brief 顶点缓冲区抽象基类
     *
     * 这个类提供了顶点缓冲区的统一接口，封装了顶点数据的存储和管理。
     * 顶点缓冲区存储在GPU内存中，包含了渲染所需的顶点数据，如位置、
     * 颜色、纹理坐标、法线等信息。
     *
     * 主要功能包括：
     * - 创建和管理GPU顶点缓冲区
     * - 上传和更新顶点数据
     * - 绑定缓冲区用于渲染
     * - 支持静态和动态使用模式
     *
     * 具体的实现由各个图形API的派生类提供（如VulkanVertexBuffer）。
     */
    class VertexBuffer : public RefCounted
    {
    public:
        /**
         * @brief 虚析构函数
         *
         * 确保派生类能够正确清理GPU资源。
         */
        virtual ~VertexBuffer() {}

        /**
         * @brief 设置缓冲区数据
         * @param buffer 要上传的数据指针
         * @param size 数据大小（字节）
         * @param offset 缓冲区内的偏移量（字节），默认为0
         *
         * 将数据上传到顶点缓冲区的指定位置。这是主线程版本的方法。
         */
        virtual void SetData(void *buffer, uint64_t size, uint64_t offset = 0) = 0;

        /**
         * @brief 设置缓冲区数据（渲染线程版本）
         * @param buffer 要上传的数据指针
         * @param size 数据大小（字节）
         * @param offset 缓冲区内的偏移量（字节），默认为0
         *
         * 将数据上传到顶点缓冲区的指定位置。这是渲染线程版本的方法，
         * 用于多线程渲染环境中的线程安全操作。
         */
        virtual void RT_SetData(void *buffer, uint64_t size, uint64_t offset = 0) = 0;

        /**
         * @brief 绑定顶点缓冲区
         *
         * 将此顶点缓冲区绑定到当前的渲染上下文，使其成为后续
         * 渲染操作的顶点数据源。
         */
        virtual void Bind() const = 0;

        /**
         * @brief 获取缓冲区大小
         * @return 缓冲区大小（字节）
         *
         * 返回顶点缓冲区的总大小。
         */
        virtual unsigned int GetSize() const = 0;

        /**
         * @brief 获取渲染器ID
         * @return 渲染器相关的资源ID
         *
         * 返回底层图形API中对应的缓冲区资源ID，用于调试和内部操作。
         */
        virtual RendererID GetRendererID() const = 0;

        /**
         * @brief 创建顶点缓冲区（带初始数据）
         * @param data 初始数据指针
         * @param size 数据大小（字节）
         * @param usage 使用模式，默认为静态
         * @return 创建的顶点缓冲区实例智能指针
         *
         * 创建一个新的顶点缓冲区并用提供的数据初始化。
         * 具体的实现类型取决于当前使用的渲染API。
         */
        static Ref<VertexBuffer> Create(void *data, uint64_t size, VertexBufferUsage usage = VertexBufferUsage::Static);

        /**
         * @brief 创建顶点缓冲区（仅分配空间）
         * @param size 缓冲区大小（字节）
         * @param usage 使用模式，默认为动态
         * @return 创建的顶点缓冲区实例智能指针
         *
         * 创建一个指定大小的空顶点缓冲区，不包含初始数据。
         * 数据可以稍后通过SetData方法上传。
         */
        static Ref<VertexBuffer> Create(uint64_t size, VertexBufferUsage usage = VertexBufferUsage::Dynamic);
    };
}

#endif