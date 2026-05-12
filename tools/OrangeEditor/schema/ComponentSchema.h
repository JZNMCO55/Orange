#ifndef ORANGE_EDITOR_SCHEMA_COMPONENT_SCHEMA_H
#define ORANGE_EDITOR_SCHEMA_COMPONENT_SCHEMA_H

// ComponentSchema —— 单个 component 类型的完整 schema 描述。
//
// 包括：
//   * 类型名 / 显示名
//   * properties 列表（PropertyDescriptor 集合）
//   * 类型擦除的 component-on-entity 访问（has / get / add / remove）
//
// 这层抽象让 SchemaInspector 完全不依赖具体 component 类型，可以遍历
// "registry 中已注册的所有 schema → 对每个 schema 调 has(world, entity)
// → 若存在则 Draw"——这正是 Lumix StudioApp 的 PropertyGrid 路径，与
// Godot EditorInspector 同型。
//
// 类型擦除策略与 PropertyDescriptor 同：函数指针 + void* 返回。caller
// 必须保证 cast 回正确类型（typically schema 注册代码内通过 capture-less
// lambda 把 `World::GetComponent<T>` 包成 `void*` 返回值）。

#include "PropertyDescriptor.h"

#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/World.h>

#include <vector>

namespace Orange::Editor::Schema
{

struct ComponentSchema
{
    // 类型 id（编译期字面量）。同时充当 SetFieldValueCommand fieldKey 的
    // 前缀 —— SchemaInspector 拼接 "typeName.propName" 作为 coalesce key。
    const char* typeName    = nullptr;

    // 用户可见的 component header label（CollapsingHeader 标题）。
    const char* displayName = nullptr;

    // 字段列表。注册顺序 = Inspector 内显示顺序。
    std::vector<PropertyDescriptor> properties;

    // ---- 类型擦除的 component-on-entity 访问 ----------------------------
    //
    // get 返回 void* —— SchemaInspector 把它喂给每个 PropertyDescriptor 的
    // get/set。caller 不能 cast 这个指针成任何具体类型；schema 内部 lambda
    // 内已写死了 reinterpret_cast，外界只当 opaque handle 用。
    //
    // add / remove 可选：nullptr 表示该 component 在当前 schema 体系下
    // 不能通过 Add Component 菜单 / 右键 Remove Component 操作（典型例外：
    // Hierarchy 由 DnD reparent 管理 / Animator 需要具体 IAnimator 子类）。
    using HasFn    = bool  (*)(const Orange::Engine::World& world, Orange::Engine::Entity entity);
    using GetFn    = void* (*)(Orange::Engine::World&       world, Orange::Engine::Entity entity);
    using AddFn    = void  (*)(Orange::Engine::World&       world, Orange::Engine::Entity entity);
    using RemoveFn = void  (*)(Orange::Engine::World&       world, Orange::Engine::Entity entity);

    HasFn    has    = nullptr;
    GetFn    get    = nullptr;
    AddFn    add    = nullptr;
    RemoveFn remove = nullptr;
};

}  // namespace Orange::Editor::Schema

#endif  // ORANGE_EDITOR_SCHEMA_COMPONENT_SCHEMA_H
