#ifndef ORANGE_EDITOR_SCHEMA_PROPERTY_DESCRIPTOR_H
#define ORANGE_EDITOR_SCHEMA_PROPERTY_DESCRIPTOR_H

// PropertyDescriptor —— 描述 component 的单个字段。
//
// 字段元数据（name / label / type / attributes）+ 类型擦除的 get / set
// 函数指针。SchemaInspector 用 switch (type) 分派到合适的 ImGui 控件，
// 通过 get/set 读写实际字段值。
//
// 类型擦除策略：
//   * get/set 用 `void*` + `void*` —— caller 必须保证 outValue / inValue
//     的实际类型与 PropertyType 匹配。SchemaInspector 走 switch(type) 在
//     对应 case 内显式定义 local typed buffer，保证类型一致
//   * 这种"伪 variant"方案比真正的 std::variant 节省 sizeof(slot) + cache
//     locality + 无 RTTI 依赖，与 CLAUDE.md "Serialization and reflection"
//     节禁令兼容（禁的是 entt::meta / RTTR / cereal，不是 void* + 模板）
//
// 设计参考：vendor/LumixEngine/src/engine/reflection.h 的 `Property<T>` +
// `IPropertyVisitor`——Lumix 用 virtual visitor，OrangeEditor 用枚举 +
// switch（视觉上更直接，新增 PropertyType 时编译器会在 SchemaInspector
// 的 switch 警告 -Wswitch-enum 提示遗漏 case）。

#include "PropertyType.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>

namespace Orange::Editor::Schema
{

// PropertyTypeOf<T> —— 编译期把 C++ 字段类型映射到 PropertyType 枚举。
// 用法见 ComponentSchemaBuilder::Field<T>(...) 内的自动推导。
template <typename T> struct PropertyTypeOf;
template <> struct PropertyTypeOf<float>        { static constexpr PropertyType value = PropertyType::Float;  };
template <> struct PropertyTypeOf<int>          { static constexpr PropertyType value = PropertyType::Int;    };
template <> struct PropertyTypeOf<unsigned int> { static constexpr PropertyType value = PropertyType::UInt;   };
template <> struct PropertyTypeOf<bool>         { static constexpr PropertyType value = PropertyType::Bool;   };
template <> struct PropertyTypeOf<glm::vec2>    { static constexpr PropertyType value = PropertyType::Vec2;   };
template <> struct PropertyTypeOf<glm::vec3>    { static constexpr PropertyType value = PropertyType::Vec3;   };
template <> struct PropertyTypeOf<glm::vec4>    { static constexpr PropertyType value = PropertyType::Vec4;   };
template <> struct PropertyTypeOf<glm::quat>    { static constexpr PropertyType value = PropertyType::Quat;   };
template <> struct PropertyTypeOf<std::string>  { static constexpr PropertyType value = PropertyType::String; };

// PropertyAttributes —— 修饰单个 property 的可选属性。
// 与 Lumix `IAttribute` 类层级（MinAttribute / ClampAttribute / ColorAttribute
// / ...）相比，OrangeEditor 走"全部 attribute 塞同一个结构体里 + 标志位
// 选用"的扁平方案。理由：内置 component 只有有限几种修饰场景，不需要可扩
// 展的 attribute 列表；插入新 attribute 时直接加结构体字段更简单。
struct PropertyAttributes
{
    // 数值字段（Float / Int / Vec2 / Vec3 / Vec4）的 DragFloat 参数。
    // hasRange = false → 不限制最值（DragFloat 传 min=max=0.0f）。
    bool  hasRange  = false;
    float minValue  = 0.0f;
    float maxValue  = 0.0f;
    float dragSpeed = 0.1f;

    // Vec3 / Vec4 走 ColorEdit3/4（HDR 时配合 ColorEditFlags_HDR）而非
    // DragFloat3/4。其它 PropertyType 设置 isColor 会被 SchemaInspector
    // 忽略（switch 内只在 Vec3/Vec4 case 检查）。
    bool  isColor   = false;

    // 同 ImGui::SetTooltip 内容；nullptr 表示无 tooltip。字符串字面量
    // 由 schema 注册代码持有（静态生命周期），不复制。
    const char* tooltip = nullptr;
};

// PropertyDescriptor —— 单个字段的完整描述。
//
// 多数情况下通过 ComponentSchemaBuilder::Field<T>(...) 间接构造；直接
// 实例化仅在自定义 plugin / advanced 用例里出现。
struct PropertyDescriptor
{
    // 字段内部 id。同时充当 SetFieldValueCommand 的 fieldKey（component.field
    // 格式，由 builder 自动拼接），影响 coalesce 匹配粒度。
    //
    // 字符串字面量静态生命周期，schema 不拷贝；调用方应保证传入的字符串
    // 与 schema 同生命周期（典型：编译期字面量）。
    const char* name  = nullptr;

    // 用户可见的字段标签（ImGui 控件 label）。同样要求静态生命周期。
    const char* label = nullptr;

    PropertyType       type    = PropertyType::Float;
    PropertyAttributes attribs = {};

    // 类型擦除的字段访问。
    //   * outValue / inValue 必须指向与 PropertyType 对应的 C++ 类型实例。
    //   * SchemaInspector::DrawProperty 内 switch (type) 的每个 case 都
    //     声明对应 typed buffer 调用，保证类型一致。
    //
    // 函数指针（非 std::function）—— 无堆分配 + 直接 inline 友好；通过
    // capture-less lambda 可零成本初始化。
    using GetFn = void (*)(const void* component, void*       outValue);
    using SetFn = void (*)(void*       component, const void* inValue);
    GetFn get = nullptr;
    SetFn set = nullptr;
};

}  // namespace Orange::Editor::Schema

#endif  // ORANGE_EDITOR_SCHEMA_PROPERTY_DESCRIPTOR_H
