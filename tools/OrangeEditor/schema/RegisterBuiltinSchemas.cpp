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

#include <orange/engine/animation/AnimationClip.h>
#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/animation/ClipAnimator.h>
#include <orange/engine/animation/IAnimator.h>
#include <orange/engine/asset/SoundAsset.h>
#include <orange/engine/asset/TextureAsset.h>
#include <orange/engine/audio/AudioSourceComponent.h>
#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/RigidBodyComponent.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/EnvironmentComponent.h>
#include <orange/engine/render/PostProcessComponent.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/ParticleEmitterComponent.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/render/SubMeshMaterialsComponent.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/script/ScriptComponent.h>

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
                .Helper("方向由 entity 的 Transform.rotation 派生 —— 在上方 Transform 段旋转 entity 即可改光向。\n"
                        "场景中只有第一个 DirectionalLight 参与光照与阴影；其余 DirLight 会被忽略。")
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
                .Tooltip("勾选后 Pipeline 为该点光烘 6 面 cubemap omnidirectional 阴影。\n"
                         "多 shadow caster 架构落地后生效。")
                // —— 可见光晕（GAP-2026-05-11 G3）——
                .Field<&PL::haloEnabled>("haloEnabled", "Halo Enabled")
                .Tooltip("勾选后 Pipeline 在 entity.Transform.position 画一个 emissive\n"
                         "sphere（半径 haloRadius、emissive 颜色 = color * intensity *\n"
                         "haloIntensity），让用户在 viewport 看到光源本身（典型 Ori-like\n"
                         "发光主角 / 灯泡 prop 用法）。BloomPass 自然散光产生 glow。\n"
                         "与 castsShadow 正交——halo 只影响视觉，不参与光照计算。")
                .Field<&PL::haloRadius>("haloRadius", "Halo Radius (m)")
                .Range(0.01f, 5.0f)
                .DragSpeed(0.01f)
                .Tooltip("halo sphere 的世界半径（米）。典型 0.1–0.3m 灯泡级；过大\n"
                         "会让 halo 吞掉真正的 mesh，过小会被 bloom 完全糊掉看不到形状。")
                .Field<&PL::haloIntensity>("haloIntensity", "Halo Intensity")
                .Range(0.0f, 10.0f)
                .DragSpeed(0.02f)
                .Tooltip("halo emissive 强度的额外乘子（与 PointLight.intensity 独立）。\n"
                         "1.0 = halo 视觉强度直接跟随 light intensity；>1 加强 halo glow；\n"
                         "<1 减弱 halo。最终 emissive 颜色 = color * intensity * haloIntensity。")
                .Addable()
                .Removable()
                .Register();
        }

        // SpotLight schema。位置 + 方向均由 Transform 派生（与 DirectionalLight /
        // PointLight 同款）；Inspector 暴露 color / intensity / range / 内外锥半角
        //（弧度，与 RigidBody initialAngle 同款 "(rad)" 字段惯例）/ castsShadow。
        void RegisterSpotLightSchema()
        {
            using SL = Orange::Engine::Render::SpotLight;
            ComponentSchemaBuilder<SL>("SpotLight", "Spot Light")
                .Helper("位置由 Transform.position、锥光方向由 Transform.rotation 派生 —— 在上方 Transform 段移动 / 旋转 entity 即改光位与朝向。\n"
                        "identity rotation 表示锥光向下（-Y），与 Directional / Point 同款约定。")
                .Field<&SL::color>("color", "Color")
                .Color()
                .Field<&SL::intensity>("intensity", "Intensity")
                .Range(0.0f, 1000.0f)
                .DragSpeed(0.05f)
                .Field<&SL::range>("range", "Range (m)")
                .Range(0.01f, 1000.0f)
                .DragSpeed(0.1f)
                .Tooltip("光照影响距离上限（米）。超出此距离贡献 smoothstep 截断到 0。\n"
                         "也是透视阴影 light proj 的 zFar。")
                .Field<&SL::innerConeAngle>("innerConeAngle", "Inner Cone (rad)")
                .Range(0.01f, 1.5f)
                .DragSpeed(0.01f)
                .Tooltip("内锥半角（弧度，从中心轴量起）。≤ 此角全亮。")
                .Field<&SL::outerConeAngle>("outerConeAngle", "Outer Cone (rad)")
                .Range(0.01f, 1.5f)
                .DragSpeed(0.01f)
                .Tooltip("外锥半角（弧度）。≥ 此角全暗；内外之间 smoothstep 软过渡。\n"
                         "透视阴影 light proj 的 fov = 2 × 外锥半角。建议 ≥ 内锥半角。")
                .Field<&SL::castsShadow>("castsShadow", "Casts Shadow")
                .Tooltip("勾选后 Pipeline 为该聚光烘一张 perspective shadow map。\n"
                         "多 shadow caster 架构落地后生效。")
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
            static_assert(static_cast<int>(BT::Static) == 0, "BodyType enum drift");
            static_assert(static_cast<int>(BT::Kinematic) == 1, "BodyType enum drift");
            static_assert(static_cast<int>(BT::Dynamic) == 2, "BodyType enum drift");

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
                .Addable()   // c10 修正：v0.1 期 InspectorPanel +Add Component popup
                             // 实际包含 Transform 项（移除后能重新加回，行为对称于
                             // Removable）；c5 注册时把 Transform 标为非 Addable 并
                             // 误写注释"与 v0.1 行为一致"，本期一并修正
                .Removable() // v0.1 期 hardcode 支持 right-click Remove；保留
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
                +[](const void* c, void* out)
            {
                const auto& v                 = static_cast<const PEC*>(c)->desc.colorStart;
                *static_cast<glm::vec3*>(out) = glm::vec3{v.x, v.y, v.z};
            };
            static const auto setColorStartRGB =
                +[](void* c, const void* in)
            {
                auto&       v   = static_cast<PEC*>(c)->desc.colorStart;
                const auto& rgb = *static_cast<const glm::vec3*>(in);
                v.x             = rgb.x;
                v.y             = rgb.y;
                v.z             = rgb.z;
            };
            static const auto getColorStartAlpha =
                +[](const void* c, void* out)
            {
                *static_cast<float*>(out) =
                    static_cast<const PEC*>(c)->desc.colorStart.w;
            };
            static const auto setColorStartAlpha =
                +[](void* c, const void* in)
            {
                static_cast<PEC*>(c)->desc.colorStart.w = *static_cast<const float*>(in);
            };
            static const auto getColorEndRGB =
                +[](const void* c, void* out)
            {
                const auto& v                 = static_cast<const PEC*>(c)->desc.colorEnd;
                *static_cast<glm::vec3*>(out) = glm::vec3{v.x, v.y, v.z};
            };
            static const auto setColorEndRGB =
                +[](void* c, const void* in)
            {
                auto&       v   = static_cast<PEC*>(c)->desc.colorEnd;
                const auto& rgb = *static_cast<const glm::vec3*>(in);
                v.x             = rgb.x;
                v.y             = rgb.y;
                v.z             = rgb.z;
            };
            static const auto getColorEndAlpha =
                +[](const void* c, void* out)
            {
                *static_cast<float*>(out) =
                    static_cast<const PEC*>(c)->desc.colorEnd.w;
            };
            static const auto setColorEndAlpha =
                +[](void* c, const void* in)
            {
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

            // 配套组件 SubMeshMaterialsComponent（单 mesh 多 material 的 slot → material
            // 映射）的 Inspector schema 见下方 RegisterSubMeshMaterialsComponentSchema()
            // ——它唯一的字段是 std::vector<MaterialInstance*> 数组，由新增的
            // PropertyType::AssetRefArray + Builder::FieldAssetRefArray 表达（之前缺
            // "AssetRef 数组"字段类型而留为后续；本次补齐）。注册顺序紧接 Renderable，
            // 让多材质实体的 slot 面板紧贴 Renderable 段显示。
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
                if (host.scene.pWorld == nullptr)
                {
                    return;
                }
                RC rc{};
                rc.mesh = host.assets.cubeMeshHandle;
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
            static const auto meshGet = +[](const void*               c,
                                            const EditorAssetContext& ctx,
                                            void*                     out)
            {
                auto* r    = static_cast<const RC*>(c);
                auto* sOut = static_cast<std::string*>(out);
                if (ctx.pAssets == nullptr || !r->mesh.IsValid())
                {
                    sOut->clear();
                    return;
                }
                *sOut = std::string{ctx.pAssets->PathOf<
                    ::Orange::Engine::Asset::MeshAsset>(r->mesh)};
            };
            static const auto meshSet = +[](void*                     c,
                                            const EditorAssetContext& ctx,
                                            const void*               in)
            {
                auto*       r    = static_cast<RC*>(c);
                const auto& path = *static_cast<const std::string*>(in);
                if (ctx.pAssets == nullptr)
                {
                    return;
                }
                if (path.empty())
                {
                    r->mesh = {};
                    return;
                }
                auto lr = ctx.pAssets->Load<
                    ::Orange::Engine::Asset::MeshAsset>(path);
                if (lr.IsOk())
                {
                    r->mesh = lr.Value();
                }
            };

            static const auto materialGet = +[](const void*               c,
                                                const EditorAssetContext& ctx,
                                                void*                     out)
            {
                auto* r    = static_cast<const RC*>(c);
                auto* sOut = static_cast<std::string*>(out);
                sOut->clear();
                if (r->materialInstance == nullptr)
                {
                    return;
                }
                // O(N) 反查 path → ptr 表。namedMaterialInstances 当前规模 < 10，
                // 即使每帧调用一次也可忽略。后续若 schema 内有大量 material 字段
                // 可加 ptr → path 反向缓存。
                for (const auto& [path, ptr] : ctx.namedMaterialInstances)
                {
                    if (ptr == r->materialInstance)
                    {
                        *sOut = path;
                        return;
                    }
                }
            };
            static const auto materialSet = +[](void*                     c,
                                                const EditorAssetContext& ctx,
                                                const void*               in)
            {
                auto*       r    = static_cast<RC*>(c);
                const auto& path = *static_cast<const std::string*>(in);
                if (path.empty())
                {
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

        // SubMeshMaterialsComponent 的 Inspector schema —— 单 mesh 多 material 时
        // 各 sub-mesh 段的 slot → MaterialInstance* 映射面板。导入多材质模型
        // （glTF 多 primitive material）/ drop 时由 EditorAssetDropHandler 自动挂上；
        // 本 schema 让用户在 Inspector 里可见并逐 slot 重指派材质。
        //
        // 唯一字段 slots（std::vector<MaterialInstance*>）走新增的
        // PropertyType::AssetRefArray —— 与 Renderable.materialInstance 同款 path↔ptr
        // 反查（O(N) 扫 namedMaterialInstances，slot 数 < 10 可忽略），只是整体
        // marshal 成 std::vector<std::string>。slot 数由 mesh 几何决定，本面板不增删行。
        //
        // 注册顺序紧接 Renderable（见 RegisterBuiltinSchemas() 调用次序），让多材质
        // 实体的 slot 面板紧贴 Renderable 段。不挂 Addable（slot 数由 mesh 派生，
        // 手动挂空壳无意义）；挂 Removable 让用户可退回整 mesh 单材质（误删走
        // CaptureComponentState 的 AssetRefArray 分支可 Undo 还原）。
        void RegisterSubMeshMaterialsComponentSchema()
        {
            using SMC              = Orange::Engine::Render::SubMeshMaterialsComponent;
            using MaterialInstance = Orange::Engine::Render::MaterialInstance;

            // slots → vector<path>：逐 slot 反查 ptr → path（nullptr / 查不到留空串，
            // 控件显示 "(none)"，渲染端回退 Renderable.materialInstance）。
            static const auto slotsGet = +[](const void*               c,
                                             const EditorAssetContext& ctx,
                                             void*                     out)
            {
                auto* smc  = static_cast<const SMC*>(c);
                auto* vOut = static_cast<std::vector<std::string>*>(out);
                vOut->clear();
                vOut->reserve(smc->slots.size());
                for (const MaterialInstance* inst : smc->slots)
                {
                    std::string path;
                    if (inst != nullptr)
                    {
                        for (const auto& [p, ptr] : ctx.namedMaterialInstances)
                        {
                            if (ptr == inst)
                            {
                                path = p;
                                break;
                            }
                        }
                    }
                    vOut->push_back(std::move(path));
                }
            };
            // vector<path> → slots：按 path 查 namedMaterialInstances 写回 ptr。控件层
            // （SchemaInspector AssetRefArray case）已对非空 path 做过 EnsureMaterialInstance
            // lazy 注册，故此处 find 命中；Undo/Redo replay 时材质已在表里同样命中。
            // resize 到 paths.size()（控件不增删行 → 长度不变；防御性 resize 兼容
            // 外部 replay 任意长度）。
            static const auto slotsSet = +[](void*                     c,
                                             const EditorAssetContext& ctx,
                                             const void*               in)
            {
                auto*       smc   = static_cast<SMC*>(c);
                const auto& paths = *static_cast<const std::vector<std::string>*>(in);
                smc->slots.resize(paths.size());
                for (std::size_t i = 0; i < paths.size(); ++i)
                {
                    if (paths[i].empty())
                    {
                        smc->slots[i] = nullptr;
                        continue;
                    }
                    auto it       = ctx.namedMaterialInstances.find(paths[i]);
                    smc->slots[i] = (it != ctx.namedMaterialInstances.end())
                                        ? it->second
                                        : nullptr;
                }
            };

            ComponentSchemaBuilder<SMC>("SubMeshMaterials", "Sub-Mesh Materials")
                .Helper("单 mesh 多 material 时各 sub-mesh 段的材质映射。\n"
                        "Slot 下标对应 mesh 的 sub-mesh materialSlot；留空的 slot 渲染时回退到 Renderable 的 Material。\n"
                        "slot 数由 mesh 几何决定（导入多材质模型时自动生成），本面板只重指派各 slot 的材质、不增删 slot。")
                .FieldAssetRefArray("slots", "Slots", AssetKind::Material,
                                    slotsGet, slotsSet)
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
            static const auto cubemapGet = +[](const void*               c,
                                               const EditorAssetContext& ctx,
                                               void*                     out)
            {
                auto* e    = static_cast<const EC*>(c);
                auto* sOut = static_cast<std::string*>(out);
                if (ctx.pAssets == nullptr || !e->cubemap.IsValid())
                {
                    sOut->clear();
                    return;
                }
                *sOut = std::string{ctx.pAssets->PathOf<
                    ::Orange::Engine::Asset::TextureAsset>(e->cubemap)};
            };
            static const auto cubemapSet = +[](void*                     c,
                                               const EditorAssetContext& ctx,
                                               const void*               in)
            {
                auto*       e    = static_cast<EC*>(c);
                const auto& path = *static_cast<const std::string*>(in);
                if (ctx.pAssets == nullptr)
                {
                    return;
                }
                if (path.empty())
                {
                    e->cubemap = {};
                    return;
                }
                auto lr = ctx.pAssets->Load<
                    ::Orange::Engine::Asset::TextureAsset>(path);
                if (lr.IsOk())
                {
                    e->cubemap = lr.Value();
                }
            };

            ComponentSchemaBuilder<EC>("Environment", "Environment")
                .Helper("Environment 作为全局单例使用 —— 场景中只有第一个 EnvironmentComponent 生效，"
                        "其余会被忽略。建议每个 scene 至多挂一个。")
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

        // PostProcessComponent 的 Inspector schema —— 屏幕空间 post + PCSS 的数据驱动控制
        // 面板（取代 ScenePanel/sample hardcode）。
        //
        // V2 GAP-2026-05-27-postprocess-component-local-volume 落地：Pipeline 升级为
        // collect-all + 相机位置混合的 volume 系统后，本 schema 暴露 4 个 volume 容器字
        // 段（mode/localExtent/priority/blendDistance）让用户在 Inspector 创建 Local
        // 体积；scene 序列化（ComponentSerializers.cpp）此前已带这些字段（v1 即铺路），
        // 老 scene Load 行为不变（所有 mode 默认 Global，等价 v1 first-found）。
        // bloom/tonemap 不在本组件（由默认 chain 管，参 GAP-2026-05-27-tonemap-operator-
        // selection）。
        void RegisterPostProcessComponentSchema()
        {
            using PP = Orange::Engine::Render::PostProcessComponent;

            // Mode enum 项名表 —— 顺序与 enum class Mode 定义 (Global=0, Local=1) 严格
            // 对齐。静态生命周期；EnumNames 不复制。
            static const char* const kModeNames[] = {"Global", "Local"};
            static_assert(static_cast<int>(PP::Mode::Global) == 0, "PostProcess::Mode enum drift");
            static_assert(static_cast<int>(PP::Mode::Local) == 1, "PostProcess::Mode enum drift");

            ComponentSchemaBuilder<PP>("PostProcess", "Post Process")
                .Helper("场景后处理设置（V2 local volume：多组件按相机位置混合）。\n"
                        "  Global：作 base 底，全局生效\n"
                        "  Local ：实体 Transform 处 localExtent 半尺寸盒，相机进入按\n"
                        "          blendDistance smoothstep 淡入，按 priority 仲裁\n"
                        "bloom / tonemap 由默认渲染链管理，不在此组件。")
                // —— Volume 容器（V2 GAP-2026-05-27-postprocess-component-local-volume）——
                .FieldEnum<&PP::mode>("mode", "Mode")
                .EnumNames(kModeNames, 2)
                .Tooltip("Global = 全局作用（多个 Global 取场景内第一个）；\n"
                         "Local  = 实体 Transform 位置 ± localExtent 半尺寸盒内生效，\n"
                         "         相机出盒后 blendDistance 内 smoothstep 淡出。")
                .Field<&PP::localExtent>("localExtent", "Local Extent (m)")
                .Range(0.01f, 100.0f)
                .DragSpeed(0.05f)
                .Tooltip("Mode=Local 时生效：盒半尺寸（相机到 entity.Transform.position\n"
                         "                  各轴距离 ≤ 对应分量则视为盒内 weight=1）。")
                .Field<&PP::priority>("priority", "Priority")
                .DragSpeed(0.01f)
                .Tooltip("多 volume 重叠时仲裁谁压谁：标量字段按 priority 升序 lerp\n"
                         "（高 priority 最后 apply 更 dominant）；bool / 离散字段按\n"
                         "weight>0 中最高 priority 接管。Global 底也参与排序。")
                .Field<&PP::blendDistance>("blendDistance", "Blend Distance (m)")
                .Range(0.0f, 20.0f)
                .DragSpeed(0.05f)
                .Tooltip("Mode=Local 时生效：相机出盒后这段距离内按 smoothstep 从 1 淡\n"
                         "到 0；blendDistance=0 = 硬切换无过渡。")
                // —— SSAO / GTAO ——
                .Field<&PP::ssaoEnabled>("ssaoEnabled", "SSAO Enabled")
                .Field<&PP::ssaoUseGtao>("ssaoUseGtao", "Use GTAO")
                .Tooltip("勾选 = GTAO（horizon-based，更准更平滑）；取消 = 半球 kernel SSAO。")
                .Field<&PP::ssaoRadius>("ssaoRadius", "SSAO Radius (m)")
                .Range(0.05f, 2.0f)
                .DragSpeed(0.01f)
                .Field<&PP::ssaoStrength>("ssaoStrength", "SSAO Strength")
                .Range(0.0f, 2.0f)
                .DragSpeed(0.01f)
                .Field<&PP::ssaoPower>("ssaoPower", "SSAO Power")
                .Range(1.0f, 4.0f)
                .DragSpeed(0.02f)
                // —— SSR ——
                .Field<&PP::ssrEnabled>("ssrEnabled", "SSR Enabled")
                .Field<&PP::ssrMaxDistance>("ssrMaxDistance", "SSR Max Distance (m)")
                .Range(1.0f, 64.0f)
                .DragSpeed(0.1f)
                .Field<&PP::ssrThickness>("ssrThickness", "SSR Thickness")
                .Range(0.05f, 4.0f)
                .DragSpeed(0.01f)
                .Field<&PP::ssrStrength>("ssrStrength", "SSR Strength")
                .Range(0.0f, 1.0f)
                .DragSpeed(0.01f)
                // —— 接触阴影 ——
                .Field<&PP::contactEnabled>("contactEnabled", "Contact Shadow Enabled")
                .Field<&PP::contactLength>("contactLength", "Contact Length (m)")
                .Range(0.02f, 1.0f)
                .DragSpeed(0.005f)
                .Field<&PP::contactThickness>("contactThickness", "Contact Thickness")
                .Range(0.05f, 2.0f)
                .DragSpeed(0.01f)
                .Field<&PP::contactStrength>("contactStrength", "Contact Strength")
                .Range(0.0f, 1.0f)
                .DragSpeed(0.01f)
                // —— 景深 ——
                .Field<&PP::dofEnabled>("dofEnabled", "DoF Enabled")
                .Tooltip("编辑器相机移动时对焦面固定在 view 空间 focusDistance 处。")
                .Field<&PP::dofFocusDistance>("dofFocusDistance", "Focus Distance (m)")
                .Range(0.5f, 100.0f)
                .DragSpeed(0.1f)
                .Field<&PP::dofFocusRange>("dofFocusRange", "Focus Range (m)")
                .Range(0.5f, 50.0f)
                .DragSpeed(0.1f)
                .Field<&PP::dofMaxCoCRadius>("dofMaxCoCRadius", "Max Blur Radius (uv)")
                .Range(0.002f, 0.05f)
                .DragSpeed(0.001f)
                // —— TAA ——
                .Field<&PP::taaEnabled>("taaEnabled", "TAA Enabled")
                .Field<&PP::taaFeedback>("taaFeedback", "TAA Feedback")
                .Range(0.8f, 0.98f)
                .DragSpeed(0.005f)
                .Tooltip("历史权重。越高越稳越糊、收敛越慢。典型 0.85–0.95。")
                // —— 色彩分级 ——
                .Field<&PP::gradeEnabled>("gradeEnabled", "Color Grade Enabled")
                .Field<&PP::gradeExposure>("gradeExposure", "Exposure (stops)")
                .Range(-4.0f, 4.0f)
                .DragSpeed(0.02f)
                .Field<&PP::gradeContrast>("gradeContrast", "Contrast")
                .Range(0.5f, 2.0f)
                .DragSpeed(0.01f)
                .Field<&PP::gradeSaturation>("gradeSaturation", "Saturation")
                .Range(0.0f, 2.0f)
                .DragSpeed(0.01f)
                .Field<&PP::gradeTemperature>("gradeTemperature", "Temperature")
                .Range(-1.0f, 1.0f)
                .DragSpeed(0.01f)
                .Tooltip("-1 冷偏蓝 .. +1 暖偏橙。")
                .Field<&PP::gradeTint>("gradeTint", "Tint")
                .Range(-1.0f, 1.0f)
                .DragSpeed(0.01f)
                .Tooltip("-1 偏绿 .. +1 偏品红。")
                // —— 相机运动模糊 ——
                .Field<&PP::motionBlurEnabled>("motionBlurEnabled", "Motion Blur Enabled")
                .Tooltip("相机运动模糊。仅相机重投影（无 per-object 矢量）；\n"
                         "静态相机下无效果，靠相机运镜表现。")
                .Field<&PP::motionBlurIntensity>("motionBlurIntensity", "Intensity")
                .Range(0.0f, 2.0f)
                .DragSpeed(0.02f)
                .Tooltip("速度强度乘子。1 = 一帧相机位移的全程拖影。")
                .Field<&PP::motionBlurMaxRadius>("motionBlurMaxRadius", "Max Radius (uv)")
                .Range(0.0f, 0.2f)
                .DragSpeed(0.002f)
                .Tooltip("最大模糊半径（屏幕比例）。clamp 速度防超长拖影。典型 0.02–0.08。")
                .Field<&PP::motionBlurSampleCount>("motionBlurSampleCount", "Sample Count")
                .Range(2.0f, 32.0f)
                .DragSpeed(1.0f)
                .Tooltip("沿速度方向的采样数。8–16 常见；越多越平滑越贵。")
                // —— 镜头效果（色散 + 暗角）——
                .Field<&PP::lensEnabled>("lensEnabled", "Lens Enabled")
                .Tooltip("镜头效果总开关（色散 + 暗角）。两项均可单独调 0 关闭。")
                .Field<&PP::lensChromaticAberration>("lensChromaticAberration", "Chromatic Aberration")
                .Range(0.0f, 0.02f)
                .DragSpeed(0.0005f)
                .Tooltip("色散量（径向 RGB 分离）。0 = 无；过大边缘出明显彩边。典型 0.001–0.01。")
                .Field<&PP::lensVignetteIntensity>("lensVignetteIntensity", "Vignette Intensity")
                .Range(0.0f, 1.0f)
                .DragSpeed(0.01f)
                .Tooltip("暗角强度。0 = 无，1 = 边缘压到全黑。")
                .Field<&PP::lensVignetteSmoothness>("lensVignetteSmoothness", "Vignette Smoothness")
                .Range(0.0f, 1.0f)
                .DragSpeed(0.01f)
                .Tooltip("暗角范围/软硬。越大渐变越宽、起始越往中心。典型 0.3–0.7。")
                // —— 锐化（CAS）——
                .Field<&PP::sharpenEnabled>("sharpenEnabled", "Sharpen Enabled")
                .Tooltip("CAS 式自适应锐化，TAA 的标准搭档（恢复 TAA resolve 软化的细节）。")
                .Field<&PP::sharpenStrength>("sharpenStrength", "Sharpen Strength")
                .Range(0.0f, 1.0f)
                .DragSpeed(0.01f)
                .Tooltip("锐化强度。TAA 配套典型 0.3–0.6；过大出镶边。")
                // —— 阴影质量（PCSS）——
                .Field<&PP::pcssLightSize>("pcssLightSize", "PCSS Light Size (texel)")
                .Range(0.0f, 32.0f)
                .DragSpeed(0.1f)
                .Tooltip("0 = 固定 PCF；>0 = PCSS 软阴影（接触硬、远处软）。\n"
                         "作用 directional + spot 阴影。8–16 在 2048 分辨率下可见柔和。")
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
                .Field<&HC::parent>("parent", "Parent")
                .Field<&HC::firstChild>("firstChild", "First child")
                .Field<&HC::prevSibling>("prevSibling", "Prev sibling")
                .Field<&HC::nextSibling>("nextSibling", "Next sibling")
                .Register();
        }

        void RegisterColliderComponentSchema()
        {
            using CC = Orange::Engine::Physics::ColliderComponent;
            using Orange::Engine::Physics::BoxDesc;
            using Orange::Engine::Physics::CircleDesc;
            using Orange::Engine::Physics::EdgeChainDesc;
            using Orange::Engine::Physics::PolygonDesc;

            // shape 是 std::variant<CircleDesc, BoxDesc, PolygonDesc, EdgeChainDesc>。
            // 后期补口子：v0.1 ~ v0.2.5 期 deliberately 不实现 "shape 类型切换控件"
            // （理由"切换 alternative 会重置数据，误操作风险大"），但用户撞上"加
            // Collider 永远是 Circle、要 Box 必须改代码"导致 UX 断裂；本期补上：
            //   * 顶部加 Shape Type Combo 走 FieldCustomEnum + EnumNames 渲染 Circle
            //     / Box / Polygon / Edge Chain 四项；切换走命令栈，Undo 可恢复
            //   * Polygon / EdgeChain 顶点表升级为可编辑（新增 PropertyType::
            //     PolygonVertices / EdgeChainVertices，SchemaInspector 加 case 渲染
            //     表格 + Add/Remove）
            //
            // 4 个 shape 段通过 VisibleIf(holds_alternative<X>) 互斥显示：
            //   Circle    → Radius + Center
            //   Box       → Half Extents + Center
            //   Polygon   → Vertices 表格（kMax=8）
            //   EdgeChain → Vertices 表格（kMax=16）+ isLoop checkbox
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
                const auto& shape         = static_cast<const CC*>(c)->shape;
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
                const auto& shape             = static_cast<const CC*>(c)->shape;
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
                const auto& shape             = static_cast<const CC*>(c)->shape;
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
                const auto& shape             = static_cast<const CC*>(c)->shape;
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
            static const auto isCircle = +[](const void* c) -> bool
            { return std::holds_alternative<CircleDesc>(static_cast<const CC*>(c)->shape); };
            static const auto isBox = +[](const void* c) -> bool
            { return std::holds_alternative<BoxDesc>(static_cast<const CC*>(c)->shape); };
            static const auto isPolygon = +[](const void* c) -> bool
            { return std::holds_alternative<PolygonDesc>(static_cast<const CC*>(c)->shape); };
            static const auto isEdgeChain = +[](const void* c) -> bool
            { return std::holds_alternative<EdgeChainDesc>(static_cast<const CC*>(c)->shape); };

            // ---- shape type 切换 ----
            // variant alternative 编号与 Combo 项一一对应（CircleDesc=0 / BoxDesc=1
            // / PolygonDesc=2 / EdgeChainDesc=3）；setShapeType 收到 newIdx 时把
            // shape 重置为对应 alternative 的默认构造值。
            //
            // 数据丢失语义：从 Polygon 切到 Circle 会清空 vertex 列表 / 半径回到
            // 1.0 默认；mutation 走 SchemaInspector::PropertyType::Enum 标准命令栈
            // —— Undo 可一键恢复（SetFieldValueCommand<int> 持 oldVal=旧 index，
            // setShapeType 的 apply replay 内会从 Combo 选中"新 index"再走默认重
            // 置；这意味着 Undo 路径会**恢复到 oldVal 对应的默认 alternative 值**，
            // 不是 oldVal 对应 alternative 的"原数据"——这是 variant 切换天然的
            // 信息丢失。Tooltip 内显式提示该语义，避免用户误以为"切回去数据就回
            // 来了"。
            //
            // 已知 corner case：若 ColliderComponent 没挂（component pointer 为
            // null），SchemaInspector 不会走到该字段的 get/set（外层 schema.has
            // 路径已过滤）。
            static const auto getShapeType = +[](const void* c, void* out)
            {
                *static_cast<int*>(out) =
                    static_cast<int>(static_cast<const CC*>(c)->shape.index());
            };
            static const auto setShapeType = +[](void* c, const void* in)
            {
                auto&     shape  = static_cast<CC*>(c)->shape;
                const int newIdx = *static_cast<const int*>(in);
                switch (newIdx)
                {
                    case 0:
                        shape = CircleDesc{};
                        break;
                    case 1:
                        shape = BoxDesc{};
                        break;
                    case 2:
                    {
                        // 切到 Polygon 给一个可用默认（单位方块 4 顶点，CCW）——空
                        // PolygonDesc{}（count=0）会让视口顶点编辑无顶点可拖、且物理
                        // 端退化（GAP-2026-05-25 A3 反馈"编辑不生效 / dynamic 不掉"）。
                        PolygonDesc p{};
                        p.count       = 4;
                        p.vertices[0] = glm::vec2(-0.5f, -0.5f);
                        p.vertices[1] = glm::vec2(0.5f, -0.5f);
                        p.vertices[2] = glm::vec2(0.5f, 0.5f);
                        p.vertices[3] = glm::vec2(-0.5f, 0.5f);
                        shape         = p;
                        break;
                    }
                    case 3:
                    {
                        // 切到 Edge Chain 给一条 3 顶点折线默认（同理,避免空 chain）。
                        // EdgeChain 是 static 地形用途;dynamic 物体想掉落应选 Polygon/Box。
                        EdgeChainDesc e{};
                        e.count       = 3;
                        e.vertices[0] = glm::vec2(-1.0f, 0.0f);
                        e.vertices[1] = glm::vec2(0.0f, 0.0f);
                        e.vertices[2] = glm::vec2(1.0f, 0.0f);
                        e.isLoop      = false;
                        shape         = e;
                        break;
                    }
                    default: /* out-of-range：保留旧值，与其它 Enum case 一致 */
                        break;
                }
            };
            static const char* const kShapeTypeNames[] = {
                "Circle", "Box", "Polygon", "Edge Chain"};

            // ---- Polygon / EdgeChain：整 desc 读写（命令栈 mutation） ----
            static const auto getPolygon = +[](const void* c, void* out)
            {
                const auto& shape               = static_cast<const CC*>(c)->shape;
                *static_cast<PolygonDesc*>(out) = std::holds_alternative<PolygonDesc>(shape)
                                                      ? std::get<PolygonDesc>(shape)
                                                      : PolygonDesc{};
            };
            static const auto setPolygon = +[](void* c, const void* in)
            {
                auto& shape = static_cast<CC*>(c)->shape;
                if (std::holds_alternative<PolygonDesc>(shape))
                {
                    shape = *static_cast<const PolygonDesc*>(in);
                }
            };
            static const auto getEdgeChain = +[](const void* c, void* out)
            {
                const auto& shape                 = static_cast<const CC*>(c)->shape;
                *static_cast<EdgeChainDesc*>(out) = std::holds_alternative<EdgeChainDesc>(shape)
                                                        ? std::get<EdgeChainDesc>(shape)
                                                        : EdgeChainDesc{};
            };
            static const auto setEdgeChain = +[](void* c, const void* in)
            {
                auto& shape = static_cast<CC*>(c)->shape;
                if (std::holds_alternative<EdgeChainDesc>(shape))
                {
                    shape = *static_cast<const EdgeChainDesc*>(in);
                }
            };

            // 字段顺序：通用物理字段（density / friction / restitution / isSensor）
            // 在前，shape 段在后。v0.1 hardcode 内顺序相反（shape 在前），但 schema
            // 是线性顺序——Polygon / EdgeChain 的"零字段段"放在中间会导致用户感觉
            // 通用字段"被挤"到段末尾。颠倒顺序让 shape 段总在 component 段末尾，
            // 视觉更稳定。
            ComponentSchemaBuilder<CC>("Collider", "Collider")
                .Field<&CC::density>("density", "Density")
                .DragSpeed(0.01f)
                .Field<&CC::friction>("friction", "Friction")
                .Range(0.0f, 1.0f)
                .DragSpeed(0.01f)
                .Field<&CC::restitution>("restitution", "Restitution")
                .Range(0.0f, 1.0f)
                .DragSpeed(0.01f)
                .Field<&CC::isSensor>("isSensor", "Is Sensor")
                // ---- Shape Type Combo（v0.9.5 后置补丁：变 hardcode Circle 为可切） ----
                .FieldCustomEnum("shape.type", "Shape Type",
                                 getShapeType, setShapeType)
                .EnumNames(kShapeTypeNames, 4)
                .Tooltip("切换形状类型会重置 shape 特定字段（半径 / 半宽 / 顶点表）；\n"
                         "Undo (Ctrl+Z) 仅恢复到旧类型的默认值，**不**保留切换前的原始数据。")
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
                // ---- Polygon ----
                .FieldCustom<PolygonDesc>("polygon.vertices", "Vertices",
                                          getPolygon, setPolygon)
                .GroupSeparator("Shape: Polygon")
                .VisibleIf(isPolygon)
                // ---- EdgeChain（含 isLoop checkbox 走 PropertyType case 内渲染） ----
                .FieldCustom<EdgeChainDesc>("edgechain.vertices", "Vertices",
                                            getEdgeChain, setEdgeChain)
                .GroupSeparator("Shape: Edge Chain")
                .VisibleIf(isEdgeChain)
                .Addable()
                .Removable()
                .Register();
        }

        void RegisterAnimatorComponentSchema()
        {
            using AC            = Orange::Engine::Animation::AnimatorComponent;
            using ClipAnimator  = Orange::Engine::Animation::ClipAnimator;
            using AnimationClip = Orange::Engine::Animation::AnimationClip;
            using TC            = Orange::Engine::Scene::TransformComponent;

            // 取 AnimatorComponent 内的 ClipAnimator*（仅 backend=="clip" 时非空）。
            // backend 是 skeletal / procedural / null 时返回 nullptr —— clip 专属字段
            // 据此 visibleIf 门控 + get/set 早退。dynamic_cast 安全（IAnimator 多态）。
            static const auto asClipAnimator = +[](const AC* ac) -> ClipAnimator*
            {
                if (ac == nullptr || !ac->animator)
                {
                    return nullptr;
                }
                return dynamic_cast<ClipAnimator*>(ac->animator.get());
            };

            // backend 名只读 String —— null 显示 "(no backend)"，与"未挂 backend"
            // 语义一致。
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
            // setter 是 no-op —— readOnly 路径下 SchemaInspector 不会调用 set。
            static const auto setBackendNameNoOp = +[](void*, const void*) {};

            // ---- 仅 backend=="clip" 时有意义的字段（visibleIf 门控）----------------
            // 谓词：本 AnimatorComponent 的 backend 是不是 clip。参 Collider shape
            // 的 holds_alternative 互斥 visibleIf。
            static const auto isClipBackend = +[](const void* c) -> bool
            {
                return asClipAnimator(static_cast<const AC*>(c)) != nullptr;
            };

            // clip AssetRef：读 ClipAnimator::SourceAssetPath()；写时解析 path →
            // AssetRegistry::Load<AnimationClip> → Get → SetClip + SetSourceAssetPath。
            // 走 ADR-004 方案 B 的 AssetRefGetFn/SetFn 专用槽（带 EditorAssetContext）。
            static const auto clipGet = +[](const void*               c,
                                            const EditorAssetContext& ctx,
                                            void*                     out)
            {
                (void)ctx;
                auto* sOut = static_cast<std::string*>(out);
                sOut->clear();
                ClipAnimator* clip = asClipAnimator(static_cast<const AC*>(c));
                if (clip != nullptr)
                {
                    *sOut = std::string(clip->SourceAssetPath());
                }
            };
            static const auto clipSet = +[](void*                     c,
                                            const EditorAssetContext& ctx,
                                            const void*               in)
            {
                ClipAnimator* clip = asClipAnimator(static_cast<AC*>(c));
                if (clip == nullptr)
                {
                    return;
                }
                const auto& path = *static_cast<const std::string*>(in);
                if (path.empty())
                {
                    // 清空字段：换成空 clip + 清来源路径（pose 留当前帧，下次 Seek
                    // 才更新；与 mesh 字段清空"换 Invalid handle"对位）。
                    clip->SetClip(AnimationClip{});
                    clip->SetSourceAssetPath({});
                    return;
                }
                if (ctx.pAssets == nullptr)
                {
                    return;
                }
                auto lr = ctx.pAssets->Load<AnimationClip>(path);
                if (lr.IsErr())
                {
                    return;
                }
                const AnimationClip* loaded = ctx.pAssets->Get<AnimationClip>(lr.Value());
                if (loaded == nullptr)
                {
                    return;
                }
                clip->SetClip(*loaded);
                clip->SetSourceAssetPath(path);
            };

            // loop checkbox：桥接 ClipAnimator::IsLooping() / SetLoop()。
            static const auto loopGet = +[](const void* c, void* out)
            {
                ClipAnimator* clip       = asClipAnimator(static_cast<const AC*>(c));
                *static_cast<bool*>(out) = (clip != nullptr) && clip->IsLooping();
            };
            static const auto loopSet = +[](void* c, const void* in)
            {
                ClipAnimator* clip = asClipAnimator(static_cast<AC*>(c));
                if (clip != nullptr)
                {
                    clip->SetLoop(*static_cast<const bool*>(in));
                }
            };

            // ---- "Animator (Clip)" 自定义 add 路径（c10 Renderable 同款）----------
            // 默认构造抽象 IAnimator 不可能；走 AddableWith 显式建 ClipAnimator
            // （空 clip + target=self Transform）。capture-less +lambda 才能转 AddFn
            // 函数指针；world / entity 在 lambda 内运行时取。
            static const auto animatorClipAddWith =
                +[](EditorHost& host, Orange::Engine::Entity e)
            {
                auto* pWorld = host.scene.pWorld.get();
                if (pWorld == nullptr)
                {
                    return;
                }
                TC* target = pWorld->GetComponent<TC>(e); // self Transform 作写目标
                AC  ac{};
                ac.animator = std::make_unique<ClipAnimator>(AnimationClip{}, target);
                pWorld->AddComponent<AC>(e, std::move(ac));
            };

            // displayName "Animator (Clip)" —— +Add Component 菜单显示该名；schema
            // typeName 仍是 "Animator"（plugin CanHandle / 序列化器都按此匹配）。
            // Skeletal / Procedural 仍不 Addable（需 skeleton / channel 参数，不适合
            // 默认构造）；只开 clip。
            ComponentSchemaBuilder<AC>("Animator", "Animator (Clip)")
                .FieldCustom<std::string>("backend", "Backend",
                                          getBackendName, setBackendNameNoOp)
                .ReadOnly()
                // clip AssetRef（仅 clip backend 可见，可拖 .anim / Undo 重指派）。
                .FieldAssetRef("clip", "Clip",
                               AssetKind::AnimationClip, clipGet, clipSet)
                .VisibleIf(isClipBackend)
                // loop 勾选（仅 clip backend 可见）。Play/Pause/scrub 播放控制走
                // AnimatorMiniPreviewPlugin 的 ParseEnd（自定义 ImGui + 编辑期预览
                // 状态接线，超出 schema 字段"纯数据"语义——与 AudioSource Play/Stop
                // 走 plugin 同款分工）。
                .FieldCustom<bool>("loop", "Loop", loopGet, loopSet)
                .VisibleIf(isClipBackend)
                .Tooltip("勾选后 clip 播放到结尾循环回卷；取消则到结尾停在末帧。\n"
                         "桥接 ClipAnimator::SetLoop()，与 .anim 资产内的 loop 标记独立。")
                .AddableWith(animatorClipAddWith)
                .Removable()
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

            static const auto soundGet = +[](const void*               c,
                                             const EditorAssetContext& ctx,
                                             void*                     out)
            {
                auto* a    = static_cast<const AS*>(c);
                auto* sOut = static_cast<std::string*>(out);
                if (ctx.pAssets == nullptr || !a->sound.IsValid())
                {
                    sOut->clear();
                    return;
                }
                *sOut = std::string{ctx.pAssets->PathOf<
                    ::Orange::Engine::Asset::SoundAsset>(a->sound)};
            };
            static const auto soundSet = +[](void*                     c,
                                             const EditorAssetContext& ctx,
                                             const void*               in)
            {
                auto*       a    = static_cast<AS*>(c);
                const auto& path = *static_cast<const std::string*>(in);
                if (ctx.pAssets == nullptr)
                {
                    return;
                }
                if (path.empty())
                {
                    a->sound = {};
                    return;
                }
                auto lr = ctx.pAssets->Load<
                    ::Orange::Engine::Asset::SoundAsset>(path);
                if (lr.IsOk())
                {
                    a->sound = lr.Value();
                }
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

        // ScriptComponent 的 Inspector schema —— C# 脚本引用（ADR-017）的纯数据编辑面板。
        //
        // ScriptComponent 是纯数据组件（assemblyPath / typeName 两字符串 + fieldOverrides
        // 列表），**不**受 ORANGE_ENGINE_WITH_DOTNET 门控——含脚本组件的场景在无 dotnet
        // 构建里也能 round-trip。因此本 schema 的注册同样始终编译，与运行脚本的能力
        // （ScriptSystem / ScriptRuntime，dotnet-gated）解耦：编辑器始终能 Add-Component +
        // 编辑 (assemblyPath, typeName)，只是 Play 时是否真实例化脚本由 dotnet 构建决定。
        //
        // 本 schema 仅暴露两个 string 字段（P0）：
        //   * assemblyPath —— game assembly（含脚本类型）的相对路径。
        //   * typeName     —— assembly-qualified 类型全名，如
        //                     "OrangeFixtures.Mover, ScriptFixtures"。
        //
        // fieldOverrides（authored tweakable 列表）是"动态长度 + 每行 3 异构字段"的
        // struct 数组，超出 schema 通用 PropertyType 的标量 / 固定数组表达力——走
        // ScriptFieldOverridesInspectorPlugin 的 ParseEnd 钩子做自定义列表 UI（与
        // AudioSource Play / Collider 顶点编辑同款"plugin 承载超出字段控件的 UI"分工），
        // 不为它新增 PropertyType + 改 SchemaInspector。
        void RegisterScriptComponentSchema()
        {
            using SC = Orange::Engine::Script::ScriptComponent;
            ComponentSchemaBuilder<SC>("Script", "Script")
                .Helper("挂一个托管 C# 脚本类（OrangeScript 子类）到本实体（ADR-017）。\n"
                        "Assembly Path = game assembly 相对路径；Type Name = assembly-qualified 类型全名"
                        "（如 \"OrangeFixtures.Mover, ScriptFixtures\"）。\n"
                        "Field Overrides 在下方列表编辑（实例化后、OnStart 前注入脚本 public 字段）。\n"
                        "脚本实际运行需 dotnet 构建（Play 时由 ScriptSystem 驱动）；无 dotnet 构建仅作数据保存。")
                .Field<&SC::assemblyPath>("assemblyPath", "Assembly Path")
                .Tooltip("含脚本类型的 game assembly 路径（运行期由 ScriptRuntime 解析）。")
                .Field<&SC::typeName>("typeName", "Type Name")
                .Tooltip("assembly-qualified 类型全名，如 \"OrangeFixtures.Mover, ScriptFixtures\"。")
                .Addable()
                .Removable()
                .Register();
        }

    } // anonymous namespace

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
        RegisterSpotLightSchema();
        RegisterEnvironmentComponentSchema();
        RegisterPostProcessComponentSchema();
        RegisterRenderableComponentSchema();
        RegisterSubMeshMaterialsComponentSchema();
        RegisterRigidBodyComponentSchema();
        RegisterColliderComponentSchema();
        RegisterParticleEmitterComponentSchema();
        RegisterAudioSourceComponentSchema();
        RegisterAnimatorComponentSchema();
        RegisterCameraComponentSchema();
        RegisterScriptComponentSchema();
        // 所有内置组件 schema 已全数迁完。后续 commit（c10）改 Add Component 菜
        // 单走 schema 注册表枚举驱动；c11 / c12 引入 IEditor*Plugin 抽象。
    }

} // namespace Orange::Editor::Schema
