// RegisterBuiltinSchemas 实现 —— v0.2.5 commit 3 起，把内置 component 逐
// 步迁移到 schema-driven Inspector。
//
// 本 TU 是所有内置 component schema 的注册入口；新增内置 component
// schema 直接在此追加 Builder 调用即可，无需新增 TU。
//
// 注册顺序 = Inspector 内 component header 显示顺序——与 v0.1/v0.2 的
// DrawInspectorXxx 调用顺序保持视觉一致（防止"切到 schema-driven 后
// component 顺序变了" 的用户感知 regression）。

#include "RegisterBuiltinSchemas.h"

#include "ComponentSchemaRegistry.h"

#include <orange/engine/physics/RigidBodyComponent.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/ParticleEmitterComponent.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/TransformComponent.h>

namespace Orange::Editor::Schema
{

namespace
{

void RegisterDirectionalLightSchema()
{
    using DL = Orange::Engine::Render::DirectionalLight;
    ComponentSchemaBuilder<DL>("DirectionalLight", "Directional Light")
        .Field<&DL::direction>("direction", "Direction")
            .DragSpeed(0.01f)
        .Field<&DL::color>("color", "Color")
            .Color()
        .Field<&DL::intensity>("intensity", "Intensity")
            .Range(0.0f, 1000.0f)
            .DragSpeed(0.05f)
        .Field<&DL::castsShadow>("castsShadow", "Casts Shadow")
            .Tooltip("本光源整体是否参与投影计算（全局开关）。\n"
                     "关闭后场景中不会有任何阴影，即便 Renderable 上勾了 Casts Shadow。\n"
                     "与 Renderable 的同名 flag 是 AND 关系：两个都必须为 true 才会真投影。")
        .Addable()
        .Removable()
        .Register();
}

void RegisterRigidBodyComponentSchema()
{
    using RB = Orange::Engine::Physics::RigidBodyComponent;
    using BT = Orange::Engine::Physics::BodyType;

    // BodyType enum 项名表 —— 顺序与 enum class 定义 (Static=0, Kinematic=1,
    // Dynamic=2) 严格对齐。静态生命周期；EnumNames 不复制。
    static const char* const kBodyTypeNames[] = {"Static", "Kinematic", "Dynamic"};
    static_assert(static_cast<int>(BT::Static)    == 0, "BodyType enum drift");
    static_assert(static_cast<int>(BT::Kinematic) == 1, "BodyType enum drift");
    static_assert(static_cast<int>(BT::Dynamic)   == 2, "BodyType enum drift");

    // `handle` 字段不暴露——这是 PhysicsWorld::AddBody 反写的运行时引用，
    // 编辑器不该编辑。v0.1 期 DrawInspectorRigidBody 通过 TextDisabled 给
    // 它显示一行调试值；schema 系统当前没有 "DisplayOnly" 字段标记，本
    // commit 接受这一行视觉降级，等后续 commit 引入 read-only display
    // attribute 后再补回。
    ComponentSchemaBuilder<RB>("RigidBody", "RigidBody")
        .FieldEnum<&RB::type>("type", "Type")
            .EnumNames(kBodyTypeNames, 3)
        .Field<&RB::initialPosition>("initialPosition", "Initial Position")
            .DragSpeed(0.05f)
        .Field<&RB::initialAngle>("initialAngle", "Initial Angle (rad)")
            .DragSpeed(0.01f)
        .Field<&RB::linearVelocity>("linearVelocity", "Linear Velocity")
            .DragSpeed(0.05f)
        .Field<&RB::angularVelocity>("angularVelocity", "Angular Velocity")
            .DragSpeed(0.05f)
        .Field<&RB::linearDamping>("linearDamping", "Linear Damping")
            .Range(0.0f, 100.0f)
            .DragSpeed(0.01f)
        .Field<&RB::angularDamping>("angularDamping", "Angular Damping")
            .Range(0.0f, 100.0f)
            .DragSpeed(0.01f)
        .Field<&RB::fixedRotation>("fixedRotation", "Fixed Rotation")
        .Field<&RB::gravityScale>("gravityScale", "Gravity Scale")
            .DragSpeed(0.05f)
        .Addable()
        .Removable()
        .Register();
}

void RegisterNameComponentSchema()
{
    using NC = Orange::Engine::Scene::NameComponent;
    // 视觉与 v0.1 hardcode 段对齐：
    //   * label 用 "##name" 隐藏 ImGui 控件左侧 label，让 InputText 横向占满
    //     CollapsingHeader 内宽（v0.1 `ImGui::InputText("##name", ...)` 同款）
    //   * 不 Addable / 不 Removable —— Name 由 entity 创建路径自动挂上，
    //     Inspector 不给手动添加 / 移除入口
    //
    // 持续输入：String case 每帧 InputText 返回 true → Push 一条
    // SetFieldValueCommand<std::string>，fieldKey="Name.name" + 同 entity →
    // CommandStack::Push 内 Merge 合并成单条 Undo 步骤，等价于 v0.1 期
    // RenameCommand 的 Merge 行为。
    ComponentSchemaBuilder<NC>("Name", "Name")
        .Field<&NC::name>("name", "##name")
        .Register();
}

void RegisterTransformComponentSchema()
{
    using TC = Orange::Engine::Scene::TransformComponent;
    // rotation 走 PropertyType::Quat 的 Euler-cache 路径（见
    // SchemaInspector.cpp Quat case 注释）。从 schema 视角看 rotation 仍
    // 是一个 quat 字段；Euler 缓存与 SetFieldValueCommand<glm::quat> 的
    // mOldValue / mNewValue 都用 quat marshal——Undo / Redo 是 quat 级
    // 回放，与 v0.1 期 hardcode 行为一致。
    ComponentSchemaBuilder<TC>("Transform", "Transform")
        .Field<&TC::position>("position", "Position")
            .DragSpeed(0.05f)
        .Field<&TC::rotation>("rotation", "Rotation (°)")
            .DragSpeed(0.5f)
        .Field<&TC::scale>("scale", "Scale")
            .DragSpeed(0.05f)
        .Removable()   // v0.1 期 hardcode 支持 right-click Remove；保留
        // 不 Addable —— 新 entity 创建路径默认挂 Transform，Inspector
        // "+Add Component" 列表不再出现 Transform 项（与 v0.1 行为一致）
        .Register();
}

void RegisterParticleEmitterComponentSchema()
{
    using PEC = Orange::Engine::Render::ParticleEmitterComponent;
    using PED = Orange::Engine::Render::ParticleEmitterDesc;

    // colorStart / colorEnd 是 vec4，但语义 RGB + intensity-alpha：alpha > 1
    // 让 bloom pass 自动拾取发光面。ColorEdit4 内置 alpha slider 默认 clamp
    // 到 [0, 1]（即便 HDR flag 也只放开 RGB），所以走 v0.1 hardcode 同款
    // 拆控件方案——单字段 vec4 → 两个虚拟 schema 字段：
    //   colorStartRGB (vec3, isColor)  → ColorEdit3
    //   colorStartAlpha (float, free)   → DragFloat 无上限
    // get / set lambda 必须 capture-less，转 PropertyDescriptor::GetFn / SetFn
    // 函数指针（FieldCustom 入口约定）。
    static const auto getColorStartRGB =
        +[](const void* c, void* out) {
            const auto& v = static_cast<const PEC*>(c)->desc.colorStart;
            *static_cast<glm::vec3*>(out) = glm::vec3{v.x, v.y, v.z};
        };
    static const auto setColorStartRGB =
        +[](void* c, const void* in) {
            auto& v = static_cast<PEC*>(c)->desc.colorStart;
            const auto& rgb = *static_cast<const glm::vec3*>(in);
            v.x = rgb.x; v.y = rgb.y; v.z = rgb.z;
        };
    static const auto getColorStartAlpha =
        +[](const void* c, void* out) {
            *static_cast<float*>(out) =
                static_cast<const PEC*>(c)->desc.colorStart.w;
        };
    static const auto setColorStartAlpha =
        +[](void* c, const void* in) {
            static_cast<PEC*>(c)->desc.colorStart.w = *static_cast<const float*>(in);
        };
    static const auto getColorEndRGB =
        +[](const void* c, void* out) {
            const auto& v = static_cast<const PEC*>(c)->desc.colorEnd;
            *static_cast<glm::vec3*>(out) = glm::vec3{v.x, v.y, v.z};
        };
    static const auto setColorEndRGB =
        +[](void* c, const void* in) {
            auto& v = static_cast<PEC*>(c)->desc.colorEnd;
            const auto& rgb = *static_cast<const glm::vec3*>(in);
            v.x = rgb.x; v.y = rgb.y; v.z = rgb.z;
        };
    static const auto getColorEndAlpha =
        +[](const void* c, void* out) {
            *static_cast<float*>(out) =
                static_cast<const PEC*>(c)->desc.colorEnd.w;
        };
    static const auto setColorEndAlpha =
        +[](void* c, const void* in) {
            static_cast<PEC*>(c)->desc.colorEnd.w = *static_cast<const float*>(in);
        };

    ComponentSchemaBuilder<PEC>("ParticleEmitter", "Particle Emitter")
        // 顶层 emitting 字段直接走 Field<>
        .Field<&PEC::emitting>("emitting", "Emitting")
        // 其余字段全在 PEC::desc 内 → FieldNested 双 NTTP
        .FieldNested<&PEC::desc, &PED::emissionRate>("desc.emissionRate", "Emission Rate (/s)")
            .DragSpeed(0.5f)
        .FieldNested<&PEC::desc, &PED::lifetimeMin>("desc.lifetimeMin", "Lifetime Min (s)")
            .GroupSeparator("Lifetime")
            .DragSpeed(0.01f)
        .FieldNested<&PEC::desc, &PED::lifetimeMax>("desc.lifetimeMax", "Lifetime Max (s)")
            .DragSpeed(0.01f)
        .FieldNested<&PEC::desc, &PED::spawnOffsetMin>("desc.spawnOffsetMin", "Offset Min")
            .GroupSeparator("Spawn Offset (entity local)")
            .DragSpeed(0.01f)
        .FieldNested<&PEC::desc, &PED::spawnOffsetMax>("desc.spawnOffsetMax", "Offset Max")
            .DragSpeed(0.01f)
        .FieldNested<&PEC::desc, &PED::initialVelocityMin>("desc.velocityMin", "Velocity Min")
            .GroupSeparator("Initial Velocity (m/s, worldspace)")
            .DragSpeed(0.05f)
        .FieldNested<&PEC::desc, &PED::initialVelocityMax>("desc.velocityMax", "Velocity Max")
            .DragSpeed(0.05f)
        .FieldNested<&PEC::desc, &PED::gravity>("desc.gravity", "Gravity (m/s²)")
            .GroupSeparator("Forces")
            .DragSpeed(0.05f)
        .FieldCustom<glm::vec3>("desc.colorStartRGB", "Color Start RGB",
                                getColorStartRGB, setColorStartRGB)
            .GroupSeparator("Color curve (linear lerp start→end by age01)")
            .Color()
        .FieldCustom<float>("desc.colorStartAlpha", "Color Start Alpha",
                            getColorStartAlpha, setColorStartAlpha)
            .DragSpeed(0.01f)
        .FieldCustom<glm::vec3>("desc.colorEndRGB", "Color End RGB",
                                getColorEndRGB, setColorEndRGB)
            .Color()
        .FieldCustom<float>("desc.colorEndAlpha", "Color End Alpha",
                            getColorEndAlpha, setColorEndAlpha)
            .DragSpeed(0.01f)
        .FieldNested<&PEC::desc, &PED::sizeStart>("desc.sizeStart", "Size Start")
            .GroupSeparator("Size curve")
            .DragSpeed(0.005f)
        .FieldNested<&PEC::desc, &PED::sizeEnd>("desc.sizeEnd", "Size End")
            .DragSpeed(0.005f)
        .FieldNested<&PEC::desc, &PED::maxParticles>("desc.maxParticles", "Max Particles")
            .GroupSeparator("Pool")
            .Range(0.0f, 65536.0f)
            .DragSpeed(1.0f)
        .Addable()
        .Removable()
        .Register();
}

void RegisterHierarchyComponentSchema()
{
    using HC = Orange::Engine::Scene::HierarchyComponent;
    // 4 个字段都是 Entity 引用——schema 走 PropertyType::EntityRef 的
    // 只读路径（"#<id>" / "(none)"），与 v0.1 期 hardcode 段视觉一致。
    //
    // 不 Addable / 不 Removable：父子关系由 Entity Tree 的 DnD reparent
    // 命令路径管理（避免 Inspector 与 DnD 两条修改路径竞争状态）。v0.1
    // 期 hardcode 段也没有 Add / Remove 入口。
    //
    // v0.1 期 hardcode 段最后一行 ImGui::TextDisabled "(edit by drag-drop
    // in Entity Tree)" 提示在本 commit 内**接受视觉降级**——schema 通
    // 用路径当前没有 "component-level helpText / footer" 机制，单为这
    // 一行新增 attribute 不值。后续 v0.3 IEditorInspectorPlugin 落地
    // 时可还原（plugin 在 schema 默认渲染外追加自定义 UI 是其典型用例）。
    ComponentSchemaBuilder<HC>("Hierarchy", "Hierarchy")
        .Field<&HC::parent>     ("parent",      "Parent")
        .Field<&HC::firstChild> ("firstChild",  "First child")
        .Field<&HC::prevSibling>("prevSibling", "Prev sibling")
        .Field<&HC::nextSibling>("nextSibling", "Next sibling")
        .Register();
}

}  // anonymous namespace

void RegisterBuiltinSchemas()
{
    // 注册顺序 = Inspector 内 component header 显示顺序：与 v0.1 期
    // DrawInspectorPanel 内显式调用顺序保持一致（Name → Transform →
    // Hierarchy → DirectionalLight → ... → RigidBody → ...）。
    RegisterNameComponentSchema();
    RegisterTransformComponentSchema();
    RegisterHierarchyComponentSchema();
    RegisterDirectionalLightSchema();
    RegisterRigidBodyComponentSchema();
    RegisterParticleEmitterComponentSchema();
    // 后续 commit 在此追加：Renderable / Collider / Animator
}

}  // namespace Orange::Editor::Schema
