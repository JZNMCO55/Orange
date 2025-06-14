#ifndef SHADER_UNIFORM_H
#define SHADER_UNIFORM_H

#include "Core/Base/Base.h"
#include "Core/Base/Logger.h"
#include <string>
#include <vector>

namespace Orange
{
    /**
     * @enum ShaderDomain
     * @brief 着色器域枚举
     *
     * 定义了着色器的执行域或阶段。虽然目前主要用于兼容性，
     * 但为未来的扩展预留了接口。
     *
     * @note 当前实现中Pixel域未被使用，主要用于向后兼容
     */
    enum class ShaderDomain
    {
        None = 0,   ///< 无特定域
        Vertex = 0, ///< 顶点着色器域
        Pixel = 1   ///< 像素着色器域（未使用）
    };

    /**
     * @class ShaderResourceDeclaration
     * @brief 着色器资源声明类
     *
     * 这个类表示着色器中的一个资源声明，包含资源的名称、绑定信息
     * 和数量等属性。着色器资源声明用于描述着色器接口中的各种
     * 资源绑定点，如纹理、缓冲区、采样器等。
     *
     * 主要功能包括：
     * - 资源标识：通过名称唯一标识着色器资源
     * - 绑定管理：管理资源的集合和寄存器绑定
     * - 数量控制：支持数组类型的资源声明
     * - 序列化支持：支持资源声明的持久化存储
     * - 反射查询：提供着色器接口的反射信息
     *
     * 资源绑定模型：
     * - Set：描述符集合索引，用于组织相关资源
     * - Register：寄存器或绑定点，资源在集合中的位置
     * - Count：资源数量，支持数组类型的资源
     *
     * 使用场景：
     * - 着色器反射：分析着色器的资源需求
     * - 资源绑定：自动化资源绑定过程
     * - 接口验证：验证着色器接口的兼容性
     * - 工具支持：为着色器编辑器提供信息
     *
     * 跨平台支持：
     * 不同图形API有不同的资源绑定模型：
     * - Vulkan：使用描述符集合和绑定点
     * - DirectX：使用寄存器和空间
     * - OpenGL：使用统一位置和纹理单元
     *
     * 此类提供了统一的抽象，隐藏了底层API的差异。
     */
    class ShaderResourceDeclaration
    {
    public:
        /**
         * @brief 默认构造函数
         *
         * 创建一个空的资源声明，所有属性都使用默认值。
         */
        ShaderResourceDeclaration() = default;

        /**
         * @brief 构造函数
         * @param name 资源名称
         * @param set 描述符集合索引
         * @param resourceRegister 资源寄存器或绑定点
         * @param count 资源数量
         *
         * 创建一个完整的资源声明，包含所有必要的绑定信息。
         *
         * 参数说明：
         * - name：着色器中声明的资源名称
         * - set：资源所属的描述符集合
         * - resourceRegister：资源在集合中的绑定位置
         * - count：资源数量，1表示单个资源，>1表示数组
         */
        ShaderResourceDeclaration(const std::string &name, uint32_t set, uint32_t resourceRegister, uint32_t count)
            : m_Name(name), m_Set(set), m_Register(resourceRegister), m_Count(count)
        {
        }

        /**
         * @brief 获取资源名称
         * @return 资源名称的常量引用
         *
         * 返回着色器中声明的资源名称，用于标识和查找资源。
         */
        virtual const std::string &GetName() const { return m_Name; }

        /**
         * @brief 获取描述符集合索引
         * @return 描述符集合索引
         *
         * 返回资源所属的描述符集合索引。在Vulkan中对应descriptor set，
         * 在DirectX中对应register space。
         */
        virtual uint32_t GetSet() const { return m_Set; }

        /**
         * @brief 获取资源寄存器
         * @return 资源寄存器或绑定点
         *
         * 返回资源在描述符集合中的绑定位置。在Vulkan中对应binding，
         * 在DirectX中对应register number。
         */
        virtual uint32_t GetRegister() const { return m_Register; }

        /**
         * @brief 获取资源数量
         * @return 资源数量
         *
         * 返回资源的数量。对于单个资源返回1，对于数组资源返回数组大小。
         */
        virtual uint32_t GetCount() const { return m_Count; }
#ifdef TODO
        /**
         * @brief 序列化资源声明
         * @param serializer 序列化器
         * @param instance 要序列化的实例
         *
         * 将资源声明序列化到流中，用于着色器包的创建和缓存。
         *
         * 序列化内容：
         * - 资源名称字符串
         * - 描述符集合索引
         * - 资源寄存器编号
         * - 资源数量
         */
        static void Serialize(StreamWriter *serializer, const ShaderResourceDeclaration &instance)
        {
            serializer->WriteString(instance.m_Name);
            serializer->WriteRaw(instance.m_Set);
            serializer->WriteRaw(instance.m_Register);
            serializer->WriteRaw(instance.m_Count);
        }

        /**
         * @brief 反序列化资源声明
         * @param deserializer 反序列化器
         * @param instance 要反序列化的实例
         *
         * 从流中反序列化资源声明，用于着色器包的加载和恢复。
         *
         * 反序列化过程：
         * 1. 读取资源名称字符串
         * 2. 读取描述符集合索引
         * 3. 读取资源寄存器编号
         * 4. 读取资源数量
         *
         * @note 反序列化的顺序必须与序列化的顺序完全一致
         */
        static void Deserialize(StreamReader *deserializer, ShaderResourceDeclaration &instance)
        {
            deserializer->ReadString(instance.m_Name);
            deserializer->ReadRaw(instance.m_Set);
            deserializer->ReadRaw(instance.m_Register);
            deserializer->ReadRaw(instance.m_Count);
        }
#endif
    private:
        std::string m_Name;      ///< 资源名称
        uint32_t m_Set = 0;      ///< 描述符集合索引
        uint32_t m_Register = 0; ///< 资源寄存器或绑定点
        uint32_t m_Count = 0;    ///< 资源数量
    };

    /**
     * @typedef ShaderResourceList
     * @brief 着色器资源列表类型定义
     *
     * 定义了着色器资源声明指针的向量类型，用于管理一组相关的
     * 着色器资源声明。这个类型通常用于存储着色器的所有资源
     * 声明，便于批量处理和管理。
     *
     * 使用场景：
     * - 着色器反射：存储着色器的所有资源声明
     * - 资源验证：批量验证资源绑定的正确性
     * - 接口分析：分析着色器的资源需求
     * - 自动绑定：自动化资源绑定过程
     *
     * 内存管理：
     * 列表中存储的是资源声明的指针，需要注意指针的生命周期管理。
     * 通常这些指针指向着色器对象内部的资源声明，与着色器同生命周期。
     *
     * @note 使用指针而非对象是为了避免不必要的拷贝开销
     * @note 调用者需要确保指针的有效性
     */
    typedef std::vector<ShaderResourceDeclaration *> ShaderResourceList;
}

#endif