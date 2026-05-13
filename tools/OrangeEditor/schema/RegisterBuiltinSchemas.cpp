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

}  // anonymous namespace

void RegisterBuiltinSchemas()
{
    RegisterDirectionalLightSchema();
    RegisterRigidBodyComponentSchema();
    // 后续 commit 在此追加：Transform / Name / Hierarchy / Renderable /
    //                       Collider / ParticleEmitter / Animator
}

}  // namespace Orange::Editor::Schema
