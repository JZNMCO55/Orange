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

#include <orange/engine/physics/ColliderDesc.h>
#include <orange/engine/scene/Entity.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>

// 前向声明：AssetRef 字段的 accessor 拿引用参数即可，无需此处 include
// EditorAssetContext.h（避免把 AssetRegistry / MaterialSystem / AnimatorRegistry
// 等引擎头通过 PropertyDescriptor.h 传染到所有消费 schema 的 TU）
struct EditorAssetContext;

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
template <> struct PropertyTypeOf<Orange::Engine::Entity>
{
    static constexpr PropertyType value = PropertyType::EntityRef;
};
template <> struct PropertyTypeOf<Orange::Engine::Physics::PolygonDesc>
{
    static constexpr PropertyType value = PropertyType::PolygonVertices;
};
template <> struct PropertyTypeOf<Orange::Engine::Physics::EdgeChainDesc>
{
    static constexpr PropertyType value = PropertyType::EdgeChainVertices;
};

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

    // PropertyType::Enum 专用：Combo 控件的项名表。
    //   * enumNames 指向一段 const char*[enumCount] 数组，元素顺序 = 枚举
    //     underlying 值递增顺序（典型 enum class : 0,1,2,...）
    //   * 字符串字面量 + 数组本体由 schema 注册代码持有静态生命周期
    //   * 其它 PropertyType 上设这两个字段会被 SchemaInspector switch
    //     忽略（只在 Enum case 内检查）
    const char* const* enumNames = nullptr;
    int                enumCount = 0;

    // 视觉分组分隔符。非 nullptr → SchemaInspector 在本字段控件渲染**之前**
    // 调 ImGui::SeparatorText(groupSeparator)。用于把若干相关字段在 Inspector
    // 段内视觉聚成一组（典型场景：ParticleEmitter 的 Lifetime / Spawn Offset /
    // Initial Velocity / Color curve / Pool 等小节）。
    //
    // 字符串字面量静态生命周期，schema 不拷贝。注册顺序敏感——每个 group
    // 的第一个字段挂 GroupSeparator(text)，后续字段不挂；下一个 group 的
    // 第一个字段再挂新的 GroupSeparator。
    const char* groupSeparator = nullptr;

    // 只读显示标志。true → SchemaInspector 走"只显示当前值、不暴露编辑控件
    // / 不 Push SetFieldValueCommand"的路径。当前仅 PropertyType::String case
    // 实现 readOnly 分支（Text "<label>:" + SameLine + TextDisabled "<value>"）；
    // 其他 PropertyType 上设 readOnly 暂被忽略（c9 之后按需扩展，等真有 use
    // case 时再补，避免预先撒网）。
    //
    // 典型用例：
    //   * AnimatorComponent.animator->BackendName()（v0.1 期显示为指针；c9
    //     升级为 backend 名字符串）
    //   * 未来 RigidBodyComponent.handle（c4 期已记 deferred）/ Renderable
    //     mesh handle / MaterialInstance ptr（c7 期已记 deferred）—— 这些
    //     类型尚无 schema 化路径，等专门的 PropertyType（AssetHandle 等）
    //     落地后再用 readOnly 显示
    //
    // 与 visibleIf 正交：readOnly 决定"显示成什么样"；visibleIf 决定"是否
    // 显示"。两个都可同时用（少见但合法）。
    bool readOnly = false;

    // 条件可见谓词。nullptr = 总显示（默认）；非 nullptr → SchemaInspector
    // 在渲染本字段的"任何 UI（含 GroupSeparator）"之前调一次，返回 false
    // 时本字段**整体**跳过（不画 SeparatorText / 不画控件 / 不查 tooltip）。
    //
    // 典型用例：std::variant 持有的"互斥子结构"——ColliderComponent.shape
    // 在 Circle / Box / Polygon / EdgeChain 之间四选一，circle.radius 字段
    // 注册 visibleIf = `holds_alternative<CircleDesc>` 即可在不持 Circle 时
    // 自动隐藏。
    //
    // 函数指针（非 std::function）—— 与 PropertyDescriptor::GetFn / SetFn
    // 同档零开销；caller 通过 capture-less lambda 注入即可。component 指针
    // 与 PropertyDescriptor::get/set 的入参同义（caller 在 lambda 内
    // `static_cast<const C*>(component)` 拿 typed 组件指针）。
    using VisibleIfFn = bool (*)(const void* component);
    VisibleIfFn visibleIf = nullptr;

    // PropertyType::AssetRef 字段专用：资源类型标签。Asset 浏览器按 kind
    // 过滤可拖入；DnD payload 携带 kind 让接收字段校验类型匹配；Material
    // 子模式入口判定。默认 Unknown 不限定类型（接受任何 path）。
    AssetKind assetKind = AssetKind::Unknown;
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

    // AssetRef 字段专用 accessor —— 与 GetFn / SetFn 同款类型擦除策略，但
    // 多带一个 `const EditorAssetContext& ctx` 参数让 path↔handle 转换能
    // 取到 AssetRegistry / namedMaterialInstances 两段数据，而不依赖
    // RegisterBuiltinSchemas.cpp 的 file-scope 静态指针。
    //
    // 设计理由（详见 docs/decisions/ADR-004）：
    //   * 主路径走 LumixEngine reflection.h:130 同款 "setter 不带 editor
    //     context" —— 90%+ property 是纯数据 setter，加 ctx 参数是不必要
    //     的污染
    //   * AssetRef 是 OrangeEngine 因组件存 AssetHandle（而非 Lumix 那样
    //     存 Path）导致的真实架构错配的局部解：schema 层做 std::string
    //     ↔ AssetHandle 双向转换确实需要 AssetRegistry
    //   * 未来若有第二类 ctx-hungry 字段（ScriptRef / LocaleRef / ...）
    //     再考虑升级到 PropertyDescriptor 统一传 ctx 的方案 A，触发条件
    //     与升级路径记录在 ADR-004
    //
    // 调用约定：SchemaInspector AssetRef case 优先用 assetRefGet/assetRefSet
    // （非空时）；它们与上方 get/set 在 AssetRef 字段上是互斥关系（同一
    // PropertyDescriptor 上同时设两套会被 schema 注册期 assert）。其它
    // PropertyType 字段忽略本对槽位。
    using AssetRefGetFn = void (*)(const void* component,
                                   const EditorAssetContext& ctx,
                                   void* outValue);
    using AssetRefSetFn = void (*)(void* component,
                                   const EditorAssetContext& ctx,
                                   const void* inValue);
    AssetRefGetFn assetRefGet = nullptr;
    AssetRefSetFn assetRefSet = nullptr;
};

}  // namespace Orange::Editor::Schema

#endif  // ORANGE_EDITOR_SCHEMA_PROPERTY_DESCRIPTOR_H
