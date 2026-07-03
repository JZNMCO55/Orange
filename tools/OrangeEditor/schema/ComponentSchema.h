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

// EditorHost 前向声明 —— AddFn 签名引用它，但 ComponentSchema.h 本身不需要
// EditorHost 的完整定义（只暴露引用类型签名）。Builder 实现（Component
// SchemaRegistry.h）和 AddFn 调用站点（InspectorPanel.cpp / SchemaInspector.cpp）
// 才需要 #include "../EditorHost.h"。
struct EditorHost;

namespace Orange::Editor::Schema
{

    struct ComponentSchema
    {
        // 类型 id（编译期字面量）。同时充当 SetFieldValueCommand fieldKey 的
        // 前缀 —— SchemaInspector 拼接 "typeName.propName" 作为 coalesce key。
        const char* typeName = nullptr;

        // 用户可见的 component header label（CollapsingHeader 标题）。
        const char* displayName = nullptr;

        // 字段列表。注册顺序 = Inspector 内显示顺序。
        std::vector<PropertyDescriptor> properties;

        // 段顶 helper 文案（可空）。CollapsingHeader 展开后、properties 渲染之前
        // 显示一行（或多行）TextDisabled + Bullet，用来提示"这个 component 的隐藏
        // 心智模型"——典型用例：DirectionalLight 方向由 Transform.rotation 派生，
        // 多实例语义（first-found 生效）等 UI 上不自发现的约定。文案语言跟随
        // OrangeEditor UI（zh-CN）。
        // nullptr / 空串都视为"无 helper"，不占垂直空间。
        const char* helperText = nullptr;

        // ---- 类型擦除的 component-on-entity 访问 ----------------------------
        //
        // get 返回 void* —— SchemaInspector 把它喂给每个 PropertyDescriptor 的
        // get/set。caller 不能 cast 这个指针成任何具体类型；schema 内部 lambda
        // 内已写死了 reinterpret_cast，外界只当 opaque handle 用。
        //
        // add / remove 可选：nullptr 表示该 component 在当前 schema 体系下
        // 不能通过 Add Component 菜单 / 右键 Remove Component 操作（典型例外：
        // Hierarchy 由 DnD reparent 管理 / Animator 需要具体 IAnimator 子类）。
        //
        // AddFn 签名差异：has / get / remove 只需要 World + Entity，因为这些操
        // 作完全在 ECS 内部；add 需要 EditorHost 引用——typically default-construct
        // 即可（走 Builder::Addable()），但部分 component 在 +Add 路径需要预先
        // 注入 editor-side 资源（典型：Renderable 预绑 cubeMesh + defaultMaterial），
        // 这些预绑数据存在 EditorAssetContext / 其他 sub-context 内。让 AddFn 拿
        // EditorHost& 而非只是 World& 就避免了"add fn 通过全局 / 静态指针拿
        // assets"的丑陋写法。caller 在 add 内通过 `host.scene.pWorld->...` 取 world。
        using HasFn    = bool (*)(const Orange::Engine::World& world, Orange::Engine::Entity entity);
        using GetFn    = void* (*)(Orange::Engine::World & world, Orange::Engine::Entity entity);
        using AddFn    = void (*)(EditorHost& host, Orange::Engine::Entity entity);
        using RemoveFn = void (*)(Orange::Engine::World& world, Orange::Engine::Entity entity);

        HasFn    has    = nullptr;
        GetFn    get    = nullptr;
        AddFn    add    = nullptr;
        RemoveFn remove = nullptr;
    };

} // namespace Orange::Editor::Schema

#endif // ORANGE_EDITOR_SCHEMA_COMPONENT_SCHEMA_H
