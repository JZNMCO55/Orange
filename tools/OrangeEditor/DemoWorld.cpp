// DemoWorld 实现 —— 见 DemoWorld.h 的注释。

#include "DemoWorld.h"

#include "EditorHierarchy.h"
#include "MaterialFileIO.h"
#include "demo_game/HealthComponent.h"

#include <orange/engine/core/Log.h>

#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/animation/AnimatorRegistry.h>
#include <orange/engine/animation/ClipAnimator.h>
#include <orange/engine/animation/ProceduralAnimator.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshLoader.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/asset/SkeletonAsset.h>
#include <orange/engine/asset/SkeletonLoader.h>
#include <orange/engine/asset/SoundAsset.h>
#include <orange/engine/asset/SoundLoader.h>
#include <orange/engine/asset/TextureAsset.h>
#include <orange/engine/asset/TextureLoader.h>
#include <orange/engine/core/Serialization.h>
#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/ColliderDesc.h>
#include <orange/engine/physics/RigidBodyComponent.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/EnvironmentComponent.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/render/ParticleEmitterComponent.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// BeepWav helpers + Make{Plane,Cube,Sphere}Mesh + InitializeEditorAssets +
// BuildNamedMaterialInstances 已迁出至 BuiltinAssets.{h,cpp}（v1.0.1 c11
// 拆分；DemoWorld.cpp 仅保留 Seed 函数）。


// demo 世界层级：
//   Root
//   ├── Camera           （2.5D 侧视角）
//   ├── Sun              （平行光 + 软阴影）
//   └── Geometry
//       ├── Ground       （大平面，textured，静态刚体）
//       ├── Backdrop     （竖立背景平面，rim_light）
//       ├── Platform L   （左台，toon，静态刚体）
//       ├── Platform R   （右台，toon，静态刚体）
//       ├── Tower        （高塔 scale×2Y，toon，静态刚体）
//       ├── Glow Box     （溶解方块，dissolve，自动动画）
//       ├── Emissive Pillar（自发光细柱，emissive）
//       ├── Dynamic Box  （动态刚体，Play Mode 物理演示，y=4 悬空下落）
//       ├── Fire Emitter （粒子：火焰，暖橙 HDR → bloom）
//       └── Sparkle Emitter（粒子：萤火，蓝白 HDR → bloom）
void SeedDemoWorld(EditorHost& host)
{
    using ::Orange::Engine::Entity;
    using ::Orange::Engine::Scene::NameComponent;
    using ::Orange::Engine::Scene::TransformComponent;
    using ::Orange::Engine::Render::Camera;
    using ::Orange::Engine::Render::DirectionalLight;
    using ::Orange::Engine::Render::RenderableComponent;
    using ::Orange::Engine::Render::ParticleEmitterComponent;
    using ::Orange::Engine::Render::ParticleEmitterDesc;
    using ::Orange::Engine::Physics::BodyType;
    using ::Orange::Engine::Physics::ColliderComponent;
    using ::Orange::Engine::Physics::BoxDesc;
    using ::Orange::Engine::Physics::CircleDesc;
    using ::Orange::Engine::Physics::EdgeChainDesc;
    using ::Orange::Engine::Physics::PolygonDesc;
    using ::Orange::Engine::Physics::RigidBodyComponent;
    using ::Orange::Engine::Animation::AnimatorComponent;
    using ::Orange::Engine::Animation::ClipAnimator;

    auto& world = *host.scene.pWorld;
    auto make = [&](const char* name) -> Entity {
        Entity e = world.CreateEntity();
        world.AddComponent<NameComponent>(e, NameComponent{name});
        world.AddComponent<TransformComponent>(e, TransformComponent{});
        return e;
    };

    // ---- 层级容器 -------------------------------------------------------
    Entity root     = make("Root");
    Entity camera   = make("Camera");
    Entity sun      = make("Sun");
    Entity geometry = make("Geometry");

    // ---- Geometry 下的子节点 --------------------------------------------
    Entity ground          = make("Ground");
    Entity backdrop        = make("Backdrop");
    Entity platformLeft    = make("Platform L");
    Entity platformRight   = make("Platform R");
    Entity tower           = make("Tower");
    Entity glowBox         = make("Glow Box");
    Entity emissivePillar  = make("Emissive Pillar");
    Entity dynamicBox      = make("Dynamic Box");
    Entity fireEmitter     = make("Fire Emitter");
    Entity sparkleEmitter  = make("Sparkle Emitter");

    // v0.3 c1：演示 / 验收前置实体。
    //   * slimeDoll：Animator-only 实体，无 Renderable——专门展示 Animator schema
    //     段 + ReadOnly Backend 字段，Inspector 可直接观察。
    //   * staticCircle / staticPolygon / staticEdgeChain：Box 之外三种 shape
    //     的 Collider 验收前置实体；位置放右侧远端，无 Renderable，物理上
    //     仅作为 schema 段渲染样本——切换到这三个实体看 Inspector 即可验
    //     c8 三个 visibleIf 互斥段。
    Entity slimeDoll       = make("Slime Doll");
    Entity clipCube        = make("Animated Cube (clip)");
    Entity testFighter     = make("Test Fighter");
    Entity staticCircle    = make("Static Circle (demo)");
    Entity staticPolygon   = make("Static Polygon (demo)");
    Entity staticEdgeChain = make("Static EdgeChain (demo)");

    // ---- Camera：2.5D 侧视角 + 轻微俯角 --------------------------------
    // EditorCamera 会每帧覆写 view 矩阵；此处的 view 仅在非编辑器消费
    // （如 Play Mode 截图、非 editor host）时生效。
    {
        Camera cam = Camera::Perspective(glm::radians(45.0f),
                                         /*aspect=*/1.0f,
                                         /*zNear=*/0.1f,
                                         /*zFar=*/100.0f);
        cam.view = glm::lookAt(glm::vec3(0.0f, 2.0f, 8.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent<Camera>(camera, cam);
    }

    // ---- Sun：暖色平行光 + 软阴影开启 -----------------------------------
    {
        auto* tc = world.GetComponent<TransformComponent>(sun);
        if (tc != nullptr)
        {
            tc->position = glm::vec3(5.0f, 8.0f, 5.0f);
            // 方向由 rotation 派生；identity 表示光向下，这里把传统
            // (0.4,-1,0.3) 朝向编码进 rotation 里。
            tc->rotation = ::Orange::Engine::Render::
                MakeDirectionalLightRotationFromDir(
                    glm::vec3(0.4f, -1.0f, 0.3f));
        }

        DirectionalLight dl{};
        dl.color       = glm::vec3(1.0f, 0.93f, 0.78f);  // 暖黄阳光
        dl.intensity   = 1.3f;
        dl.castsShadow = true;  // 开启软阴影
        world.AddComponent<DirectionalLight>(sun, dl);
    }

    // ---- Ground（大平面，textured，静态刚体）----------------------------
    {
        auto* tc = world.GetComponent<TransformComponent>(ground);
        if (tc != nullptr) { tc->position.y = -0.5f; }

        RenderableComponent rc{};
        rc.mesh             = host.assets.planeMeshHandle;
        rc.materialInstance = host.assets.pFloorMaterial.get();
        rc.visible          = true;
        rc.castsShadow      = false;
        world.AddComponent<RenderableComponent>(ground, rc);

        RigidBodyComponent rb{};
        rb.type          = BodyType::Static;
        rb.fixedRotation = true;
        rb.gravityScale  = 0.0f;
        world.AddComponent<RigidBodyComponent>(ground, rb);

        ColliderComponent cc{};
        cc.shape    = BoxDesc{glm::vec2{5.0f, 0.5f}};
        cc.density  = 0.0f;
        cc.friction = 0.6f;
        world.AddComponent<ColliderComponent>(ground, cc);
    }

    // ---- Backdrop（竖立背景平面，rim_light）-----------------------------
    // 绕 +X 轴旋转 90°：平面法线 +Y → +Z，朝向相机，形成 5×5 背景幕布。
    // 位于 z=-3.5，Y 方向从地面 (-0.5) 延伸到上方 (4.5)。
    {
        auto* tc = world.GetComponent<TransformComponent>(backdrop);
        if (tc != nullptr)
        {
            tc->position = glm::vec3(0.0f, 2.0f, -3.5f);
            tc->rotation = glm::angleAxis(glm::radians(90.0f),
                                          glm::vec3(1.0f, 0.0f, 0.0f));
        }
        RenderableComponent rc{};
        rc.mesh             = host.assets.planeMeshHandle;
        rc.materialInstance = host.assets.pRimLightMaterial.get();
        rc.visible          = true;
        rc.castsShadow      = false;
        world.AddComponent<RenderableComponent>(backdrop, rc);
    }

    // ---- Platform L（左侧平台，toon，静态刚体）--------------------------
    {
        auto* tc = world.GetComponent<TransformComponent>(platformLeft);
        if (tc != nullptr) { tc->position = glm::vec3(-2.0f, 0.0f, 0.0f); }

        RenderableComponent rc{};
        rc.mesh             = host.assets.cubeMeshHandle;
        rc.materialInstance = host.assets.pToonMaterial.get();
        rc.visible          = true;
        rc.castsShadow      = true;
        world.AddComponent<RenderableComponent>(platformLeft, rc);

        RigidBodyComponent rb{};
        rb.type          = BodyType::Static;
        rb.fixedRotation = true;
        rb.gravityScale  = 0.0f;
        world.AddComponent<RigidBodyComponent>(platformLeft, rb);

        ColliderComponent cc{};
        cc.shape    = BoxDesc{glm::vec2{0.5f, 0.5f}};
        cc.density  = 0.0f;
        cc.friction = 0.4f;
        world.AddComponent<ColliderComponent>(platformLeft, cc);
    }

    // ---- Platform R（右侧平台，toon，静态刚体）--------------------------
    {
        auto* tc = world.GetComponent<TransformComponent>(platformRight);
        if (tc != nullptr) { tc->position = glm::vec3(2.0f, 0.0f, 0.0f); }

        RenderableComponent rc{};
        rc.mesh             = host.assets.cubeMeshHandle;
        rc.materialInstance = host.assets.pToonMaterial.get();
        rc.visible          = true;
        rc.castsShadow      = true;
        world.AddComponent<RenderableComponent>(platformRight, rc);

        RigidBodyComponent rb{};
        rb.type          = BodyType::Static;
        rb.fixedRotation = true;
        rb.gravityScale  = 0.0f;
        world.AddComponent<RigidBodyComponent>(platformRight, rb);

        ColliderComponent cc{};
        cc.shape    = BoxDesc{glm::vec2{0.5f, 0.5f}};
        cc.density  = 0.0f;
        cc.friction = 0.4f;
        world.AddComponent<ColliderComponent>(platformRight, cc);
    }

    // ---- Tower（高塔，scale Y×2，toon，静态刚体）-----------------------
    // scale(1,2,1) → 实际半高 1.0；center y=0.5 → 底部 y=-0.5（齐地面）。
    {
        auto* tc = world.GetComponent<TransformComponent>(tower);
        if (tc != nullptr)
        {
            tc->position = glm::vec3(0.0f, 0.5f, -1.0f);
            tc->scale    = glm::vec3(1.0f, 2.0f, 1.0f);
        }
        RenderableComponent rc{};
        rc.mesh             = host.assets.cubeMeshHandle;
        rc.materialInstance = host.assets.pToonMaterial.get();
        rc.visible          = true;
        rc.castsShadow      = true;
        world.AddComponent<RenderableComponent>(tower, rc);

        RigidBodyComponent rb{};
        rb.type          = BodyType::Static;
        rb.fixedRotation = true;
        rb.gravityScale  = 0.0f;
        world.AddComponent<RigidBodyComponent>(tower, rb);

        // 物理碰撞盒需与 scale 后的实际半尺寸匹配
        ColliderComponent cc{};
        cc.shape    = BoxDesc{glm::vec2{0.5f, 1.0f}};
        cc.density  = 0.0f;
        cc.friction = 0.5f;
        world.AddComponent<ColliderComponent>(tower, cc);
    }

    // ---- Glow Box（dissolve 溶解方块，自动 pingpong 动画）---------------
    // dissolve shader 从 light UBO 的 uFrameInfo.x 自驱 dissolve_t，
    // 不需要 game 代码手动驱动——Edit 模式下即可看到溶解 + 发光边沿效果。
    {
        auto* tc = world.GetComponent<TransformComponent>(glowBox);
        if (tc != nullptr) { tc->position = glm::vec3(0.8f, 0.0f, 0.5f); }

        RenderableComponent rc{};
        rc.mesh             = host.assets.cubeMeshHandle;
        rc.materialInstance = host.assets.pDissolveMaterial.get();
        rc.visible          = true;
        rc.castsShadow      = false;
        world.AddComponent<RenderableComponent>(glowBox, rc);
    }

    // ---- Emissive Pillar（自发光细柱，emissive，HDR→bloom）--------------
    // scale(0.5,2.5,0.5) → 细高柱；center y=0.75 → 底部 y=-0.5（齐地面）。
    {
        auto* tc = world.GetComponent<TransformComponent>(emissivePillar);
        if (tc != nullptr)
        {
            tc->position = glm::vec3(-1.2f, 0.75f, 1.0f);
            tc->scale    = glm::vec3(0.5f, 2.5f, 0.5f);
        }
        RenderableComponent rc{};
        rc.mesh             = host.assets.cubeMeshHandle;
        rc.materialInstance = host.assets.pLightObjectMaterial.get();
        rc.visible          = true;
        rc.castsShadow      = false;
        world.AddComponent<RenderableComponent>(emissivePillar, rc);
    }

    // ---- Dynamic Box（演示 Play Mode 物理：悬空下落，落到地面上）---------
    // 位于 y=4，正上方无遮挡；Play 后受重力下落，碰 Ground 静止。
    // 使用 toon 材质，castsShadow=true，视觉上与静态台面区分。
    {
        auto* tc = world.GetComponent<TransformComponent>(dynamicBox);
        if (tc != nullptr) { tc->position = glm::vec3(0.0f, 4.0f, 0.0f); }

        RenderableComponent rc{};
        rc.mesh             = host.assets.cubeMeshHandle;
        rc.materialInstance = host.assets.pToonMaterial.get();
        rc.visible          = true;
        rc.castsShadow      = true;
        world.AddComponent<RenderableComponent>(dynamicBox, rc);

        RigidBodyComponent rb{};
        rb.type          = BodyType::Dynamic;
        rb.fixedRotation = false;
        rb.gravityScale  = 1.0f;
        rb.linearDamping = 0.05f;
        world.AddComponent<RigidBodyComponent>(dynamicBox, rb);

        ColliderComponent cc{};
        cc.shape    = BoxDesc{glm::vec2{0.5f, 0.5f}};
        cc.density  = 1.0f;
        cc.friction = 0.5f;
        world.AddComponent<ColliderComponent>(dynamicBox, cc);
    }

    // ---- Fire Emitter（火焰粒子：暖橙 HDR，bloom 自动触发光晕）----------
    {
        auto* tc = world.GetComponent<TransformComponent>(fireEmitter);
        if (tc != nullptr) { tc->position = glm::vec3(1.5f, -0.3f, 0.5f); }

        ParticleEmitterComponent pec{};
        pec.emitting               = true;
        pec.desc.emissionRate      = 30.0f;
        pec.desc.lifetimeMin       = 0.6f;
        pec.desc.lifetimeMax       = 1.2f;
        pec.desc.spawnOffsetMin    = glm::vec2{-0.12f, 0.0f};
        pec.desc.spawnOffsetMax    = glm::vec2{ 0.12f, 0.0f};
        pec.desc.initialVelocityMin = glm::vec2{-0.25f, 1.2f};
        pec.desc.initialVelocityMax = glm::vec2{ 0.25f, 2.2f};
        pec.desc.gravity           = glm::vec2{0.0f, -0.4f};
        pec.desc.colorStart        = glm::vec4{1.6f, 0.75f, 0.1f, 2.2f};  // HDR 橙黄
        pec.desc.colorEnd          = glm::vec4{0.7f, 0.15f, 0.0f, 0.0f};  // 红色熄灭
        pec.desc.sizeStart         = 0.04f;
        pec.desc.sizeEnd           = 0.09f;
        pec.desc.maxParticles      = 128u;
        world.AddComponent<ParticleEmitterComponent>(fireEmitter, pec);
    }

    // ---- Sparkle Emitter（萤火粒子：蓝白 HDR，飘浮上升）----------------
    {
        auto* tc = world.GetComponent<TransformComponent>(sparkleEmitter);
        if (tc != nullptr) { tc->position = glm::vec3(-1.5f, 1.2f, 0.8f); }

        ParticleEmitterComponent pec{};
        pec.emitting               = true;
        pec.desc.emissionRate      = 10.0f;
        pec.desc.lifetimeMin       = 2.0f;
        pec.desc.lifetimeMax       = 3.5f;
        pec.desc.spawnOffsetMin    = glm::vec2{-0.5f, -0.3f};
        pec.desc.spawnOffsetMax    = glm::vec2{ 0.5f,  0.3f};
        pec.desc.initialVelocityMin = glm::vec2{-0.15f, 0.05f};
        pec.desc.initialVelocityMax = glm::vec2{ 0.15f, 0.35f};
        pec.desc.gravity           = glm::vec2{0.0f, 0.08f};   // 轻微上浮
        pec.desc.colorStart        = glm::vec4{0.7f, 0.9f, 3.0f, 3.5f};  // HDR 蓝白（强 bloom）
        pec.desc.colorEnd          = glm::vec4{0.3f, 0.5f, 1.0f, 0.0f};  // 蓝色消散
        pec.desc.sizeStart         = 0.025f;
        pec.desc.sizeEnd           = 0.055f;
        pec.desc.maxParticles      = 64u;
        world.AddComponent<ParticleEmitterComponent>(sparkleEmitter, pec);
    }

    // ---- Slime Doll（procedural Animator + Renderable）-------------------
    // "编辑器内动画播放"端到端可见 demo：sphere + pAnimatedMaterial（pbr 模板），
    // 挂 procedural Animator。Enter Play → EditorRenderLayer 每帧 Tick animator →
    // ProceduralAnimator 覆写 pAnimatedMaterial 的 uBaseColor → Pipeline pbr 路径
    // 每帧读 override 进 push constant → 球做绿色呼吸脉动。位置贴在 Glow Box 上方。
    {
        auto* tc = world.GetComponent<TransformComponent>(slimeDoll);
        if (tc != nullptr) { tc->position = glm::vec3(0.8f, 1.6f, 0.5f); }

        // Renderable —— sphere mesh + 动画专属材质。pAnimatedMaterial 为空（pbr
        // 模板未注册等极端情况）时退化为不挂 Renderable，仅保留 Animator 段。
        if (host.assets.pAnimatedMaterial != nullptr)
        {
            RenderableComponent rc{};
            rc.mesh             = host.assets.sphereMeshHandle;
            rc.materialInstance = host.assets.pAnimatedMaterial.get();
            rc.visible          = true;
            rc.castsShadow      = true;
            world.AddComponent<RenderableComponent>(slimeDoll, rc);
        }

        AnimatorComponent ac{};
        if (host.assets.pAnimators != nullptr)
        {
            // factory 已把 target 设为 pAnimatedMaterial + 注册 uBaseColor 呼吸
            // channel（见 BuiltinAssets.cpp AnimatorRegistry 段）。
            ac.animator = host.assets.pAnimators->Create("procedural");
        }
        world.AddComponent<AnimatorComponent>(slimeDoll, std::move(ac));
    }

    // ---- Animated Cube（ClipAnimator + Renderable）----------------------
    // B2.2 端到端可见 demo：cube + 标准 toon 材质，挂数据驱动的 ClipAnimator。
    // Enter Play → TickAnimators 每帧 Tick → ClipAnimator 采样 AnimationClip 的
    // position.y / rotation.euler 轨道写进本地 Transform → cube 上下浮动 + 绕 Y
    // 自旋。与 Slime Doll（ProceduralAnimator 写 material uniform）对照：这一条
    // 是写 Transform 的路径。clip 数据随 scene 序列化（"clip" backend 形态 B 嵌入），
    // Enter-Play 快照 / Stop 还原后由 SceneSerialization 重建 + 自动接回本实体
    // Transform，故跨 Play 循环稳健。位置贴在 Slime Doll 左侧对称处。
    {
        namespace Anim = ::Orange::Engine::Animation;

        constexpr float baseY = 1.4f;
        auto*           tc    = world.GetComponent<TransformComponent>(clipCube);
        if (tc != nullptr) { tc->position = glm::vec3(-0.8f, baseY, 0.5f); }

        if (host.assets.pToonMaterial != nullptr)
        {
            RenderableComponent rc{};
            rc.mesh             = host.assets.cubeMeshHandle;
            rc.materialInstance = host.assets.pToonMaterial.get();
            rc.visible          = true;
            rc.castsShadow      = true;
            world.AddComponent<RenderableComponent>(clipCube, rc);
        }

        // 程序化构造一个 2s loop 的 bob+spin clip（实际工程里 clip 来自 timeline
        // 编辑 + .anim 资产；此处内联仅为 demo 可见）。
        auto floatKey = [](float t, float v) {
            Anim::Keyframe k;
            k.time  = t;
            k.value = glm::vec4(v, 0.0f, 0.0f, 0.0f);
            return k;
        };
        auto vec3Key = [](float t, glm::vec3 v) {
            Anim::Keyframe k;
            k.time  = t;
            k.value = glm::vec4(v, 0.0f);
            return k;
        };

        Anim::AnimationClip clip;
        clip.name     = "cube_bob_spin";
        clip.duration = 2.0f;
        clip.loop     = true;

        Anim::AnimationTrack bob;
        bob.targetName = "position.y";
        bob.valueType  = Anim::TrackValueType::Float;
        bob.keys.push_back(floatKey(0.0f, baseY));
        bob.keys.push_back(floatKey(1.0f, baseY + 0.5f));
        bob.keys.push_back(floatKey(2.0f, baseY));
        clip.tracks.push_back(std::move(bob));

        Anim::AnimationTrack spin;
        spin.targetName = "rotation.euler";
        spin.valueType  = Anim::TrackValueType::Vec3;
        spin.keys.push_back(vec3Key(0.0f, glm::vec3(0.0f)));
        spin.keys.push_back(vec3Key(2.0f, glm::vec3(0.0f, 360.0f, 0.0f)));
        clip.tracks.push_back(std::move(spin));

        AnimatorComponent ac{};
        ac.animator = std::make_unique<ClipAnimator>(
            std::move(clip), world.GetComponent<TransformComponent>(clipCube));
        world.AddComponent<AnimatorComponent>(clipCube, std::move(ac));
    }

    // ---- Test Fighter（HealthComponent 验收前置实体）----------------------
    // 演示 extraSerializers 扩展点的 Save/Load round-trip：挂非默认 hp 值，
    // Save → 重启 → Load 后 Inspector 应显示相同数值。无 Renderable / 物理体，
    // 仅作 Inspector 样本。
    {
        auto* tc = world.GetComponent<TransformComponent>(testFighter);
        if (tc != nullptr) { tc->position = glm::vec3(-8.0f, 0.5f, 0.0f); }

        DemoGame::HealthComponent health{};
        health.hp    = 75;
        health.maxHp = 100;
        world.AddComponent<DemoGame::HealthComponent>(testFighter, health);
    }

    // ---- Static Circle (demo)（Circle Collider 验收前置实体）-------------
    // 放右侧远端 (x=8) 避免与主场景视觉冲突；无 Renderable，仅供 Inspector
    // 段渲染样本。RigidBody Static + 与 Collider 配对，Load 时 PhysicsWorld
    // 能完整 AddBody（不出现 "single component" 警告）。
    {
        auto* tc = world.GetComponent<TransformComponent>(staticCircle);
        if (tc != nullptr) { tc->position = glm::vec3(8.0f, -0.5f, 0.0f); }

        RigidBodyComponent rb{};
        rb.type          = BodyType::Static;
        rb.fixedRotation = true;
        rb.gravityScale  = 0.0f;
        world.AddComponent<RigidBodyComponent>(staticCircle, rb);

        ColliderComponent cc{};
        cc.shape    = CircleDesc{/*radius=*/0.5f, /*center=*/glm::vec2{0.0f, 0.0f}};
        cc.density  = 0.0f;
        cc.friction = 0.5f;
        world.AddComponent<ColliderComponent>(staticCircle, cc);
    }

    // ---- Static Polygon (demo)（Polygon Collider 验收前置实体）-----------
    // 4 顶点凸四边形（梯形），验证 Polygon shape schema 段在 Inspector 渲染。
    {
        auto* tc = world.GetComponent<TransformComponent>(staticPolygon);
        if (tc != nullptr) { tc->position = glm::vec3(8.5f, -0.5f, 0.0f); }

        RigidBodyComponent rb{};
        rb.type          = BodyType::Static;
        rb.fixedRotation = true;
        rb.gravityScale  = 0.0f;
        world.AddComponent<RigidBodyComponent>(staticPolygon, rb);

        PolygonDesc pd{};
        pd.count = 4u;
        pd.vertices[0] = glm::vec2{-0.4f, -0.3f};
        pd.vertices[1] = glm::vec2{ 0.4f, -0.3f};
        pd.vertices[2] = glm::vec2{ 0.3f,  0.3f};
        pd.vertices[3] = glm::vec2{-0.3f,  0.3f};

        ColliderComponent cc{};
        cc.shape    = pd;
        cc.density  = 0.0f;
        cc.friction = 0.5f;
        world.AddComponent<ColliderComponent>(staticPolygon, cc);
    }

    // ---- Static EdgeChain (demo)（EdgeChain Collider 验收前置实体）-------
    // 4 顶点开放折线（不闭环），验证 EdgeChain shape schema 段在 Inspector
    // 渲染 + isLoop 字段。
    {
        auto* tc = world.GetComponent<TransformComponent>(staticEdgeChain);
        if (tc != nullptr) { tc->position = glm::vec3(9.0f, -0.5f, 0.0f); }

        RigidBodyComponent rb{};
        rb.type          = BodyType::Static;
        rb.fixedRotation = true;
        rb.gravityScale  = 0.0f;
        world.AddComponent<RigidBodyComponent>(staticEdgeChain, rb);

        EdgeChainDesc ed{};
        ed.count = 4u;
        ed.vertices[0] = glm::vec2{-0.5f,  0.0f};
        ed.vertices[1] = glm::vec2{-0.2f,  0.3f};
        ed.vertices[2] = glm::vec2{ 0.2f,  0.3f};
        ed.vertices[3] = glm::vec2{ 0.5f,  0.0f};
        ed.isLoop = false;

        ColliderComponent cc{};
        cc.shape    = ed;
        cc.density  = 0.0f;
        cc.friction = 0.5f;
        world.AddComponent<ColliderComponent>(staticEdgeChain, cc);
    }

    // ---- 构建父子层级 ---------------------------------------------------
    EditorHierarchy::LinkAsLastChild(world, root,     camera);
    EditorHierarchy::LinkAsLastChild(world, root,     sun);
    EditorHierarchy::LinkAsLastChild(world, root,     geometry);
    EditorHierarchy::LinkAsLastChild(world, geometry, ground);
    EditorHierarchy::LinkAsLastChild(world, geometry, backdrop);
    EditorHierarchy::LinkAsLastChild(world, geometry, platformLeft);
    EditorHierarchy::LinkAsLastChild(world, geometry, platformRight);
    EditorHierarchy::LinkAsLastChild(world, geometry, tower);
    EditorHierarchy::LinkAsLastChild(world, geometry, glowBox);
    EditorHierarchy::LinkAsLastChild(world, geometry, emissivePillar);
    EditorHierarchy::LinkAsLastChild(world, geometry, dynamicBox);
    EditorHierarchy::LinkAsLastChild(world, geometry, fireEmitter);
    EditorHierarchy::LinkAsLastChild(world, geometry, sparkleEmitter);
    EditorHierarchy::LinkAsLastChild(world, geometry, slimeDoll);
    EditorHierarchy::LinkAsLastChild(world, geometry, testFighter);
    EditorHierarchy::LinkAsLastChild(world, geometry, staticCircle);
    EditorHierarchy::LinkAsLastChild(world, geometry, staticPolygon);
    EditorHierarchy::LinkAsLastChild(world, geometry, staticEdgeChain);
}

// PBR showcase scene 种植：与 sample 13_pbr_direct / 14_pbr_ibl 同款 3×3 球
// 阵布局，但放成两组并排——左侧 warm 暖橙（对应 13_pbr_direct），右侧
// white furnace（对应 14_pbr_ibl）。Camera 正前方 + Sun 暖光 + Environment
// 占位 entity（cubemap 留空，用户拖 HDR 进去激活 IBL）。
//
// 球阵布局：每组 3×3，spacing 1.4，组间留 ~1.4 间距让两组明显分开。
// 行（Y 自下而上）= metallic [0.0, 0.5, 1.0]；列（X 自左向右）= roughness
// [0.1, 0.5, 0.9]。两组共 18 球，与 InitializeEditorAssets 内 lazy bake 的
// 18 个 MaterialInstance 一一对应（路径键 warm_m{0..2}r{0..2} / white_m{0..2}r{0..2}）。
void SeedPbrShowcaseWorld(Orange::Engine::World& targetWorld,
                          const EditorAssetContext& assets)
{
    using ::Orange::Engine::Entity;
    using ::Orange::Engine::Scene::NameComponent;
    using ::Orange::Engine::Scene::TransformComponent;
    using ::Orange::Engine::Render::Camera;
    using ::Orange::Engine::Render::DirectionalLight;
    using ::Orange::Engine::Render::EnvironmentComponent;
    using ::Orange::Engine::Render::RenderableComponent;

    auto& world = targetWorld;
    auto make = [&](const char* name) -> Entity {
        Entity e = world.CreateEntity();
        world.AddComponent<NameComponent>(e, NameComponent{name});
        world.AddComponent<TransformComponent>(e, TransformComponent{});
        return e;
    };

    Entity root        = make("Root");
    Entity camera      = make("Camera");
    Entity sun         = make("Sun");
    Entity environment = make("Environment");
    Entity warmGroup   = make("Warm Spheres (13_pbr_direct)");
    Entity whiteGroup  = make("White Spheres (14_pbr_ibl furnace)");

    // Camera：正前方稍高俯视，与 sample 14_pbr_ibl 视角同款 + 拉远适应两组
    // 并排球阵的宽度。
    {
        Camera cam = Camera::Perspective(glm::radians(40.0f), 1.0f, 0.1f, 100.0f);
        cam.view = glm::lookAt(glm::vec3(0.0f, 0.3f, 8.0f),
                               glm::vec3(0.0f, 0.0f, 0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent<Camera>(camera, cam);
    }

    // Sun：暖光平行光从右上前斜下打，与 demo 同款方向。
    {
        auto* tc = world.GetComponent<TransformComponent>(sun);
        if (tc != nullptr)
        {
            tc->position = glm::vec3(5.0f, 8.0f, 5.0f);
            tc->rotation = ::Orange::Engine::Render::
                MakeDirectionalLightRotationFromDir(
                    glm::vec3(0.4f, -1.0f, 0.3f));
        }
        DirectionalLight dl{};
        dl.color       = glm::vec3(1.0f, 0.95f, 0.85f);  // 偏白暖色，furnace 球阵不偏色
        dl.intensity   = 1.2f;
        dl.castsShadow = false;  // showcase 无地面，不需要阴影
        world.AddComponent<DirectionalLight>(sun, dl);
    }

    // Environment：默认空 cubemap。用户拖 HDR 进去后 Pipeline auto re-bake，
    // 14_pbr_ibl 那组 furnace 球会显示真实 IBL specular 反射。
    {
        EnvironmentComponent env{};
        world.AddComponent<EnvironmentComponent>(environment, env);
    }

    // 18 个球 entity：左 warm 9 + 右 white 9
    constexpr float kSphereSpacing = 1.4f;
    // 两组中心 X 距离 = 单组宽 (2 * spacing) + 组间 spacing = 4.2 → 左右对称
    // 让中心 0 ≈ 两组中间。
    constexpr float kGroupOffset = 2.5f;

    auto spawnSphereGrid =
        [&](Entity parent, const char* variantKey, float groupCenterX,
            std::size_t materialBaseIndex)
        {
            for (std::size_t row = 0; row < 3; ++row)
            {
                for (std::size_t col = 0; col < 3; ++col)
                {
                    char name[64];
                    std::snprintf(name, sizeof(name), "%s m%zur%zu",
                                  variantKey, row, col);
                    Entity sphere = make(name);

                    auto* tc = world.GetComponent<TransformComponent>(sphere);
                    if (tc != nullptr)
                    {
                        tc->position = glm::vec3(
                            groupCenterX + (static_cast<float>(col) - 1.0f) * kSphereSpacing,
                            (static_cast<float>(row) - 1.0f) * kSphereSpacing,
                            0.0f);
                    }

                    RenderableComponent rc{};
                    rc.mesh    = assets.sphereMeshHandle;
                    rc.visible = true;
                    rc.castsShadow = false;
                    const std::size_t matIdx = materialBaseIndex + row * 3 + col;
                    if (matIdx < assets.pbrShowcaseMaterials.size())
                    {
                        rc.materialInstance = assets.pbrShowcaseMaterials[matIdx].get();
                    }
                    world.AddComponent<RenderableComponent>(sphere, rc);

                    EditorHierarchy::LinkAsLastChild(world, parent, sphere);
                }
            }
        };

    spawnSphereGrid(warmGroup,  "warm",  -kGroupOffset, /*matBase=*/0);
    spawnSphereGrid(whiteGroup, "white",  kGroupOffset, /*matBase=*/9);

    EditorHierarchy::LinkAsLastChild(world, root, camera);
    EditorHierarchy::LinkAsLastChild(world, root, sun);
    EditorHierarchy::LinkAsLastChild(world, root, environment);
    EditorHierarchy::LinkAsLastChild(world, root, warmGroup);
    EditorHierarchy::LinkAsLastChild(world, root, whiteGroup);
}
