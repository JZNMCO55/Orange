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

#include "../context/EditorAssetContext.h"
#include "ComponentSchemaRegistry.h"

#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/animation/IAnimator.h>
#include <orange/engine/asset/SoundAsset.h>
#include <orange/engine/asset/TextureAsset.h>
#include <orange/engine/audio/AudioSourceComponent.h>
#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/RigidBodyComponent.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/EnvironmentComponent.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/ParticleEmitterComponent.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/TransformComponent.h>

#include <string>
#include <variant>

namespace Orange::Editor::Schema
{

namespace
{

void RegisterDirectionalLightSchema()
{
    using DL = Orange::Engine::Render::DirectionalLight;
    // 方向字段已废 —— 改由 entity 的 TransformComponent.rotation 派生
    // （Pipeline 内 ComputeDirectionalLightWorldDir(rotation)）。Inspector
    // 不再展示 Direction 控件；用户旋转 entity（Rotate gizmo / Transform
    // schema 的 rotation 编辑）即可改光向，与 Unity / Unreal / Godot 同款
    // 工业惯例（几何状态由 Transform 唯一拥有）。
    ComponentSchemaBuilder<DL>("DirectionalLight", "Directional Light")
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

// PointLight schema。位置由 Transform 派生（与 DirectionalLight 同款），
// Inspector 仅暴露 color / intensity / range / castsShadow 四字段。
// castsShadow 当前 Pipeline 忽略（omnidirectional shadow 是 long-term
// roadmap 量级），tooltip 显式说明留作 forward-compat。
void RegisterPointLightSchema()
{
    using PL = Orange::Engine::Render::PointLight;
    ComponentSchemaBuilder<PL>("PointLight", "Point Light")
        .Field<&PL::color>("color", "Color")
            .Color()
        .Field<&PL::intensity>("intensity", "Intensity")
            .Range(0.0f, 1000.0f)
            .DragSpeed(0.05f)
        .Field<&PL::range>("range", "Range (m)")
            .Range(0.01f, 1000.0f)
            .DragSpeed(0.1f)
            .Tooltip("光照影响距离上限（米）。超出此距离贡献被 smoothstep 截断到 0。\n"
                     "Pipeline 用 range 做 culling + shader 内 distance fade。\n"
                     "典型室内点光 5–10m，路灯 30–100m。")
        .Field<&PL::castsShadow>("castsShadow", "Casts Shadow")
            .Tooltip("保留字段。当前 Pipeline 忽略 PointLight 的投影标记。\n"
                     "Omnidirectional cubemap shadow 是 Phase 10 量级，留待后续 milestone。")
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
        .Addable()     // c10 修正：v0.1 期 InspectorPanel +Add Component popup
                       // 实际包含 Transform 项（移除后能重新加回，行为对称于
                       // Removable）；c5 注册时把 Transform 标为非 Addable 并
                       // 误写注释"与 v0.1 行为一致"，本期一并修正
        .Removable()   // v0.1 期 hardcode 支持 right-click Remove；保留
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

// v0.9.5 c3：file-scope gpAssetContext 单例下架。AssetRef get/set 改走
// PropertyDescriptor::AssetRefGetFn / AssetRefSetFn 新签名，由 SchemaInspector
// 在 dispatch + 命令栈 replay 两个路径显式传入 `const EditorAssetContext&`。
// 同步删除 SetEditorAssetContextForSchema 外部 setter；main.cpp 启动期注入
// 调用一并清掉（见 v0.9.5 c3 同步改动）。
//
// 设计选型详见 docs/decisions/ADR-004：方案 B（AssetRefAccessor 专用槽位）
// 而非方案 A（扩 GetFn/SetFn 全字段加 ctx）；未来若有第二类 ctx-hungry 字段
// （ScriptRef / LocaleRef ...）再考虑升级到方案 A。

void RegisterRenderableComponentSchema()
{
    using RC = Orange::Engine::Render::RenderableComponent;

    // c10 落地 Renderable 自定义 add 路径：v0.1 期 +Add Component 在挂 Renderable
    // 时**预绑** cubeMesh + defaultMaterial（让用户立刻在 viewport 看到一个白色
    // 立方体，而不是 mesh=Invalid / material=nullptr 的"隐形"挂法）。c7 schema
    // 化时为了避免 hardcode 路径暂未挂 Addable；c10 通过 .AddableWith() 还原。
    //
    // 自定义 add lambda 必须 capture-less 才能转 ComponentSchema::AddFn 函数指针；
    // 因此 mesh / material 引用走 host.assets 路径在 lambda 内**运行时**取，而
    // 非 lambda 创建期 capture。
    static const auto renderableAddWithPreset =
        +[](EditorHost& host, Orange::Engine::Entity e)
        {
            if (host.scene.pWorld == nullptr) { return; }
            RC rc{};
            rc.mesh             = host.assets.cubeMeshHandle;
            // 默认绑 PBR 材质（Cocos Creator builtin-standard 同款手感）。
            // pPbrMaterial 缺席时退化到 default.material（textured 模板棋盘格
            // dev-checker，保证物体永远可见）。textured 不再做"默认主战场"的
            // 角色——它是 dev-checker，地板 / 墙面这类需要程序图案的场景仍可
            // 显式选用。
            rc.materialInstance = host.assets.pPbrMaterial
                ? host.assets.pPbrMaterial.get()
                : host.assets.pDefaultRenderableMaterial.get();
            host.scene.pWorld->AddComponent<RC>(e, rc);
        };

    // mesh / materialInstance 字段走 PropertyType::AssetRef 注册。AssetHandle ↔
    // path 双向转换由 ctx.pAssets / ctx.namedMaterialInstances 承担。
    static const auto meshGet = +[](const void* c,
                                    const EditorAssetContext& ctx,
                                    void* out) {
        auto* r = static_cast<const RC*>(c);
        auto* sOut = static_cast<std::string*>(out);
        if (ctx.pAssets == nullptr || !r->mesh.IsValid()) {
            sOut->clear();
            return;
        }
        *sOut = std::string{ctx.pAssets->PathOf<
            ::Orange::Engine::Asset::MeshAsset>(r->mesh)};
    };
    static const auto meshSet = +[](void* c,
                                    const EditorAssetContext& ctx,
                                    const void* in) {
        auto* r = static_cast<RC*>(c);
        const auto& path = *static_cast<const std::string*>(in);
        if (ctx.pAssets == nullptr) { return; }
        if (path.empty()) {
            r->mesh = {};
            return;
        }
        auto lr = ctx.pAssets->Load<
            ::Orange::Engine::Asset::MeshAsset>(path);
        if (lr.IsOk()) { r->mesh = lr.Value(); }
    };

    static const auto materialGet = +[](const void* c,
                                        const EditorAssetContext& ctx,
                                        void* out) {
        auto* r = static_cast<const RC*>(c);
        auto* sOut = static_cast<std::string*>(out);
        sOut->clear();
        if (r->materialInstance == nullptr) { return; }
        // O(N) 反查 path → ptr 表。namedMaterialInstances 当前规模 < 10，
        // 即使每帧调用一次也可忽略。后续若 schema 内有大量 material 字段
        // 可加 ptr → path 反向缓存。
        for (const auto& [path, ptr] : ctx.namedMaterialInstances)
        {
            if (ptr == r->materialInstance) { *sOut = path; return; }
        }
    };
    static const auto materialSet = +[](void* c,
                                        const EditorAssetContext& ctx,
                                        const void* in) {
        auto* r = static_cast<RC*>(c);
        const auto& path = *static_cast<const std::string*>(in);
        if (path.empty()) {
            r->materialInstance = nullptr;
            return;
        }
        auto it = ctx.namedMaterialInstances.find(path);
        if (it != ctx.namedMaterialInstances.end())
        {
            r->materialInstance = it->second;
        }
    };

    ComponentSchemaBuilder<RC>("Renderable", "Renderable")
        .FieldAssetRef("mesh", "Mesh", AssetKind::Mesh, meshGet, meshSet)
        .FieldAssetRef("materialInstance", "Material", AssetKind::Material,
                       materialGet, materialSet)
        .Field<&RC::visible>("visible", "Visible")
        .Field<&RC::castsShadow>("castsShadow", "Casts Shadow")
            .Tooltip("本物体是否参与投射阴影（per-object 开关）。\n"
                     "关掉对应 \"几何不投影但仍接收阴影\"（典型用例：透明 UI / 装饰物 /\n"
                     "近景特效）。与 DirectionalLight 的同名 flag 是 AND 关系：两个都\n"
                     "必须为 true 才会真投影。")
        .AddableWith(renderableAddWithPreset)
        .Removable()
        .Register();
}

// EnvironmentComponent 的 Inspector schema。
//
// 三个字段：cubemap（HDR equirect 资产引用）+ tint（线性 RGB 色调乘子）+
// intensity（标量强度）。schema 注册与 RenderableComponent.mesh 同款 AssetRef
// 路径——通过 gpAssetRegistry 反查 path 字符串，控件层把 string 显示成短名
// （ImGui DnD 接收 + 浏览器写入由后续编辑器 milestone 处理）。
//
// 视觉对位：Material 五通道 schema + DirectionalLight schema，同节奏的
// PBR-IBL Inspector 三件套。Environment 浏览器 / sky preview 留给独立的
// 编辑器伴随 milestone（见 docs/pbr-ibl-milestone.md §RISK-6 决议）。
//
// 定义位置：放在 RegisterRenderableComponentSchema 之后，是因为本函数依赖
// gpAssetRegistry（在 Renderable 段之前的 anonymous namespace 内声明）；
// 实际 Inspector 显示顺序由 RegisterBuiltinSchemas() 内的调用次序决定，
// 与定义顺序解耦。
void RegisterEnvironmentComponentSchema()
{
    using EC = Orange::Engine::Render::EnvironmentComponent;

    // cubemap AssetRef 字段的 get/set —— 与 Renderable.mesh 同款实现，仅
    // 类型从 MeshAsset 换成 TextureAsset。ctx.pAssets 由 SchemaInspector
    // dispatch / 命令 replay 路径显式传入，不再依赖 file-scope 静态。
    static const auto cubemapGet = +[](const void* c,
                                       const EditorAssetContext& ctx,
                                       void* out) {
        auto* e = static_cast<const EC*>(c);
        auto* sOut = static_cast<std::string*>(out);
        if (ctx.pAssets == nullptr || !e->cubemap.IsValid()) {
            sOut->clear();
            return;
        }
        *sOut = std::string{ctx.pAssets->PathOf<
            ::Orange::Engine::Asset::TextureAsset>(e->cubemap)};
    };
    static const auto cubemapSet = +[](void* c,
                                       const EditorAssetContext& ctx,
                                       const void* in) {
        auto* e = static_cast<EC*>(c);
        const auto& path = *static_cast<const std::string*>(in);
        if (ctx.pAssets == nullptr) { return; }
        if (path.empty()) {
            e->cubemap = {};
            return;
        }
        auto lr = ctx.pAssets->Load<
            ::Orange::Engine::Asset::TextureAsset>(path);
        if (lr.IsOk()) { e->cubemap = lr.Value(); }
    };

    ComponentSchemaBuilder<EC>("Environment", "Environment")
        .FieldAssetRef("cubemap", "Cubemap (HDR)", AssetKind::Texture,
                       cubemapGet, cubemapSet)
        .Field<&EC::tint>("tint", "Tint")
            .Color()
            .Tooltip("线性 RGB 色调乘子，对 irradiance + prefiltered IBL 一并生效。\n"
                     "(1,1,1) = 无修正；(0,0,0) = 关掉 IBL 贡献。\n"
                     "在 host 端预乘 intensity，shader 内一次相乘，无需重烘焙。")
        .Field<&EC::intensity>("intensity", "Intensity")
            .Range(0.0f, 16.0f)
            .DragSpeed(0.05f)
            .Tooltip("标量强度乘子。1 = 烘焙原始亮度；> 1 = 加亮；< 1 = 减亮（夜景 / 阴天）。\n"
                     "调本字段不会触发重烘焙——亮度微调由 LightUbo 的 iblFactor 即时承担。")
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

void RegisterColliderComponentSchema()
{
    using CC = Orange::Engine::Physics::ColliderComponent;
    using Orange::Engine::Physics::CircleDesc;
    using Orange::Engine::Physics::BoxDesc;
    using Orange::Engine::Physics::PolygonDesc;
    using Orange::Engine::Physics::EdgeChainDesc;

    // shape 是 std::variant<CircleDesc, BoxDesc, PolygonDesc, EdgeChainDesc>。
    // v0.1 hardcode 内 deliberately not implemented "shape 类型切换控件"
    // （切换 alternative 会重置数据，误操作风险大）；c8 严格 1:1 复刻，
    // **不**引入 shape 切换入口。
    //
    // 4 个 shape 段通过 VisibleIf(holds_alternative<X>) 互斥显示：
    //   Circle    → Radius + Center
    //   Box       → Half Extents + Center
    //   Polygon   → 仅 GroupSeparator 占位（顶点编辑 — later task）
    //   EdgeChain → 同上
    //
    // 视觉降级（vs v0.1）：
    //   * Polygon 不再显示动态 vertex count（v0.1 `(%u verts)`）
    //   * EdgeChain 不再显示动态 vertex count + loop 状态
    // 两条都属 informational 显示，v0.1 也不可编辑——真正能编辑时（后续
    // collider 专用 UI / v0.4 Gizmo）再回归。
    //
    // FieldCustom getter / setter 必须 capture-less 才能转 PropertyDescriptor
    // 的函数指针类型。get/set 内对错配 alternative 走 std::get_if 守护
    // ——虽然 visibleIf 已在 SchemaInspector 侧把错配 alternative 整段跳
    // 掉、getter 理论上不会被调到错配状态，但保留 defensive guard 让
    // setter 安全（防御命令栈 Undo / Redo 在 shape 已被外部改写后回放命令的边界情况）。

    // ---- Circle 字段 ----
    static const auto getCircleRadius = +[](const void* c, void* out)
    {
        const auto& shape = static_cast<const CC*>(c)->shape;
        *static_cast<float*>(out) = std::holds_alternative<CircleDesc>(shape)
            ? std::get<CircleDesc>(shape).radius
            : 0.0f;
    };
    static const auto setCircleRadius = +[](void* c, const void* in)
    {
        auto& shape = static_cast<CC*>(c)->shape;
        if (auto* s = std::get_if<CircleDesc>(&shape))
        {
            s->radius = *static_cast<const float*>(in);
        }
    };
    static const auto getCircleCenter = +[](const void* c, void* out)
    {
        const auto& shape = static_cast<const CC*>(c)->shape;
        *static_cast<glm::vec2*>(out) = std::holds_alternative<CircleDesc>(shape)
            ? std::get<CircleDesc>(shape).center
            : glm::vec2{0.0f};
    };
    static const auto setCircleCenter = +[](void* c, const void* in)
    {
        auto& shape = static_cast<CC*>(c)->shape;
        if (auto* s = std::get_if<CircleDesc>(&shape))
        {
            s->center = *static_cast<const glm::vec2*>(in);
        }
    };

    // ---- Box 字段 ----
    static const auto getBoxHalfExtents = +[](const void* c, void* out)
    {
        const auto& shape = static_cast<const CC*>(c)->shape;
        *static_cast<glm::vec2*>(out) = std::holds_alternative<BoxDesc>(shape)
            ? std::get<BoxDesc>(shape).halfExtents
            : glm::vec2{0.0f};
    };
    static const auto setBoxHalfExtents = +[](void* c, const void* in)
    {
        auto& shape = static_cast<CC*>(c)->shape;
        if (auto* s = std::get_if<BoxDesc>(&shape))
        {
            s->halfExtents = *static_cast<const glm::vec2*>(in);
        }
    };
    static const auto getBoxCenter = +[](const void* c, void* out)
    {
        const auto& shape = static_cast<const CC*>(c)->shape;
        *static_cast<glm::vec2*>(out) = std::holds_alternative<BoxDesc>(shape)
            ? std::get<BoxDesc>(shape).center
            : glm::vec2{0.0f};
    };
    static const auto setBoxCenter = +[](void* c, const void* in)
    {
        auto& shape = static_cast<CC*>(c)->shape;
        if (auto* s = std::get_if<BoxDesc>(&shape))
        {
            s->center = *static_cast<const glm::vec2*>(in);
        }
    };

    // ---- 4 个 shape 的 visibleIf 谓词 ----
    static const auto isCircle    = +[](const void* c) -> bool
        { return std::holds_alternative<CircleDesc>   (static_cast<const CC*>(c)->shape); };
    static const auto isBox       = +[](const void* c) -> bool
        { return std::holds_alternative<BoxDesc>      (static_cast<const CC*>(c)->shape); };
    static const auto isPolygon   = +[](const void* c) -> bool
        { return std::holds_alternative<PolygonDesc>  (static_cast<const CC*>(c)->shape); };
    static const auto isEdgeChain = +[](const void* c) -> bool
        { return std::holds_alternative<EdgeChainDesc>(static_cast<const CC*>(c)->shape); };

    // 字段顺序：通用物理字段（density / friction / restitution / isSensor）
    // 在前，shape 段在后。v0.1 hardcode 内顺序相反（shape 在前），但 schema
    // 是线性顺序——Polygon / EdgeChain 的"零字段段"放在中间会导致用户感觉
    // 通用字段"被挤"到段末尾。颠倒顺序让 shape 段总在 component 段末尾，
    // 视觉更稳定。
    ComponentSchemaBuilder<CC>("Collider", "Collider")
        .Field<&CC::density>("density", "Density")
            .DragSpeed(0.01f)
        .Field<&CC::friction>("friction", "Friction")
            .Range(0.0f, 1.0f).DragSpeed(0.01f)
        .Field<&CC::restitution>("restitution", "Restitution")
            .Range(0.0f, 1.0f).DragSpeed(0.01f)
        .Field<&CC::isSensor>("isSensor", "Is Sensor")
        // ---- Circle ----
        .FieldCustom<float>("circle.radius", "Radius",
                            getCircleRadius, setCircleRadius)
            .GroupSeparator("Shape: Circle")
            .VisibleIf(isCircle)
            .DragSpeed(0.01f)
        .FieldCustom<glm::vec2>("circle.center", "Center",
                                getCircleCenter, setCircleCenter)
            .VisibleIf(isCircle)
            .DragSpeed(0.01f)
        // ---- Box ----
        .FieldCustom<glm::vec2>("box.halfExtents", "Half Extents",
                                getBoxHalfExtents, setBoxHalfExtents)
            .GroupSeparator("Shape: Box")
            .VisibleIf(isBox)
            .DragSpeed(0.01f)
        .FieldCustom<glm::vec2>("box.center", "Center",
                                getBoxCenter, setBoxCenter)
            .VisibleIf(isBox)
            .DragSpeed(0.01f)
        // ---- Polygon / EdgeChain：零字段 header-only ----
        .Group("Shape: Polygon (vertex editing — later)",   isPolygon)
        .Group("Shape: EdgeChain (vertex editing — later)", isEdgeChain)
        .Addable()
        .Removable()
        .Register();
}

void RegisterAnimatorComponentSchema()
{
    using AC = Orange::Engine::Animation::AnimatorComponent;

    // AnimatorComponent.animator 是 std::unique_ptr<IAnimator>——IAnimator 是
    // 抽象基类，构造 AnimatorComponent 需要具体子类实例（SkeletalAnimator /
    // ProceduralAnimator / 游戏自注册的 backend）。schema 当前没有"构造抽象
    // 子类"的入口，所以本组件 **不 Addable**——与 v0.1 期 +Add Component
    // popup 内显式跳过 Animator 的行为一致。
    //
    // 也不 Removable —— v0.1 期 hardcode 段用裸 CollapsingHeader，无 Remove
    // 入口；本期保持一致。后续 v0.7 Animation 子模式可能引入 backend 切换
    // 路径，那时再决定 schema 是否 Addable / Removable。
    //
    // 字段：仅一个 backend 名只读 String。v0.1 hardcode 显示 `Animator
    // (runtime) : <ptr>` 原始指针 + TextDisabled "(animator backend editing
    // — later task)" 占位；c9 升级为更可读的 backend name 字符串（如
    // "skeletal_dragonbones" / "procedural"），通过 c9 同步引入的 ReadOnly
    // attribute 显示。指针地址显示与 later-task 占位文本本期接受视觉变更
    // ——backend name 信息量严格高于指针地址。
    //
    // getter 捕获 unique_ptr 可能为 null 的情况——AnimatorComponent 默认
    // 构造时 animator 是空 unique_ptr，理论上不应进入 Inspector 段（v0.1
    // hardcode 也没 guard，进了就显示 nullptr 指针），但 schema 路径保留
    // defensive guard：null 显示 "(no backend)"，与"未挂 backend"语义一致。
    static const auto getBackendName = +[](const void* c, void* out)
    {
        const auto* ac = static_cast<const AC*>(c);
        if (ac->animator)
        {
            *static_cast<std::string*>(out) =
                std::string(ac->animator->BackendName());
        }
        else
        {
            *static_cast<std::string*>(out) = "(no backend)";
        }
    };
    // setter 是 no-op —— readOnly 路径下 SchemaInspector 不会调用 set，留
    // nullptr 也行（c9 同步放宽了 set==nullptr 早退检查），但保留显式 no-op
    // 让 FieldCustom 调用站点更对称（caller 一眼能看出"这是 read-only"）。
    static const auto setBackendNameNoOp = +[](void*, const void*) { };

    ComponentSchemaBuilder<AC>("Animator", "Animator")
        .FieldCustom<std::string>("backend", "Backend",
                                  getBackendName, setBackendNameNoOp)
            .ReadOnly()
        // 不 Addable / 不 Removable —— 见函数顶注释
        .Register();
}

// v0.4 c5：注册 Camera schema 让 CameraFrustumGizmoPlugin 的 dispatch 路径
// 走通（plugin 通过 schema.has + schema.get 找 component）。
//
// 当前**不**暴露 view / projection 字段——schema 体系没有 Mat4 PropertyType，
// 且 Camera 的 view / projection 是矩阵形态（Camera::Perspective / lookAt
// 生成），直接 Inspector 编辑 4×4 矩阵不友好。GAP-2026-05-15 落地后 ECS
// Camera 数据不再被 ApplyEditorCameraToWorld 覆写，frustum gizmo 已能反
// 映用户在构造期 / Inspector 内设置的 projection；未来引入 CameraDesc
// {fov, aspect, near, far} 拆分后再补 Inspector 字段。Inspector 段当前
// 仍空（只 collapsing header），不影响 frustum gizmo 视觉。
//
// 不 Addable / 不 Removable —— 同 Animator：Camera 的初始化路径需要 fov /
// aspect 等参数，不适合"+Add Component 走默认构造"路径。
void RegisterCameraComponentSchema()
{
    using Cam = Orange::Engine::Render::Camera;
    ComponentSchemaBuilder<Cam>("Camera", "Camera")
        // schema 注册不需要 .Field()——仅 typeName + has + get 就够 plugin
        // dispatch 路径用。Inspector 段空，没字段。
        .Register();
}

// AudioSourceComponent 的 Inspector schema。
//
// 五字段：sound（.wav / .ogg 资源引用）+ playOnAwake / loop / volume / pitch。
// sound AssetRef 走 ctx.pAssets 路径（与 Renderable.mesh / Environment.cubemap
// 同款 PathOf / Load 双向转换），其余四字段是 PureData 标量。
//
// 试播 / 停止按钮**不**通过本 schema 暴露 —— 走 IEditorInspectorPlugin
// ParseEnd 钩子（AudioSourceInspectorPlugin），让"播放控制"与"参数编辑"
// 切分清晰（同 Unity AudioSource：参数走 Inspector，Play/Stop 走 inspector
// 顶部的工具栏按钮 / 独立小窗）。
void RegisterAudioSourceComponentSchema()
{
    using AS = Orange::Engine::Audio::AudioSourceComponent;

    static const auto soundGet = +[](const void* c,
                                     const EditorAssetContext& ctx,
                                     void* out) {
        auto* a    = static_cast<const AS*>(c);
        auto* sOut = static_cast<std::string*>(out);
        if (ctx.pAssets == nullptr || !a->sound.IsValid()) {
            sOut->clear();
            return;
        }
        *sOut = std::string{ctx.pAssets->PathOf<
            ::Orange::Engine::Asset::SoundAsset>(a->sound)};
    };
    static const auto soundSet = +[](void* c,
                                     const EditorAssetContext& ctx,
                                     const void* in) {
        auto* a = static_cast<AS*>(c);
        const auto& path = *static_cast<const std::string*>(in);
        if (ctx.pAssets == nullptr) { return; }
        if (path.empty()) {
            a->sound = {};
            return;
        }
        auto lr = ctx.pAssets->Load<
            ::Orange::Engine::Asset::SoundAsset>(path);
        if (lr.IsOk()) { a->sound = lr.Value(); }
    };

    ComponentSchemaBuilder<AS>("AudioSource", "Audio Source")
        .FieldAssetRef("sound", "Sound", AssetKind::Sound, soundGet, soundSet)
        .Field<&AS::playOnAwake>("playOnAwake", "Play On Awake")
            .Tooltip("Play Mode 进入瞬间是否自动 Start。\n"
                     "关闭时由游戏侧脚本 / 编辑器 Inspector 试播按钮触发。")
        .Field<&AS::loop>("loop", "Loop")
            .Tooltip("是否循环播放。loop=true 时声音不会自然结束。")
        .Field<&AS::volume>("volume", "Volume")
            .Range(0.0f, 2.0f)
            .DragSpeed(0.01f)
            .Tooltip("音量乘子（0..1+）。0 = 静音；1 = 原始音量；> 1 可能 clip。")
        .Field<&AS::pitch>("pitch", "Pitch")
            .Range(0.25f, 4.0f)
            .DragSpeed(0.01f)
            .Tooltip("音高乘子（1 = 原速；0.5 = 半速半音高；2 = 双倍）。\n"
                     "miniaudio 通过重采样实现，pitch != 1 时 CPU 开销略升。")
        .Addable()
        .Removable()
        .Register();
}

}  // anonymous namespace

void RegisterBuiltinSchemas()
{
    // 注册顺序 = Inspector 内 component header 显示顺序：与 v0.1 期
    // DrawInspectorPanel 内显式调用顺序保持一致（Name → Transform →
    // Hierarchy → DirectionalLight → ... → RigidBody → Collider →
    // ParticleEmitter → Animator）。
    RegisterNameComponentSchema();
    RegisterTransformComponentSchema();
    RegisterHierarchyComponentSchema();
    RegisterDirectionalLightSchema();
    RegisterPointLightSchema();
    RegisterEnvironmentComponentSchema();
    RegisterRenderableComponentSchema();
    RegisterRigidBodyComponentSchema();
    RegisterColliderComponentSchema();
    RegisterParticleEmitterComponentSchema();
    RegisterAudioSourceComponentSchema();
    RegisterAnimatorComponentSchema();
    RegisterCameraComponentSchema();
    // 所有内置组件 schema 已全数迁完。后续 commit（c10）改 Add Component 菜
    // 单走 schema 注册表枚举驱动；c11 / c12 引入 IEditor*Plugin 抽象。
}

}  // namespace Orange::Editor::Schema
