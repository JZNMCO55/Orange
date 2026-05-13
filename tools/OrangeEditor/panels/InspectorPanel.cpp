// Inspector 面板（每个内置组件一个 Draw 段 + Add Component popup）。
// 与 EditorRenderLayer.cpp 主 TU 共享同一类声明 EditorRenderLayer。

#include "../EditorRenderLayer.h"

#include "../command/LambdaCommand.h"
#include "../command/SetFieldValueCommand.h"
#include "../schema/ComponentSchemaRegistry.h"
#include "../schema/SchemaInspector.h"

#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/ColliderDesc.h>
#include <orange/engine/physics/RigidBodyComponent.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/ParticleEmitterComponent.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <variant>

// Inspector 入口：选中实体的 entity id + 所有"已挂着的内置 component"
// 各起一个 CollapsingHeader 段。每段 if HasComponent → DrawXxx。
//
// 组件类型表是**硬编码**的（按"内置组件"列表枚举）。引擎当前
// 禁止 entt::meta / 反射，所以游戏侧自定义 component
// 暂时只能不显示 —— 后续真要扩展时走"编辑器扩展点 API 让游
// 戏注册自己的 inspector callback"路径，不在本期范围内。
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

    // v0.2.5 整骨期：已迁 schema 的 component 走 schema-driven 渲染；
    // 未迁的仍走 DrawInspectorXxx 硬编码路径。全部迁完后，整段 if-block
    // 统一替换为 Orange::Editor::Schema::DrawEntityViaSchemas(mHost, e)
    // 并删除剩余 DrawInspectorXxx 调用。
    //
    // 已迁顺序 = schema 注册顺序（见 RegisterBuiltinSchemas.cpp）= 这里
    // 显式分派的顺序，保证 v0.1 期 Inspector 内 component header 视觉
    // 顺序不变（Name → Transform → Hierarchy → DirectionalLight →
    // Renderable → RigidBody → Collider → ParticleEmitter → Animator）。
    auto& schemaReg = Orange::Editor::Schema::ComponentSchemaRegistry::Instance();
    using Orange::Editor::Schema::DrawComponentSchemaSection;
    if (const auto* s = schemaReg.Find<Orange::Engine::Scene::NameComponent>())
        { DrawComponentSchemaSection(mHost, e, *s); }
    if (const auto* s = schemaReg.Find<Orange::Engine::Scene::TransformComponent>())
        { DrawComponentSchemaSection(mHost, e, *s); }
    if (const auto* s = schemaReg.Find<Orange::Engine::Scene::HierarchyComponent>())
        { DrawComponentSchemaSection(mHost, e, *s); }
    if (const auto* s = schemaReg.Find<Orange::Engine::Render::DirectionalLight>())
        { DrawComponentSchemaSection(mHost, e, *s); }
    DrawInspectorRenderable(e);
    if (const auto* s = schemaReg.Find<Orange::Engine::Physics::RigidBodyComponent>())
        { DrawComponentSchemaSection(mHost, e, *s); }
    DrawInspectorCollider(e);
    if (const auto* s = schemaReg.Find<Orange::Engine::Render::ParticleEmitterComponent>())
        { DrawComponentSchemaSection(mHost, e, *s); }
    DrawInspectorAnimator(e);

    // ---- + Add Component -----------------------------------------
    // 列出尚未挂在本实体上的内置可添加组件。Animator 跳过 —— 需要具
    // 体 IAnimator 子类实例，不能用空 unique_ptr 默认构造。Hierarchy
    // 跳过 —— DnD 管理，手动 add 会出现 "孤立 HC"（parent invalid
    // 且不挂在任何父链上）。
    //
    // AddComponent 是破坏性操作（从此刻起历史才有意义），清掉 undo 历史
    // 防止旧命令对组件布局做出错误假设。
    ImGui::Separator();
    if (ImGui::Button("+ Add Component")) {
        ImGui::OpenPopup("##add_component");
    }
    if (ImGui::BeginPopup("##add_component")) {
        using namespace Orange::Engine::Scene;
        using namespace Orange::Engine::Render;
        using namespace Orange::Engine::Physics;
        auto& w = *mHost.scene.pWorld;
        if (!w.HasComponent<TransformComponent>(e)
            && ImGui::MenuItem("Transform")) {
            w.AddComponent<TransformComponent>(e, TransformComponent{});
            mHost.cmdStack.Clear();
        }
        if (!w.HasComponent<DirectionalLight>(e)
            && ImGui::MenuItem("Directional Light")) {
            w.AddComponent<DirectionalLight>(e, DirectionalLight{});
            mHost.cmdStack.Clear();
        }
        if (!w.HasComponent<RenderableComponent>(e)
            && ImGui::MenuItem("Renderable")) {
            // 新挂的 Renderable 默认绑内置 cube mesh + textured material，
            // 用户立刻能在 Scene 视口看到一个白色立方体；不挂 mesh /
            // material 的空 Renderable 等于隐形，对刚加完组件的用户来说
            // 没有可见反馈。
            RenderableComponent rc{};
            rc.mesh             = mHost.assets.cubeMeshHandle;
            rc.materialInstance = mHost.assets.pDefaultRenderableMaterial.get();
            w.AddComponent<RenderableComponent>(e, rc);
            mHost.cmdStack.Clear();
        }
        if (!w.HasComponent<RigidBodyComponent>(e)
            && ImGui::MenuItem("RigidBody")) {
            w.AddComponent<RigidBodyComponent>(e, RigidBodyComponent{});
            mHost.cmdStack.Clear();
        }
        if (!w.HasComponent<ColliderComponent>(e)
            && ImGui::MenuItem("Collider")) {
            w.AddComponent<ColliderComponent>(e, ColliderComponent{});
            mHost.cmdStack.Clear();
        }
        if (!w.HasComponent<ParticleEmitterComponent>(e)
            && ImGui::MenuItem("Particle Emitter")) {
            w.AddComponent<ParticleEmitterComponent>(e,
                ParticleEmitterComponent{});
            mHost.cmdStack.Clear();
        }
        ImGui::EndPopup();
    }

    ImGui::EndDisabled();
    ImGui::End();
}

// CollapsingHeader 包装 —— 多一个 "右键 → Remove Component" 上下文菜
// 单。outRemove 表示用户本帧请求了移除；调用方在 fields 渲染完后据
// 此调 RemoveComponent。把 remove 写在 fields 之后是为了让该帧的
// field 控件仍正常渲染，不会因为半途 remove 而 GetComponent 拿到野
// 指针。
//
// 不暴露 Name / Hierarchy 的 remove —— Name 总在让 Entity Tree 有
// 名字显示；Hierarchy 是 DnD 维护的结构性数据，手动 remove 会让自
// 身脱离父链且子节点变成孤儿。这两段调用方直接用裸 CollapsingHeader。
bool EditorRenderLayer::ComponentHeader(const char* label, bool* outRemove,
                                        bool defaultOpen)
{
    *outRemove = false;
    const bool open = ImGui::CollapsingHeader(
        label, defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Remove Component")) { *outRemove = true; }
        ImGui::EndPopup();
    }
    return open;
}

// ---- 各 component 段 -------------------------------------------------
//
// 每个 DrawInspectorXxx 的统一模式（v0.2 Command System）：
//   1. 先 HasComponent 检查 —— 不挂就整段不显示
//   2. CollapsingHeader（默认展开），点 header 可折叠
//   3. 控件前先捕获 old 值
//   4. 控件返回 true（有变化）时 Push SetFieldValueCommand<T>
//   5. RemoveComponent 直接执行 + Clear()（无法撤销，历史清零）

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

void EditorRenderLayer::DrawInspectorRenderable(Orange::Engine::Entity e)
{
    using RC = Orange::Engine::Render::RenderableComponent;
    if (!mHost.scene.pWorld->HasComponent<RC>(e)) { return; }
    bool remove = false;
    const bool open = ComponentHeader("Renderable", &remove);
    if (!open) {
        if (remove) {
            mHost.scene.pWorld->RemoveComponent<RC>(e);
            mHost.cmdStack.Clear();
        }
        return;
    }
    auto* r  = mHost.scene.pWorld->GetComponent<RC>(e);
    auto* pW = mHost.scene.pWorld.get();

    // mesh / materialInstance 是 handle / 裸指针 —— 编辑得通过 Asset
    // 浏览器（后续扩展）才有意义。这里只读显示。
    ImGui::Text("Mesh handle      : %llu",
                static_cast<unsigned long long>(r->mesh.Value()));
    ImGui::Text("MaterialInstance : %p",
                reinterpret_cast<void*>(r->materialInstance));

    {
        bool oldVisible = r->visible;
        if (ImGui::Checkbox("Visible", &r->visible)) {
            mHost.cmdStack.Push(std::make_unique<SetFieldValueCommand<bool>>(
                e, "renderable.visible", oldVisible, r->visible,
                [pW, e](const bool& v) {
                    if (auto* rc = pW->GetComponent<RC>(e)) rc->visible = v;
                }));
        }
    }
    {
        bool oldShadow = r->castsShadow;
        if (ImGui::Checkbox("Casts Shadow", &r->castsShadow)) {
            mHost.cmdStack.Push(std::make_unique<SetFieldValueCommand<bool>>(
                e, "renderable.castsShadow", oldShadow, r->castsShadow,
                [pW, e](const bool& v) {
                    if (auto* rc = pW->GetComponent<RC>(e)) rc->castsShadow = v;
                }));
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "本物体是否参与投射阴影（per-object 开关）。\n"
                "关掉对应 \"几何不投影但仍接收阴影\"（典型用例：透明 UI / 装饰物 /\n"
                "近景特效）。与 DirectionalLight 的同名 flag 是 AND 关系：两个都\n"
                "必须为 true 才会真投影。");
        }
    }

    if (remove) {
        mHost.scene.pWorld->RemoveComponent<RC>(e);
        mHost.cmdStack.Clear();
    }
}

// DrawInspectorRigidBody 已删除 —— v0.2.5 commit 4 起 RigidBodyComponent
// 走 schema-driven 渲染（schema/RegisterBuiltinSchemas.cpp 内 RegisterRigid
// BodyComponentSchema()）。`handle` 字段（PhysicsWorld::AddBody 反写的运
// 行时 BodyHandle）暂未在 schema 内显示——schema 系统当前没有 "DisplayOnly /
// read-only" 字段标记，本期接受这一行 TextDisabled 调试值的视觉降级，等
// 后续 commit 引入 read-only display attribute 后再补回。Combo 控件能力
// 由本 commit 同步引入的 PropertyType::Enum 提供，BodyType enum 项名表见
// schema 注册代码。

void EditorRenderLayer::DrawInspectorCollider(Orange::Engine::Entity e)
{
    using CC = Orange::Engine::Physics::ColliderComponent;
    using ::Orange::Engine::Physics::CircleDesc;
    using ::Orange::Engine::Physics::BoxDesc;
    using ::Orange::Engine::Physics::PolygonDesc;
    using ::Orange::Engine::Physics::EdgeChainDesc;
    if (!mHost.scene.pWorld->HasComponent<CC>(e)) { return; }
    bool remove = false;
    const bool open = ComponentHeader("Collider", &remove);
    if (!open) {
        if (remove) {
            mHost.scene.pWorld->RemoveComponent<CC>(e);
            mHost.cmdStack.Clear();
        }
        return;
    }
    auto* c  = mHost.scene.pWorld->GetComponent<CC>(e);
    auto* pW = mHost.scene.pWorld.get();

    // shape 是 std::variant —— 显示 shape 类型 + 各自的简单数值。
    // 切换 shape 类型（assign 一个不同 alternative）会重置数据，
    // 比起 Inspector 一行 Combo 误操作风险大，这里**不**提供切换
    // 控件，留给后续 collider 编辑专用 UI。
    if (std::holds_alternative<CircleDesc>(c->shape)) {
        auto& s = std::get<CircleDesc>(c->shape);
        ImGui::Text("Shape: Circle");
        {
            float old = s.radius;
            if (ImGui::DragFloat("Radius", &s.radius, 0.01f, 0.0f, 0.0f)) {
                mHost.cmdStack.Push(std::make_unique<SetFieldValueCommand<float>>(
                    e, "collider.circle.radius", old, s.radius,
                    [pW, e](const float& v) {
                        if (auto* cc = pW->GetComponent<CC>(e))
                            if (auto* sd = std::get_if<CircleDesc>(&cc->shape))
                                sd->radius = v;
                    }));
            }
        }
        {
            glm::vec2 old = s.center;
            if (ImGui::DragFloat2("Center", &s.center.x, 0.01f)) {
                mHost.cmdStack.Push(std::make_unique<SetFieldValueCommand<glm::vec2>>(
                    e, "collider.circle.center", old, s.center,
                    [pW, e](const glm::vec2& v) {
                        if (auto* cc = pW->GetComponent<CC>(e))
                            if (auto* sd = std::get_if<CircleDesc>(&cc->shape))
                                sd->center = v;
                    }));
            }
        }
    } else if (std::holds_alternative<BoxDesc>(c->shape)) {
        auto& s = std::get<BoxDesc>(c->shape);
        ImGui::Text("Shape: Box");
        {
            glm::vec2 old = s.halfExtents;
            if (ImGui::DragFloat2("Half Extents", &s.halfExtents.x, 0.01f)) {
                mHost.cmdStack.Push(std::make_unique<SetFieldValueCommand<glm::vec2>>(
                    e, "collider.box.halfExtents", old, s.halfExtents,
                    [pW, e](const glm::vec2& v) {
                        if (auto* cc = pW->GetComponent<CC>(e))
                            if (auto* sd = std::get_if<BoxDesc>(&cc->shape))
                                sd->halfExtents = v;
                    }));
            }
        }
        {
            glm::vec2 old = s.center;
            if (ImGui::DragFloat2("Center", &s.center.x, 0.01f)) {
                mHost.cmdStack.Push(std::make_unique<SetFieldValueCommand<glm::vec2>>(
                    e, "collider.box.center", old, s.center,
                    [pW, e](const glm::vec2& v) {
                        if (auto* cc = pW->GetComponent<CC>(e))
                            if (auto* sd = std::get_if<BoxDesc>(&cc->shape))
                                sd->center = v;
                    }));
            }
        }
    } else if (std::holds_alternative<PolygonDesc>(c->shape)) {
        const auto& s = std::get<PolygonDesc>(c->shape);
        ImGui::Text("Shape: Polygon (%u verts)",
                    static_cast<unsigned>(s.count));
        ImGui::TextDisabled("(polygon vertex editing — later task)");
    } else if (std::holds_alternative<EdgeChainDesc>(c->shape)) {
        const auto& s = std::get<EdgeChainDesc>(c->shape);
        ImGui::Text("Shape: EdgeChain (%u verts, loop=%s)",
                    static_cast<unsigned>(s.count),
                    s.isLoop ? "yes" : "no");
        ImGui::TextDisabled("(edge chain editing — later task)");
    }
    ImGui::Separator();
    {
        float old = c->density;
        if (ImGui::DragFloat("Density", &c->density, 0.01f, 0.0f, 0.0f)) {
            mHost.cmdStack.Push(std::make_unique<SetFieldValueCommand<float>>(
                e, "collider.density", old, c->density,
                [pW, e](const float& v) {
                    if (auto* cc = pW->GetComponent<CC>(e)) cc->density = v;
                }));
        }
    }
    {
        float old = c->friction;
        if (ImGui::DragFloat("Friction", &c->friction, 0.01f, 0.0f, 1.0f)) {
            mHost.cmdStack.Push(std::make_unique<SetFieldValueCommand<float>>(
                e, "collider.friction", old, c->friction,
                [pW, e](const float& v) {
                    if (auto* cc = pW->GetComponent<CC>(e)) cc->friction = v;
                }));
        }
    }
    {
        float old = c->restitution;
        if (ImGui::DragFloat("Restitution", &c->restitution, 0.01f, 0.0f, 1.0f)) {
            mHost.cmdStack.Push(std::make_unique<SetFieldValueCommand<float>>(
                e, "collider.restitution", old, c->restitution,
                [pW, e](const float& v) {
                    if (auto* cc = pW->GetComponent<CC>(e)) cc->restitution = v;
                }));
        }
    }
    {
        bool old = c->isSensor;
        if (ImGui::Checkbox("Is Sensor", &c->isSensor)) {
            mHost.cmdStack.Push(std::make_unique<SetFieldValueCommand<bool>>(
                e, "collider.isSensor", old, c->isSensor,
                [pW, e](const bool& v) {
                    if (auto* cc = pW->GetComponent<CC>(e)) cc->isSensor = v;
                }));
        }
    }

    if (remove) {
        mHost.scene.pWorld->RemoveComponent<CC>(e);
        mHost.cmdStack.Clear();
    }
}

void EditorRenderLayer::DrawInspectorAnimator(Orange::Engine::Entity e)
{
    using AC = Orange::Engine::Animation::AnimatorComponent;
    if (!mHost.scene.pWorld->HasComponent<AC>(e)) { return; }
    if (!ImGui::CollapsingHeader("Animator")) { return; }
    const auto* a = mHost.scene.pWorld->GetComponent<AC>(e);
    // AnimatorComponent 持 unique_ptr<IAnimator>，是 move-only 抽象类指
    // 针，运行时 "换 backend" 不是 inspector 一行 combo 能搞定的。这里
    // 仅显示是否挂着 + 指针地址；详细参数交给后续动画子模式。
    ImGui::Text("Animator (runtime) : %p",
                reinterpret_cast<const void*>(a->animator.get()));
    ImGui::TextDisabled("(animator backend editing — later task)");
}
