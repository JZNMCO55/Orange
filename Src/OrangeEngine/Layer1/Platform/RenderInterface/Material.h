#ifndef MATERIAL_H
#define MATERIAL_H

#include "Core/Base/Ref.h"
#include "Core/Base/Base.h"
#include "Shader.h"
#include "Texture.h"

#include <unordered_set>

namespace Orange
{
    /**
     * @enum MaterialFlag
     * @brief 材质标志枚举
     *
     * 定义了材质的各种渲染状态标志，这些标志控制材质在渲染过程中的行为。
     * 使用位标志的方式，可以组合多个标志来定义复杂的渲染状态。
     */
    enum class MaterialFlag
    {
        None = BIT(0),                ///< 无特殊标志
        DepthTest = BIT(1),           ///< 启用深度测试
        Blend = BIT(2),               ///< 启用混合模式（用于透明渲染）
        TwoSided = BIT(3),            ///< 双面渲染（禁用背面剔除）
        DisableShadowCasting = BIT(4) ///< 禁用阴影投射
    };

    /**
     * @class Material
     * @brief 材质抽象基类
     *
     * 这个类提供了材质系统的统一接口，封装了着色器参数管理、纹理绑定
     * 和渲染状态控制。材质是连接3D模型和着色器的桥梁，定义了物体
     * 在渲染时的外观和行为。
     *
     * 主要功能包括：
     * - 着色器参数设置和获取（标量、向量、矩阵）
     * - 纹理资源绑定和管理
     * - 渲染状态标志控制
     * - 材质的创建、复制和重载机制
     * - 支持多种数据类型的统一变量
     *
     * 材质系统支持的数据类型：
     * - 基本类型：float, int, uint32_t, bool
     * - 向量类型：vec2, vec3, vec4, ivec2, ivec3, ivec4
     * - 矩阵类型：mat3, mat4
     * - 纹理类型：Texture2D, TextureCube, Image2D, ImageView
     *
     * 具体的实现由各个图形API的派生类提供（如VulkanMaterial）。
     */
    class Material : public RefCounted
    {
    public:
        /**
         * @brief 创建材质实例
         * @param shader 关联的着色器程序
         * @param name 材质名称，默认为空字符串
         * @return 创建的材质实例智能指针
         *
         * 根据指定的着色器创建一个新的材质实例。材质将使用着色器中
         * 定义的统一变量和资源绑定点来管理参数。
         */
        static Ref<Material> Create(const Ref<Shader> &shader, const std::string &name = "");

        /**
         * @brief 复制材质实例
         * @param other 要复制的源材质
         * @param name 新材质的名称，默认为空字符串
         * @return 复制的材质实例智能指针
         *
         * 创建一个现有材质的副本，包括所有的参数值和状态设置。
         * 这对于创建材质变体非常有用。
         */
        static Ref<Material> Copy(const Ref<Material> &other, const std::string &name = "");

        /**
         * @brief 虚析构函数
         *
         * 确保派生类能够正确清理资源。
         */
        virtual ~Material() {}

        /**
         * @brief 使材质失效并重新创建
         *
         * 当材质的底层资源需要重新创建时调用此方法，
         * 通常在着色器重新编译或渲染上下文改变时使用。
         */
        virtual void Invalidate() = 0;

        /**
         * @brief 着色器重新加载时的回调
         *
         * 当关联的着色器被重新加载时调用此方法，材质需要
         * 更新其内部状态以适应新的着色器定义。
         */
        virtual void OnShaderReloaded() = 0;

        /**
         * @brief 设置浮点数参数
         * @param name 参数名称
         * @param value 浮点数值
         *
         * 设置着色器中的float类型统一变量。
         */
        virtual void Set(const std::string &name, float value) = 0;

        /**
         * @brief 设置整数参数
         * @param name 参数名称
         * @param value 整数值
         *
         * 设置着色器中的int类型统一变量。
         */
        virtual void Set(const std::string &name, int value) = 0;

        /**
         * @brief 设置无符号整数参数
         * @param name 参数名称
         * @param value 无符号整数值
         *
         * 设置着色器中的uint类型统一变量。
         */
        virtual void Set(const std::string &name, uint32_t value) = 0;

        /**
         * @brief 设置布尔参数
         * @param name 参数名称
         * @param value 布尔值
         *
         * 设置着色器中的bool类型统一变量。
         */
        virtual void Set(const std::string &name, bool value) = 0;

        /**
         * @brief 设置2D向量参数
         * @param name 参数名称
         * @param value 2D向量值
         *
         * 设置着色器中的vec2类型统一变量。
         */
        virtual void Set(const std::string &name, const glm::vec2 &value) = 0;

        /**
         * @brief 设置3D向量参数
         * @param name 参数名称
         * @param value 3D向量值
         *
         * 设置着色器中的vec3类型统一变量。
         */
        virtual void Set(const std::string &name, const glm::vec3 &value) = 0;

        /**
         * @brief 设置4D向量参数
         * @param name 参数名称
         * @param value 4D向量值
         *
         * 设置着色器中的vec4类型统一变量。
         */
        virtual void Set(const std::string &name, const glm::vec4 &value) = 0;

        /**
         * @brief 设置2D整数向量参数
         * @param name 参数名称
         * @param value 2D整数向量值
         *
         * 设置着色器中的ivec2类型统一变量。
         */
        virtual void Set(const std::string &name, const glm::ivec2 &value) = 0;

        /**
         * @brief 设置3D整数向量参数
         * @param name 参数名称
         * @param value 3D整数向量值
         *
         * 设置着色器中的ivec3类型统一变量。
         */
        virtual void Set(const std::string &name, const glm::ivec3 &value) = 0;

        /**
         * @brief 设置4D整数向量参数
         * @param name 参数名称
         * @param value 4D整数向量值
         *
         * 设置着色器中的ivec4类型统一变量。
         */
        virtual void Set(const std::string &name, const glm::ivec4 &value) = 0;

        /**
         * @brief 设置3x3矩阵参数
         * @param name 参数名称
         * @param value 3x3矩阵值
         *
         * 设置着色器中的mat3类型统一变量。
         */
        virtual void Set(const std::string &name, const glm::mat3 &value) = 0;

        /**
         * @brief 设置4x4矩阵参数
         * @param name 参数名称
         * @param value 4x4矩阵值
         *
         * 设置着色器中的mat4类型统一变量。
         */
        virtual void Set(const std::string &name, const glm::mat4 &value) = 0;

        /**
         * @brief 设置2D纹理参数
         * @param name 参数名称
         * @param texture 2D纹理对象
         *
         * 将2D纹理绑定到着色器中的采样器。
         */
        virtual void Set(const std::string &name, const Ref<Texture2D> &texture) = 0;

        /**
         * @brief 设置2D纹理数组参数
         * @param name 参数名称
         * @param texture 2D纹理对象
         * @param arrayIndex 纹理数组索引
         *
         * 将2D纹理绑定到着色器中的纹理数组的指定索引位置。
         */
        virtual void Set(const std::string &name, const Ref<Texture2D> &texture, uint32_t arrayIndex) = 0;

        /**
         * @brief 设置立方体纹理参数
         * @param name 参数名称
         * @param texture 立方体纹理对象
         *
         * 将立方体纹理绑定到着色器中的立方体采样器。
         */
        virtual void Set(const std::string &name, const Ref<TextureCube> &texture) = 0;

        /**
         * @brief 设置2D图像参数
         * @param name 参数名称
         * @param image 2D图像对象
         *
         * 将2D图像绑定到着色器中的图像单元，用于计算着色器的读写操作。
         */
        virtual void Set(const std::string &name, const Ref<Image2D> &image) = 0;

        /**
         * @brief 设置图像视图参数
         * @param name 参数名称
         * @param image 图像视图对象
         *
         * 将图像视图绑定到着色器中的图像单元。
         */
        virtual void Set(const std::string &name, const Ref<ImageView> &image) = 0;

        /**
         * @brief 获取浮点数参数引用
         * @param name 参数名称
         * @return 浮点数参数的引用
         *
         * 返回指定名称的float参数的可修改引用。
         */
        virtual float &GetFloat(const std::string &name) = 0;

        /**
         * @brief 获取整数参数引用
         * @param name 参数名称
         * @return 整数参数的引用
         *
         * 返回指定名称的int32_t参数的可修改引用。
         */
        virtual int32_t &GetInt(const std::string &name) = 0;

        /**
         * @brief 获取无符号整数参数引用
         * @param name 参数名称
         * @return 无符号整数参数的引用
         *
         * 返回指定名称的uint32_t参数的可修改引用。
         */
        virtual uint32_t &GetUInt(const std::string &name) = 0;

        /**
         * @brief 获取布尔参数引用
         * @param name 参数名称
         * @return 布尔参数的引用
         *
         * 返回指定名称的bool参数的可修改引用。
         */
        virtual bool &GetBool(const std::string &name) = 0;

        /**
         * @brief 获取2D向量参数引用
         * @param name 参数名称
         * @return 2D向量参数的引用
         *
         * 返回指定名称的vec2参数的可修改引用。
         */
        virtual glm::vec2 &GetVector2(const std::string &name) = 0;

        /**
         * @brief 获取3D向量参数引用
         * @param name 参数名称
         * @return 3D向量参数的引用
         *
         * 返回指定名称的vec3参数的可修改引用。
         */
        virtual glm::vec3 &GetVector3(const std::string &name) = 0;

        /**
         * @brief 获取4D向量参数引用
         * @param name 参数名称
         * @return 4D向量参数的引用
         *
         * 返回指定名称的vec4参数的可修改引用。
         */
        virtual glm::vec4 &GetVector4(const std::string &name) = 0;

        /**
         * @brief 获取3x3矩阵参数引用
         * @param name 参数名称
         * @return 3x3矩阵参数的引用
         *
         * 返回指定名称的mat3参数的可修改引用。
         */
        virtual glm::mat3 &GetMatrix3(const std::string &name) = 0;

        /**
         * @brief 获取4x4矩阵参数引用
         * @param name 参数名称
         * @return 4x4矩阵参数的引用
         *
         * 返回指定名称的mat4参数的可修改引用。
         */
        virtual glm::mat4 &GetMatrix4(const std::string &name) = 0;

        /**
         * @brief 获取2D纹理
         * @param name 参数名称
         * @return 2D纹理对象智能指针
         *
         * 返回指定名称绑定的2D纹理对象。
         */
        virtual Ref<Texture2D> GetTexture2D(const std::string &name) = 0;

        /**
         * @brief 获取立方体纹理
         * @param name 参数名称
         * @return 立方体纹理对象智能指针
         *
         * 返回指定名称绑定的立方体纹理对象。
         */
        virtual Ref<TextureCube> GetTextureCube(const std::string &name) = 0;

        /**
         * @brief 尝试获取2D纹理
         * @param name 参数名称
         * @return 2D纹理对象智能指针，如果不存在则返回nullptr
         *
         * 安全地尝试获取指定名称的2D纹理，不会在纹理不存在时抛出异常。
         */
        virtual Ref<Texture2D> TryGetTexture2D(const std::string &name) = 0;

        /**
         * @brief 尝试获取立方体纹理
         * @param name 参数名称
         * @return 立方体纹理对象智能指针，如果不存在则返回nullptr
         *
         * 安全地尝试获取指定名称的立方体纹理，不会在纹理不存在时抛出异常。
         */
        virtual Ref<TextureCube> TryGetTextureCube(const std::string &name) = 0;

        /**
         * @brief 获取材质标志
         * @return 当前设置的所有标志的位掩码
         *
         * 返回材质当前设置的所有渲染状态标志。
         */
        virtual uint32_t GetFlags() const = 0;

        /**
         * @brief 设置材质标志
         * @param flags 要设置的标志位掩码
         *
         * 设置材质的渲染状态标志，可以组合多个MaterialFlag值。
         */
        virtual void SetFlags(uint32_t flags) = 0;

        /**
         * @brief 检查特定标志是否设置
         * @param flag 要检查的材质标志
         * @return 如果标志已设置则返回true，否则返回false
         *
         * 检查指定的材质标志是否在当前材质中被设置。
         */
        virtual bool GetFlag(MaterialFlag flag) const = 0;

        /**
         * @brief 设置或清除特定标志
         * @param flag 要设置的材质标志
         * @param value 是否设置该标志，默认为true
         *
         * 设置或清除指定的材质标志。
         */
        virtual void SetFlag(MaterialFlag flag, bool value = true) = 0;

        /**
         * @brief 获取关联的着色器
         * @return 着色器对象智能指针
         *
         * 返回材质当前关联的着色器程序。
         */
        virtual Ref<Shader> GetShader() = 0;

        /**
         * @brief 获取材质名称
         * @return 材质名称的常量引用
         *
         * 返回材质的名称，用于调试和识别。
         */
        virtual const std::string &GetName() const = 0;
    };
}

#endif