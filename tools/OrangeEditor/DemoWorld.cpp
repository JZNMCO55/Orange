// DemoWorld 实现 —— 见 DemoWorld.h 的注释。

#include "DemoWorld.h"

#include "EditorHierarchy.h"

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/ColliderDesc.h>
#include <orange/engine/physics/RigidBodyComponent.h>
#include <orange/engine/render/Camera.h>
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

#include <cstdio>
#include <utility>
#include <vector>

std::unique_ptr<Orange::Engine::Asset::MeshAsset>
MakePlaneMesh(float halfSize)
{
    using ::Orange::Engine::Asset::MeshAsset;
    using ::Orange::Engine::Asset::VertexPosition3;
    using ::Orange::Engine::Asset::VertexUV2;

    std::vector<VertexPosition3> positions = {
        {-halfSize, 0.0f, -halfSize},
        { halfSize, 0.0f, -halfSize},
        { halfSize, 0.0f,  halfSize},
        {-halfSize, 0.0f,  halfSize},
    };
    std::vector<VertexUV2> uvs = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
    };
    std::vector<std::uint32_t> indices = {0, 2, 1, 0, 3, 2};
    return std::make_unique<MeshAsset>(std::move(positions),
                                       std::move(uvs),
                                       std::move(indices));
}

// 内置 cube mesh（6 面 × 4 顶点，共 24 vertices / 12 triangles）。每面单独
// 一组顶点是为了让 UV 在 face 边界不连续 —— textured material 在 face 间
// 看起来才正常（共享 8 顶点的方案 UV 必然拉伸 / 接缝错位）。
std::unique_ptr<Orange::Engine::Asset::MeshAsset>
MakeCubeMesh(float halfSize)
{
    using ::Orange::Engine::Asset::MeshAsset;
    using ::Orange::Engine::Asset::VertexPosition3;
    using ::Orange::Engine::Asset::VertexUV2;

    const float h = halfSize;
    std::vector<VertexPosition3> positions;
    std::vector<VertexUV2>       uvs;
    std::vector<std::uint32_t>   indices;
    positions.reserve(24);
    uvs.reserve(24);
    indices.reserve(36);

    auto addFace = [&](VertexPosition3 a, VertexPosition3 b,
                       VertexPosition3 c, VertexPosition3 d) {
        const std::uint32_t base = static_cast<std::uint32_t>(positions.size());
        positions.push_back(a); positions.push_back(b);
        positions.push_back(c); positions.push_back(d);
        uvs.push_back({0.0f, 0.0f}); uvs.push_back({1.0f, 0.0f});
        uvs.push_back({1.0f, 1.0f}); uvs.push_back({0.0f, 1.0f});
        indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 1);
        indices.push_back(base + 0); indices.push_back(base + 3); indices.push_back(base + 2);
    };

    // +X / -X / +Y / -Y / +Z / -Z；winding 与既有 sample 的 plane 同顺
    // （CCW 朝外），避免与 shadow caster / 主 pass 的 CullMode 假设打架。
    addFace({ h,-h, h}, { h,-h,-h}, { h, h,-h}, { h, h, h});  // +X
    addFace({-h,-h,-h}, {-h,-h, h}, {-h, h, h}, {-h, h,-h});  // -X
    addFace({-h, h, h}, { h, h, h}, { h, h,-h}, {-h, h,-h});  // +Y (top)
    addFace({-h,-h,-h}, { h,-h,-h}, { h,-h, h}, {-h,-h, h});  // -Y (bottom)
    addFace({-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h});  // +Z
    addFace({ h,-h,-h}, {-h,-h,-h}, {-h, h,-h}, { h, h,-h});  // -Z

    return std::make_unique<MeshAsset>(std::move(positions),
                                       std::move(uvs),
                                       std::move(indices));
}

// 一次性建好 AssetRegistry + 注册 ShaderLoader + 内置 mesh + MaterialSystem
// + 所有内置材质实例。失败仅 log，不抛；SeedDemoWorld 仍能工作（Renderable
// 退化到 nullptr material），只是 Scene 视口看不到几何。
void InitializeEditorAssets(EditorState& state)
{
    using Orange::Engine::Asset::AssetRegistry;
    using Orange::Engine::Asset::MeshAsset;
    using Orange::Engine::Asset::ShaderAsset;
    using Orange::Engine::Asset::ShaderLoader;
    using Orange::Engine::Render::MaterialSystem;

    state.pAssets = std::make_unique<AssetRegistry>();
    if (auto reg = state.pAssets->RegisterLoader<ShaderAsset>(
            std::make_unique<ShaderLoader>());
        reg.IsErr())
    {
        std::fprintf(stderr,
                     "[OrangeEditor] AssetRegistry::RegisterLoader<ShaderAsset> 失败 "
                     "(code=%u)\n",
                     static_cast<unsigned>(reg.Error()));
    }

    if (auto h = state.pAssets->Insert<MeshAsset>("editor/cube", MakeCubeMesh(0.5f));
        h.IsOk())
    {
        state.cubeMeshHandle = h.Value();
    }
    if (auto h = state.pAssets->Insert<MeshAsset>("editor/plane", MakePlaneMesh(2.5f));
        h.IsOk())
    {
        state.planeMeshHandle = h.Value();
    }

    state.pMaterials = std::make_unique<MaterialSystem>(*state.pAssets);
    if (auto rb = state.pMaterials->RegisterBuiltins(); rb.IsErr())
    {
        // 通常意味着 shaders/orange_engine/*.spv 不在 .exe 同目录——in-tree
        // build 由 CMake 把 SPV 拷到 build/bin/$<CONFIG>/shaders/orange_engine/，
        // standalone install 还没有官方流程时这里会报，但不阻止编辑器启动。
        std::fprintf(stderr,
                     "[OrangeEditor] MaterialSystem::RegisterBuiltins 失败 "
                     "(code=%u) —— Scene 视口稍后可能不显示几何\n",
                     static_cast<unsigned>(rb.Error()));
    }

    // 地面 / 备用 textured 实例
    state.pFloorMaterial = state.pMaterials->CreateInstance("textured");
    state.pWallMaterial  = state.pMaterials->CreateInstance("textured");

    // v0.1.5 新增内置材质实例（失败时 unique_ptr 为 nullptr，Renderable 降级）
    state.pToonMaterial     = state.pMaterials->CreateInstance("toon");
    state.pRimLightMaterial = state.pMaterials->CreateInstance("rim_light");
    state.pDissolveMaterial = state.pMaterials->CreateInstance("dissolve");

    // 编辑器操作共用默认材质
    state.pDefaultRenderableMaterial = state.pMaterials->CreateInstance("textured");
    state.pLightObjectMaterial       = state.pMaterials->CreateInstance("emissive");
}

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
//       ├── Fire Emitter （粒子：火焰，暖橙 HDR → bloom）
//       └── Sparkle Emitter（粒子：萤火，蓝白 HDR → bloom）
void SeedDemoWorld(EditorState& state)
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
    using ::Orange::Engine::Physics::RigidBodyComponent;

    auto& world = *state.pWorld;
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
    Entity fireEmitter     = make("Fire Emitter");
    Entity sparkleEmitter  = make("Sparkle Emitter");

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
        if (tc != nullptr) { tc->position = glm::vec3(5.0f, 8.0f, 5.0f); }

        DirectionalLight dl{};
        dl.direction   = glm::normalize(glm::vec3(0.4f, -1.0f, 0.3f));
        dl.color       = glm::vec3(1.0f, 0.93f, 0.78f);  // 暖黄阳光
        dl.intensity   = 1.3f;
        dl.castsShadow = true;  // 开启软阴影（Phase 3 Task 05 已落地）
        world.AddComponent<DirectionalLight>(sun, dl);
    }

    // ---- Ground（大平面，textured，静态刚体）----------------------------
    {
        auto* tc = world.GetComponent<TransformComponent>(ground);
        if (tc != nullptr) { tc->position.y = -0.5f; }

        RenderableComponent rc{};
        rc.mesh             = state.planeMeshHandle;
        rc.materialInstance = state.pFloorMaterial.get();
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
        rc.mesh             = state.planeMeshHandle;
        rc.materialInstance = state.pRimLightMaterial.get();
        rc.visible          = true;
        rc.castsShadow      = false;
        world.AddComponent<RenderableComponent>(backdrop, rc);
    }

    // ---- Platform L（左侧平台，toon，静态刚体）--------------------------
    {
        auto* tc = world.GetComponent<TransformComponent>(platformLeft);
        if (tc != nullptr) { tc->position = glm::vec3(-2.0f, 0.0f, 0.0f); }

        RenderableComponent rc{};
        rc.mesh             = state.cubeMeshHandle;
        rc.materialInstance = state.pToonMaterial.get();
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
        rc.mesh             = state.cubeMeshHandle;
        rc.materialInstance = state.pToonMaterial.get();
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
        rc.mesh             = state.cubeMeshHandle;
        rc.materialInstance = state.pToonMaterial.get();
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
        rc.mesh             = state.cubeMeshHandle;
        rc.materialInstance = state.pDissolveMaterial.get();
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
        rc.mesh             = state.cubeMeshHandle;
        rc.materialInstance = state.pLightObjectMaterial.get();
        rc.visible          = true;
        rc.castsShadow      = false;
        world.AddComponent<RenderableComponent>(emissivePillar, rc);
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
    EditorHierarchy::LinkAsLastChild(world, geometry, fireEmitter);
    EditorHierarchy::LinkAsLastChild(world, geometry, sparkleEmitter);
}
