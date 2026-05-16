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
    Quat,    // 旋转四元数。控件路径**不**是直接的 4 分量编辑：Quat 在 gimbal
             // lock 附近 q→Euler 不连续，DragFloat4 编辑也不直观。SchemaInspector
             // 的 Quat case 走 "DragFloat3 Euler 缓存 + apply 时回算 quat" 的混
             // 合路径，缓存放在 EditorSelection.transformEulerCache（与
             // selectedEntity 联动）。当前仅 TransformComponent.rotation 使用
    String,
    Enum,    // C++ enum / enum class —— Builder::FieldEnum<auto FieldPtr> 注册；
             // marshal 走 int（caller-side 视类型为 int，Builder 内部 lambda
             // 负责 underlying_type 转换）。控件由 PropertyAttributes::enumNames
             // 决定 Combo 项列表
    EntityRef, // Orange::Engine::Entity 字段。本 commit 仅支持只读显示
             // （"#<id>" / "(none)"）—— Hierarchy.parent / firstChild / prev /
             // nextSibling 走此路径。后续 commit 可扩展 drag-drop 写入 entity
             // 句柄；扩展时 SchemaInspector::DrawProperty 的 EntityRef case
             // 内加 source/target accept 逻辑即可，不影响已注册 schema
    AssetRef, // v0.5 c1 起：磁盘资源引用字段。get/set 类型擦除媒介是
             // std::string（资源相对路径，如 "assets/meshes/cube.mesh"）。
             // schema 注册侧通过 capture 全局 AssetRegistry / 名表静态指针
             // 完成 path ↔ component 字段（AssetHandle<T> / MaterialInstance*
             // 等）的双向映射。AssetKind 属性区分资源类型用于 Asset 浏览器
             // 过滤 + DnD payload 校验。
             //
             // 控件路径分两期：c1 仅显示当前 path（无控件 / 与 readOnly 同款
             // Text + TextDisabled）；c4 加 DnD 接收 + clear 按钮 + 浏览器
             // popup 选择
};

// AssetKind —— PropertyType::AssetRef 字段的资源类型标签。
// 用途：Asset 浏览器按 kind 过滤可拖入字段的卡片；DnD payload 携带 kind
// 让 receiving field 校验类型匹配（拒绝把 .scene 文件拖到 mesh 字段上）；
// Material 子模式入口判定（path 后缀 .material 时按 Material kind 处理）。
enum class AssetKind : std::uint8_t
{
    Unknown = 0,
    Mesh,     // .mesh / .obj / .gltf 等 → AssetHandle<MeshAsset>
    Material, // .material → MaterialInstance*（通过 namedMaterialInstances 反查）
    Texture,  // .png / .jpg / .ktx 等 → AssetHandle<TextureAsset>
    Scene,    // .scene.json → 不持 handle，仅作为路径引用（场景拖入打开）
};

}  // namespace Orange::Editor::Schema

#endif  // ORANGE_EDITOR_SCHEMA_PROPERTY_TYPE_H
