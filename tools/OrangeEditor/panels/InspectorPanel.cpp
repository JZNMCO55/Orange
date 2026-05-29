// Inspector 面板（DrawInspectorPanel + Add Component popup）。
//
// v0.2.5 commit 10 收尾：所有 component 列举 / +Add 菜单都走 Component
// SchemaRegistry 迭代，本 TU 内不再 mention 任何具体内置 component 类型。
// 与 EditorRenderLayer.cpp 主 TU 共享同一类声明 EditorRenderLayer。

#include "../EditorRenderLayer.h"

#include "../CoplanarDetector.h"
#include "../schema/ComponentSchemaRegistry.h"
#include "../schema/SchemaInspector.h"
#include "../theme/EditorTheme.h"

#include <orange/engine/scene/World.h>

#include <imgui.h>

#include <string>

// Inspector 入口：选中实体的 entity id + 所有"已挂着的 component"各起一段
// schema-driven 渲染 + 一个 +Add Component popup 按 schema registry 枚举。
//
// c10 起本 TU 不再 mention 任何具体内置 component 类型；所有组件清单 +
// 顺序 + UI 行为都从 ComponentSchemaRegistry 取。游戏侧自定义 component
// 通过同一 registry 注册即可自动出现在 Inspector + +Add 菜单——v0.3 game
// side schema 注册落地后无需改本 TU 一行。
//
// v0.7 c0：原 v0.5 c5 在本 TU 顶部 hardcode 的 Material 子模式分支已迁
// 出为 plugin/MaterialAssetInspectorPlugin.{h,cpp}（IEditorAssetInspector
// Plugin 第一个真实 case）。本 TU 顶部 dispatch 改走 host.assetInspector
// Plugins 注册表遍历——新增按选中资源类型切 Inspector 内容的子模式（v0.7
// c2 .anim_fsm / 未来 .scene.json 预览）只需注册 plugin，不再改本 TU。

void EditorRenderLayer::DrawInspectorPanel()
{
    ImGui::Begin("Inspector");

    // v0.7 c0：按选中资源类型分派 Inspector 整段——遍历 host.assetInspector
    // Plugins 注册表，第一条 CanHandle(selectedAssetPath) == true 的 plugin
    // 接管整段，跳过实体 Inspector 默认路径。未命中（无选中资源 / 不认识
    // 的扩展名）继续走默认实体 Inspector。详见 plugin/IEditorAssetInspector
    // Plugin.h 头注释。
    for (const auto& pPlugin : mHost.assetInspectorPlugins)
    {
        if (pPlugin && pPlugin->CanHandle(mHost.assets.selectedAssetPath))
        {
            pPlugin->Draw(mHost, mHost.assets.selectedAssetPath);
            ImGui::End();
            return;
        }
    }

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

    // v0.8 多选 Inspector：primary 仍正常显示，additional 集合非空时上方
    // 加 banner。multi-edit 读侧（共有属性求交摘要）已落地——遍历 schema
    // registry 统计选区内各 component 类型的持有数，列出"全员共有"（一旦
    // 接通写回广播即作用于全部）与"部分持有"。写回广播（共有属性写到所有
    // 选中 entity）需 inspector 字段拖动分组基建（让一次多选拖动 = 一次
    // Undo，而非每实体一条），留后续；本段仅只读展示，不改字段写路径。
    const std::size_t selCount = mHost.selection.SelectedCount();
    if (selCount > 1)
    {
        ImGui::TextColored(Orange::Editor::Theme::Color::GetAlertWarn(),
                           "%zu entities selected (showing primary)",
                           selCount);

        auto& world = *mHost.scene.pWorld;
        // 选区内某 schema 的持有数（primary + 有效 additional）。
        auto countHaving = [&](const auto& schema) -> std::size_t {
            if (schema.has == nullptr) { return 0; }
            std::size_t n = schema.has(world, e) ? 1u : 0u;
            for (const auto a : mHost.selection.additionalSelectedEntities)
            {
                if (world.IsValid(a) && schema.has(world, a)) { ++n; }
            }
            return n;
        };

        std::string commonList;
        std::string partialList;
        for (const auto& schema : Orange::Editor::Schema::ComponentSchemaRegistry::Instance().All())
        {
            const std::size_t n = countHaving(schema);
            if (n == 0) { continue; }
            const char* nm = (schema.displayName != nullptr)
                           ? schema.displayName
                           : (schema.typeName ? schema.typeName : "?");
            if (n == selCount)
            {
                if (!commonList.empty()) { commonList += ", "; }
                commonList += nm;
            }
            else
            {
                if (!partialList.empty()) { partialList += ", "; }
                partialList += nm;
                partialList += " (" + std::to_string(n) + "/" + std::to_string(selCount) + ")";
            }
        }

        if (!commonList.empty())
        {
            ImGui::TextDisabled("shared by all: %s", commonList.c_str());
        }
        else
        {
            ImGui::TextDisabled("(no component shared by all selected)");
        }
        if (!partialList.empty())
        {
            ImGui::TextDisabled("partial: %s", partialList.c_str());
        }
        ImGui::TextDisabled("[ editing a shared field writes to all selected that have it ]");
        ImGui::Separator();
    }

    ImGui::Text("Entity #%u",
                static_cast<unsigned>(static_cast<std::uint32_t>(e.Value())));
    ImGui::Separator();

    // Geometry Warnings —— 当前选中 entity 的 mesh 与场景内邻居 mesh 是否
    // 存在 ε 共面（典型：cube 底面 y 与 ground 顶面 y 完全相同，主 pass
    // depthCompareOp = LessOrEqual 立刻出现 z-fight 斜条纹）。详见
    // docs/engine-known-gaps.md GAP-2026-05-21-editor-coplanar-mesh-z-fight-
    // prevention。仅在 hits 非空时显示，正常 entity 不打扰。
    {
        const auto coplanarHits = Orange::Editor::Coplanar::DetectCoplanar(mHost, e);
        if (!coplanarHits.empty())
        {
            ImGui::TextColored(Orange::Editor::Theme::Color::GetAlertError(),
                               "Geometry Warnings (%zu)",
                               coplanarHits.size());
            for (const auto& h : coplanarHits)
            {
                ImGui::BulletText("%s face coplanar with '%s'.%s (gap %.3fm)",
                                  Orange::Editor::Coplanar::FaceName(h.selfFace),
                                  h.otherName.c_str(),
                                  Orange::Editor::Coplanar::FaceName(h.otherFace),
                                  static_cast<double>(h.gap));
            }
            ImGui::Separator();
        }
    }

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
    const std::string addComponentLabel =
        std::string(Orange::Editor::Theme::Icon::GetAdd()) + " Add Component";
    if (ImGui::Button(addComponentLabel.c_str())) {
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
