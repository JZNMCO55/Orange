// Inspector 面板（DrawInspectorPanel + Add Component popup）。
//
// v0.2.5 commit 10 收尾：所有 component 列举 / +Add 菜单都走 Component
// SchemaRegistry 迭代，本 TU 内不再 mention 任何具体内置 component 类型。
// 与 EditorRenderLayer.cpp 主 TU 共享同一类声明 EditorRenderLayer。

#include "../EditorRenderLayer.h"

#include "../schema/ComponentSchemaRegistry.h"
#include "../schema/SchemaInspector.h"

#include <orange/engine/scene/World.h>

#include <imgui.h>

// Inspector 入口：选中实体的 entity id + 所有"已挂着的 component"各起一段
// schema-driven 渲染 + 一个 +Add Component popup 按 schema registry 枚举。
//
// c10 起本 TU 不再 mention 任何具体内置 component 类型；所有组件清单 +
// 顺序 + UI 行为都从 ComponentSchemaRegistry 取。游戏侧自定义 component
// 通过同一 registry 注册即可自动出现在 Inspector + +Add 菜单——v0.3 game
// side schema 注册落地后无需改本 TU 一行。
void EditorRenderLayer::DrawInspectorPanel()
{
    ImGui::Begin("Inspector");
    if (mHost.scene.pWorld == nullptr || !mHost.selection.selectedEntity.IsValid()) {
        ImGui::TextDisabled("(select an entity)");
        ImGui::End();
        return;
    }
    // Entity::IsValid() 只检查是否为哨兵 null 值；Undo 可能已销毁该实体但
    // 未清掉句柄。World::IsValid 走 registry.valid()，可正确甄别死实体。
    if (!mHost.scene.pWorld->IsValid(mHost.selection.selectedEntity)) {
        mHost.selection.selectedEntity            = Orange::Engine::Entity::Invalid();
        mHost.selection.transformEulerCacheEntity = Orange::Engine::Entity::Invalid();
        ImGui::TextDisabled("(select an entity)");
        ImGui::End();
        return;
    }

    const Orange::Engine::Entity e = mHost.selection.selectedEntity;
    ImGui::Text("Entity #%u",
                static_cast<unsigned>(static_cast<std::uint32_t>(e.Value())));
    ImGui::Separator();

    // Play / Paused 期间所有 component 字段只读（灰显但可见）。
    const bool canEdit = (mHost.scene.playState == PlayState::Edit);
    if (!canEdit) {
        ImGui::TextDisabled("[ Read-only in Play / Paused ]");
        ImGui::Separator();
    }
    ImGui::BeginDisabled(!canEdit);

    // 所有 schema 段按注册顺序逐段渲染（schema.has 内部已 guard 未挂的 entity，
    // 不会画空段）。注册顺序见 schema/RegisterBuiltinSchemas.cpp，与 v0.1
    // 期 Inspector 内 component header 顺序一致：Name → Transform → Hierarchy
    // → DirectionalLight → Renderable → RigidBody → Collider → ParticleEmitter
    // → Animator。
    Orange::Editor::Schema::DrawEntityViaSchemas(mHost, e);

    // ---- + Add Component -------------------------------------------------
    // 枚举 ComponentSchemaRegistry 内所有挂了 `.Addable()` / `.AddableWith()`
    // 的 schema 项；过滤掉已挂在本实体上的 component。schema.add(host, entity)
    // 完成实际挂载——默认 Addable 走 `world.AddComponent<C>(e, C{})`，
    // AddableWith 走调用方自定义 lambda（典型：Renderable 预绑 cubeMesh +
    // defaultMaterial）。
    //
    // AddComponent 是破坏性操作（从此刻起历史才有意义），清掉 undo 历史
    // 防止旧命令对组件布局做出错误假设。
    //
    // 跳过的 component（schema 未挂 Addable）：
    //   * Name / Hierarchy —— 由 entity 创建路径 / Entity Tree DnD 自动管理
    //   * Animator         —— IAnimator 抽象类，需要具体子类实例
    auto& schemaReg = Orange::Editor::Schema::ComponentSchemaRegistry::Instance();
    ImGui::Separator();
    if (ImGui::Button("+ Add Component")) {
        ImGui::OpenPopup("##add_component");
    }
    if (ImGui::BeginPopup("##add_component")) {
        auto* pWorld = mHost.scene.pWorld.get();
        if (pWorld != nullptr)
        {
            for (const auto& schema : schemaReg.All()) {
                if (schema.add == nullptr) { continue; }        // 未挂 Addable
                if (schema.has != nullptr && schema.has(*pWorld, e)) {
                    continue;                                    // 已挂
                }
                const char* label = (schema.displayName != nullptr)
                                  ? schema.displayName
                                  : (schema.typeName ? schema.typeName : "?");
                if (ImGui::MenuItem(label)) {
                    schema.add(mHost, e);
                    mHost.cmdStack.Clear();
                }
            }
        }
        ImGui::EndPopup();
    }

    ImGui::EndDisabled();
    ImGui::End();
}

// ---- v0.2.5 schema-driven 迁移历史索引 -------------------------------
//
// v0.2.5 commit 3 ~ 10 把 EditorRenderLayer 的 9 个 DrawInspectorXxx 成员
// 函数 + 静态 ComponentHeader helper 全数清除，每个 component 段都由
// schema/RegisterBuiltinSchemas.cpp 内的 Register*ComponentSchema() 注册
// 驱动；ComponentHeaderLocal 在 schema/SchemaInspector.cpp 内部承担同等
// 角色的 CollapsingHeader 包装。
//
// 旧 DrawInspectorXxx → 迁移 commit 对照：

// DrawInspectorName / DrawInspectorTransform / DrawInspectorHierarchy 已删除
// —— v0.2.5 commit 5 起：
//   * NameComponent       走 PropertyType::String 通用路径（schema/Register
//                         BuiltinSchemas.cpp 内 RegisterNameComponentSchema）。
//                         RenameCommand 在 schema 路径下被 SetFieldValueCommand
//                         <std::string> 取代——CommandStack::Push 内 fieldKey
//                         "Name.name" + 同 entity 触发 Merge，等价 RenameCommand
//                         的连续改名 coalesce 行为
//   * TransformComponent  position / scale 走 Vec3 通用路径；rotation 走 Quat
//                         case（SchemaInspector.cpp）的 Euler-cache 混合路径，
//                         缓存仍在 EditorSelection.transformEulerCache，apply
//                         lambda 内 invalidate cache
//   * HierarchyComponent  4 个 Entity 字段走 PropertyType::EntityRef 只读路径
//
// v0.1 期 Hierarchy 段的 ImGui::TextDisabled "(edit by drag-drop in Entity
// Tree)" 提示本期接受视觉降级；后续 v0.3 IEditorInspectorPlugin 落地时还原。

// DrawInspectorDirectionalLight 已删除 —— v0.2.5 commit 3 起 DirectionalLight
// 走 schema-driven 渲染（schema/RegisterBuiltinSchemas.cpp）。"Normalize
// Direction" 一键按钮目前未在 schema 路径表达：schema 系统当前只支持
// 字段 read/write，没有"action button"概念。后续 commit 在 schema 系统
// 引入 ActionButton attribute 或独立 IEditorInspectorPlugin 路径后再
// 还原该 helper（短期可接受退化——direction 在 ImGui DragFloat3 拖动
// 时大概率仍是单位向量附近的值；Pipeline 着色阶段按需 normalize 保底）。

// DrawInspectorRigidBody 已删除 —— v0.2.5 commit 4 起 RigidBodyComponent
// 走 schema-driven 渲染（schema/RegisterBuiltinSchemas.cpp 内 RegisterRigid
// BodyComponentSchema()）。`handle` 字段（PhysicsWorld::AddBody 反写的运
// 行时 BodyHandle）暂未在 schema 内显示——schema 系统当前没有 "DisplayOnly /
// read-only" 字段标记，本期接受这一行 TextDisabled 调试值的视觉降级，等
// 后续 commit 引入 read-only display attribute 后再补回。Combo 控件能力
// 由本 commit 同步引入的 PropertyType::Enum 提供，BodyType enum 项名表见
// schema 注册代码。

// DrawInspectorCollider 已删除 —— v0.2.5 commit 8 起 ColliderComponent
// 走 schema-driven 渲染（schema/RegisterBuiltinSchemas.cpp 内 Register
// ColliderComponentSchema()）。
//
// shape 是 std::variant<CircleDesc, BoxDesc, PolygonDesc, EdgeChainDesc>；
// 本 commit 同步引入 PropertyAttributes::visibleIf + Builder::VisibleIf /
// Builder::Group 两个 schema 入口，把 v0.1 hardcode 内"按 alternative 显
// 示不同字段"的行为表达成"每个 shape 段挂 visibleIf(holds_alternative<X>)"。
// shape 类型切换控件**不**引入（v0.1 deliberately not implemented，避免
// 切换 alternative 时重置数据；保留给后续 collider 专用 UI / v0.4 Gizmo）。
//
// 视觉降级（vs v0.1）：
//   * Polygon / EdgeChain 段不再显示动态 vertex count / loop 状态
//     （v0.1 `Shape: Polygon (%u verts)` / `Shape: EdgeChain (... loop=...)`）
//   * 字段顺序由 "shape 段在前 / 通用字段在后" 改为 "通用字段在前 / shape
//     段在后" —— schema 是线性顺序，Polygon / EdgeChain 的零字段段放在中
//     间会让通用字段视觉被挤；颠倒顺序让 shape 段总在 component 段末尾
// 两条都属 informational 降级，运行时行为与 v0.1 一致。

// DrawInspectorAnimator 已删除 —— v0.2.5 commit 9 起 AnimatorComponent
// 走 schema-driven 渲染（schema/RegisterBuiltinSchemas.cpp 内 Register
// AnimatorComponentSchema()）。
//
// v0.1 hardcode 显示两行：
//   1. `Animator (runtime) : <ptr>` —— 原始 IAnimator* 指针地址
//   2. `(animator backend editing — later task)` TextDisabled 占位
//
// c9 改显 backend name 字符串（"skeletal_dragonbones" / "procedural" 等）。
// 同步引入 PropertyAttributes::readOnly + Builder::ReadOnly() 让 String
// 字段走"Text + SameLine + TextDisabled"展示路径，不暴露 InputText。
//
// 信息变更（vs v0.1）：
//   * 指针地址  → backend name（更可读，信息量提升）
//   * "later task" 占位文本 → 不再显示（informational 删除）
// 行为变更：无（v0.1 也无任何编辑能力）。
//
// readOnly attribute 同时为后续还原 c4 RigidBody.handle / c7 Renderable
// mesh handle / MaterialInstance ptr 的调试显示打下基础——但那些字段还
// 需要专门的 PropertyType（AssetHandle / BodyHandle 等），等专门 commit
// 解决，本 commit 不顺手做。
