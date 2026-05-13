#ifndef ORANGE_EDITOR_SCHEMA_PROPERTY_TYPE_H
#define ORANGE_EDITOR_SCHEMA_PROPERTY_TYPE_H

// PropertyType —— schema 系统支持的字段类型枚举。
//
// 此枚举是 schema 驱动 Inspector 的"控件分派"开关：SchemaInspector::DrawProperty
// 内 switch(propType) → 选择合适的 ImGui 控件（DragFloat / Checkbox /
// ColorEdit3 / ...）。
//
// 关闭原则（避免反射库膨胀）：
//   * 只列出**内置 component 实际用到**的类型；不预先囊括所有 C++ 标量
//   * 新增类型必须同时更新：(1) PropertyType 枚举本身、(2) PropertyTypeOf<T>
//     模板特化（PropertyDescriptor.h）、(3) SchemaInspector::DrawProperty
//     的 switch case
//
// 列表的 rationale 见 editor-roadmap.md v0.2.5："参考 Lumix Builder API
// 和 Godot ClassDB 的宏 + 模板特化注册"——同栈两个工业级编辑器手写
// reflection 的最小集合。

#include <cstdint>

namespace Orange::Editor::Schema
{

enum class PropertyType : std::uint8_t
{
    Float = 0,
    Int,
    UInt,
    Bool,
    Vec2,
    Vec3,
    Vec4,
    Quat,    // 当前未在内置 component schema 中使用；保留给后续 Transform.rotation
             // 走 Euler 缓存的混合路径（v0.2.5 后续 commit 处理）
    String,
    Enum,    // C++ enum / enum class —— Builder::FieldEnum<auto FieldPtr> 注册；
             // marshal 走 int（caller-side 视类型为 int，Builder 内部 lambda
             // 负责 underlying_type 转换）。控件由 PropertyAttributes::enumNames
             // 决定 Combo 项列表
};

}  // namespace Orange::Editor::Schema

#endif  // ORANGE_EDITOR_SCHEMA_PROPERTY_TYPE_H
