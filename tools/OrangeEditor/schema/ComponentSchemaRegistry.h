#ifndef ORANGE_EDITOR_SCHEMA_COMPONENT_SCHEMA_REGISTRY_H
#define ORANGE_EDITOR_SCHEMA_COMPONENT_SCHEMA_REGISTRY_H

// ComponentSchemaRegistry —— 编辑器进程内所有已注册 ComponentSchema 的
// 单例 store + std::type_index 索引的查找入口。
//
// 设计：
//   * 单例（Instance() 返回 static 引用）—— main 启动期调
//     RegisterBuiltinSchemas() 一次性把内置 component schema 全注册；
//     未来 v0.3 游戏侧自定义 component 通过 OrangeEditor::RegisterComponentSchema
//     扩展点继续往 registry 里加
//   * 存储用 std::deque<ComponentSchema> —— **不允许 vector**：注册的
//     schema 内 PropertyDescriptor 地址被 SetFieldValueCommand 的 lambda
//     间接通过 PropertyDescriptor::set 函数指针读到，vector 扩容重排会
//     让函数指针仍指向无效内存位置（函数指针本身是稳定的，但 schema 内
//     的 properties 是 std::vector 字段——vector 重排不影响函数指针，但
//     注册期间 deque 让 schema 本身地址稳定，方便后续 plugin 引用）
//   * lookup by std::type_index —— typeid(T) 拿 component 类型 ID；标准
//     库 free，与 entt 解耦
//
// 与 Lumix `reflection::getComponent(ComponentType)` 对应；Lumix 用自
// 己的 ComponentType（字符串 hash），OrangeEditor 直接用 std::type_index。
//
// 线程安全：本 registry **不**线程安全；编辑器是单线程 ImGui main loop，
// 所有 Register 调用在启动期单线程完成，Find 在 Inspector 帧内单线程
// 调用，无并发需求。

#include "ComponentSchema.h"
#include "PropertyDescriptor.h"

#include <deque>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>

namespace Orange::Editor::Schema
{

class ComponentSchemaRegistry
{
public:
    static ComponentSchemaRegistry& Instance();

    // 注册一个 ComponentSchema。同 type_index 重复注册会断言（开发期 bug，
    // 内置 + 游戏侧重名）；schema 通过移动入栈，按注册顺序追加到 mSchemas
    // 末尾——遍历顺序 = 注册顺序 = Inspector 内 component header 顺序。
    void Register(std::type_index typeIdx, ComponentSchema schema);

    template <typename C>
    void Register(ComponentSchema schema)
    {
        Register(std::type_index(typeid(C)), std::move(schema));
    }

    // 找不到返回 nullptr。返回 const* —— schema 注册后不可变（mutate 会让
    // 已 in-flight 的命令 lambda 捕获过期 PropertyDescriptor 引用，破坏
    // Undo/Redo 一致性）。
    const ComponentSchema* Find(std::type_index typeIdx) const;

    template <typename C>
    const ComponentSchema* Find() const
    {
        return Find(std::type_index(typeid(C)));
    }

    // 所有已注册 schema 的有序视图（注册顺序）。Inspector 用这个遍历"对
    // 当前 entity 已挂的每个 component 画 header 段"。
    const std::deque<ComponentSchema>& All() const { return mSchemas; }

    std::size_t Count() const { return mSchemas.size(); }

private:
    ComponentSchemaRegistry() = default;
    ComponentSchemaRegistry(const ComponentSchemaRegistry&)            = delete;
    ComponentSchemaRegistry& operator=(const ComponentSchemaRegistry&) = delete;

    std::deque<ComponentSchema> mSchemas;
    std::unordered_map<std::type_index, std::size_t> mByType;
};

// ---------------------------------------------------------------------------
// ComponentSchemaBuilder —— 类型安全的 schema 构造助手
// ---------------------------------------------------------------------------
//
// 用法（field 走 NTTP，attribute 走链式调用）：
//
//   ComponentSchemaBuilder<DirectionalLight>("DirectionalLight",
//                                            "Directional Light")
//       .Field<&DirectionalLight::direction>("direction", "Direction")
//           .DragSpeed(0.01f)
//       .Field<&DirectionalLight::color>("color", "Color").Color()
//       .Field<&DirectionalLight::intensity>("intensity", "Intensity")
//           .Range(0.0f, 1000.0f).DragSpeed(0.05f)
//       .Field<&DirectionalLight::castsShadow>("castsShadow", "Casts Shadow")
//           .Tooltip("...")
//       .Addable()
//       .Removable()
//       .Register();
//
// 设计要点：
//   * Field 用 NTTP（C++20 允许 member-ptr 作为模板非类型参数）。这样
//     lambda 完全 capture-less，可零成本转 PropertyDescriptor::set/get
//     的函数指针——若 fieldPtr 作为值传入 Field(...) 则 lambda 必须
//     capture 它，转函数指针失败
//   * Attribute 修饰链（Range / DragSpeed / Color / Tooltip）作用于
//     **最近一次 Field 调用**所对应的 property。实现走 mSchema.properties
//     .back() 修饰
//   * Addable / Removable 是 component 级——不属于 property，挂在 schema
//   * .Register() 终结整个 builder，把 schema 移交 registry
//
// 设计参考：
//   * vendor/LumixEngine/src/engine/reflection.h `build_module` 宏 +
//     `cmp<>().prop<>()` chaining
//   * vendor/godot/core/object/class_db.h `ClassDB::bind_method` +
//     `ADD_PROPERTY`

template <typename C>
class ComponentSchemaBuilder
{
public:
    ComponentSchemaBuilder(const char* typeName, const char* displayName)
    {
        mSchema.typeName    = typeName;
        mSchema.displayName = displayName;
        mSchema.has = [](const Orange::Engine::World& w, Orange::Engine::Entity e) {
            return w.HasComponent<C>(e);
        };
        mSchema.get = [](Orange::Engine::World& w, Orange::Engine::Entity e) -> void* {
            return static_cast<void*>(w.GetComponent<C>(e));
        };
    }

    // 注册一个字段。FieldPtr 作为 NTTP（member pointer-to-data）传入，
    // PropertyType 由其 pointee 类型经 PropertyTypeOf<T> 推导。
    template <auto FieldPtr>
    ComponentSchemaBuilder& Field(const char* name, const char* label)
    {
        using FieldT = std::remove_reference_t<decltype(std::declval<C>().*FieldPtr)>;
        PropertyDescriptor pd{};
        pd.name  = name;
        pd.label = label;
        pd.type  = PropertyTypeOf<FieldT>::value;
        pd.get = [](const void* component, void* outValue) {
            *static_cast<FieldT*>(outValue) =
                (static_cast<const C*>(component))->*FieldPtr;
        };
        pd.set = [](void* component, const void* inValue) {
            (static_cast<C*>(component))->*FieldPtr =
                *static_cast<const FieldT*>(inValue);
        };
        mSchema.properties.push_back(pd);
        return *this;
    }

    // 注册一个 enum / enum class 字段。caller-side marshal 类型固定为 int
    // ——SchemaInspector 的 Enum case 用 int buffer 配合 ImGui::Combo，
    // SetFieldValueCommand<int> 入栈；Builder 内部 lambda 负责 enum ↔
    // int 的 underlying_type 转换。
    //
    // 与 Field<> 分开提供专用入口的原因：enum 走 UnderlyingT 路径在
    // PropertyTypeOf<T> 主表里要么写 std::is_enum_v 偏特化（隐式魔法），
    // 要么强制 caller 在 Field<> 后另调 EnumNames（PropertyType 还是
    // 错的 Int）。显式 FieldEnum 比这两条都干净。
    //
    // 设计参考：vendor/godot/core/object/class_db.h 的 PROPERTY_HINT_ENUM
    // ——Godot 也是 enum 走专用 hint，runtime side 看作 int64_t。
    template <auto FieldPtr>
    ComponentSchemaBuilder& FieldEnum(const char* name, const char* label)
    {
        using FieldT = std::remove_reference_t<decltype(std::declval<C>().*FieldPtr)>;
        static_assert(std::is_enum_v<FieldT>,
                      "FieldEnum requires an enum (or enum class) member; "
                      "use Field<> for non-enum types.");
        using UnderlyingT = std::underlying_type_t<FieldT>;

        PropertyDescriptor pd{};
        pd.name  = name;
        pd.label = label;
        pd.type  = PropertyType::Enum;
        pd.get = [](const void* component, void* outValue) {
            const FieldT& field = (static_cast<const C*>(component))->*FieldPtr;
            *static_cast<int*>(outValue) =
                static_cast<int>(static_cast<UnderlyingT>(field));
        };
        pd.set = [](void* component, const void* inValue) {
            (static_cast<C*>(component))->*FieldPtr = static_cast<FieldT>(
                static_cast<UnderlyingT>(*static_cast<const int*>(inValue)));
        };
        mSchema.properties.push_back(pd);
        return *this;
    }

    // ---- 最近一次 Field 的 attribute 修饰 -----------------------------

    ComponentSchemaBuilder& Range(float minV, float maxV)
    {
        auto& a = mSchema.properties.back().attribs;
        a.hasRange = true;
        a.minValue = minV;
        a.maxValue = maxV;
        return *this;
    }

    ComponentSchemaBuilder& DragSpeed(float speed)
    {
        mSchema.properties.back().attribs.dragSpeed = speed;
        return *this;
    }

    ComponentSchemaBuilder& Color()
    {
        mSchema.properties.back().attribs.isColor = true;
        return *this;
    }

    ComponentSchemaBuilder& Tooltip(const char* text)
    {
        mSchema.properties.back().attribs.tooltip = text;
        return *this;
    }

    // PropertyType::Enum 专用：Combo 项名表。names 数组与 count 都要求静
    // 态生命周期（Builder 不复制）；元素顺序 = enum underlying 值 0..count-1。
    ComponentSchemaBuilder& EnumNames(const char* const* names, int count)
    {
        auto& a = mSchema.properties.back().attribs;
        a.enumNames = names;
        a.enumCount = count;
        return *this;
    }

    // ---- component 级配置 -------------------------------------------------

    // 允许通过 "+ Add Component" 菜单添加。默认构造 C{} 插入。
    ComponentSchemaBuilder& Addable()
    {
        mSchema.add = [](Orange::Engine::World& w, Orange::Engine::Entity e) {
            w.AddComponent<C>(e, C{});
        };
        return *this;
    }

    // 允许通过 component header 右键菜单移除。
    ComponentSchemaBuilder& Removable()
    {
        mSchema.remove = [](Orange::Engine::World& w, Orange::Engine::Entity e) {
            w.RemoveComponent<C>(e);
        };
        return *this;
    }

    // 终结：把构造好的 schema 推进全局 registry。注意：不能用 `&&` 引用
    // 限定——builder 的 attribute 链式调用全部返回 `Builder&` (lvalue)，
    // 在 lvalue 上调 `&&` 方法需要显式 std::move，破坏 fluent 体感。
    // 因此 Register 是普通成员；调用方约定上"每个 builder 只 Register
    // 一次"，重复调用会进入 registry 重复注册路径触发 assert/log。
    void Register()
    {
        ComponentSchemaRegistry::Instance().Register<C>(std::move(mSchema));
    }

private:
    ComponentSchema mSchema;
};

}  // namespace Orange::Editor::Schema

#endif  // ORANGE_EDITOR_SCHEMA_COMPONENT_SCHEMA_REGISTRY_H
