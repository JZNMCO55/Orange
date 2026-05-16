// DemoWorld 实现 —— 见 DemoWorld.h 的注释。

#include "DemoWorld.h"

#include "EditorHierarchy.h"
#include "demo_game/HealthComponent.h"

#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/animation/AnimatorRegistry.h>
#include <orange/engine/animation/ProceduralAnimator.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshLoader.h>
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
#include <filesystem>
#include <string>
#include <unordered_map>
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
void InitializeEditorAssets(EditorHost& host)
{
    using Orange::Engine::Asset::AssetRegistry;
    using Orange::Engine::Asset::MeshAsset;
    using Orange::Engine::Asset::ShaderAsset;
    using Orange::Engine::Asset::ShaderLoader;
    using Orange::Engine::Render::MaterialSystem;

    host.assets.pAssets = std::make_unique<AssetRegistry>();
    if (auto reg = host.assets.pAssets->RegisterLoader<ShaderAsset>(
            std::make_unique<ShaderLoader>());
        reg.IsErr())
    {
        std::fprintf(stderr,
                     "[OrangeEditor] AssetRegistry::RegisterLoader<ShaderAsset> 失败 "
                     "(code=%u)\n",
                     static_cast<unsigned>(reg.Error()));
    }
    // GAP-2026-05-16 G1：注册 MeshLoader 让 RenderableComponent.mesh 字段
    // 走 "assets/meshes/*.mesh" 磁盘路径 Load 路径（取代旧的内存 named
    // "editor/cube" Insert 路径）。MeshLoader v2 支持 UV 段（同 commit 落
    // 地的引擎扩展），textured / toon material 在烘焙后的 .mesh 上 UV 不
    // 丢失。
    using Orange::Engine::Asset::MeshLoader;
    if (auto reg = host.assets.pAssets->RegisterLoader<MeshAsset>(
            std::make_unique<MeshLoader>());
        reg.IsErr())
    {
        std::fprintf(stderr,
                     "[OrangeEditor] AssetRegistry::RegisterLoader<MeshAsset> 失败 "
                     "(code=%u)\n",
                     static_cast<unsigned>(reg.Error()));
    }

    // 内置 mesh lazy bake：检测 assets/meshes/X.mesh，缺失则程序化构造 +
    // MeshLoader::Save 写盘后再 Load；存在直接 Load。lazy bake 让首次跑
    // OrangeEditor 自动产出 .mesh 文件让开发者手动 git add commit 入仓；
    // 之后 CI 跑或其他人拉仓直接走盘上的 .mesh。
    //
    // 不在 InitializeEditorAssets 内 fall back 到 Insert("editor/cube",...)
    // 路径——那是 G1 之前的兼容残留，G1 ✅ 后所有 mesh 引用必须走磁盘路径。
    auto bakeIfMissingThenLoad = [&](const std::string& path,
                                     auto buildFn) -> Orange::Engine::Asset::AssetHandle<MeshAsset>
    {
        if (!std::filesystem::exists(path))
        {
            auto pMesh = buildFn();
            if (pMesh != nullptr)
            {
                // 确保目录存在；MeshLoader::Save 不创建目录。
                std::filesystem::create_directories(
                    std::filesystem::path(path).parent_path());
                if (auto sv = MeshLoader::Save(path, *pMesh); sv.IsErr())
                {
                    std::fprintf(stderr,
                                 "[OrangeEditor] MeshLoader::Save '%s' 失败 (code=%u)\n",
                                 path.c_str(),
                                 static_cast<unsigned>(sv.Error()));
                    // 仍然 Insert 一份内存版本作为最后兜底，让本次会话能继续渲染
                    if (auto h = host.assets.pAssets->Insert<MeshAsset>(path, std::move(pMesh));
                        h.IsOk())
                    {
                        return h.Value();
                    }
                    return {};
                }
            }
        }
        auto lr = host.assets.pAssets->Load<MeshAsset>(path);
        if (lr.IsErr())
        {
            std::fprintf(stderr,
                         "[OrangeEditor] Load<MeshAsset> '%s' 失败 (code=%u)\n",
                         path.c_str(),
                         static_cast<unsigned>(lr.Error()));
            return {};
        }
        return lr.Value();
    };

    host.assets.cubeMeshHandle  = bakeIfMissingThenLoad(
        "assets/meshes/cube.mesh",  [] { return MakeCubeMesh(0.5f); });
    host.assets.planeMeshHandle = bakeIfMissingThenLoad(
        "assets/meshes/plane.mesh", [] { return MakePlaneMesh(2.5f); });

    host.assets.pMaterials = std::make_unique<MaterialSystem>(*host.assets.pAssets);
    if (auto rb = host.assets.pMaterials->RegisterBuiltins(); rb.IsErr())
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
    host.assets.pFloorMaterial = host.assets.pMaterials->CreateInstance("textured");
    host.assets.pWallMaterial  = host.assets.pMaterials->CreateInstance("textured");

    // v0.1.5 新增内置材质实例（失败时 unique_ptr 为 nullptr，Renderable 降级）
    host.assets.pToonMaterial     = host.assets.pMaterials->CreateInstance("toon");
    host.assets.pRimLightMaterial = host.assets.pMaterials->CreateInstance("rim_light");
    host.assets.pDissolveMaterial = host.assets.pMaterials->CreateInstance("dissolve");

    // 编辑器操作共用默认材质
    host.assets.pDefaultRenderableMaterial = host.assets.pMaterials->CreateInstance("textured");
    host.assets.pLightObjectMaterial       = host.assets.pMaterials->CreateInstance("emissive");

    // AnimatorRegistry —— Scene::Load 遇到 AnimatorComponent 时通过 backend
    // name 查 factory 创建 IAnimator。当前只注册引擎自带 "procedural" 后端；
    // dragonbones 后端依赖 DragonBonesContext + skeleton asset，编辑器 demo
    // 暂不消费，等 v0.7 Animation 子模式上线后再注册。
    //
    // factory 捕获 dissolve material 的裸指针——pDissolveMaterial 由本
    // context 拥有，生命周期 ≥ AnimatorRegistry，指针稳定。channel 列表
    // 故意只塞一条占位 dissolve_t，目的是让 c4 IEditorInspectorPlugin
    // mini-preview 能读到 ChannelCount > 0；UBO 通路未接通前 channel 写入
    // 不会真正影响 GPU 端 uniform（参 ProceduralAnimator.h 头注释）。
    host.assets.pAnimators = std::make_unique<Orange::Engine::Animation::AnimatorRegistry>();
    {
        auto* pDissolveTarget = host.assets.pDissolveMaterial.get();
        auto factory = [pDissolveTarget]()
            -> std::unique_ptr<Orange::Engine::Animation::IAnimator>
        {
            auto anim = std::make_unique<
                Orange::Engine::Animation::ProceduralAnimator>(pDissolveTarget);
            anim->AddChannel<float>("dissolve_t",
                                    [](float t) { return t * 0.5f; });
            return anim;
        };
        if (auto rb = host.assets.pAnimators->RegisterBackend("procedural", factory);
            rb.IsErr())
        {
            std::fprintf(stderr,
                         "[OrangeEditor] AnimatorRegistry::RegisterBackend(procedural) "
                         "失败 (code=%u)\n",
                         static_cast<unsigned>(rb.Error()));
        }
    }
}

std::unordered_map<std::string, Orange::Engine::Render::MaterialInstance*>
BuildNamedMaterialInstances(const EditorAssetContext& assets)
{
    using Orange::Engine::Render::MaterialInstance;
    std::unordered_map<std::string, MaterialInstance*> m;
    if (assets.pFloorMaterial)             m["builtin/floor"]        = assets.pFloorMaterial.get();
    if (assets.pWallMaterial)              m["builtin/wall"]         = assets.pWallMaterial.get();
    if (assets.pToonMaterial)              m["builtin/toon"]         = assets.pToonMaterial.get();
    if (assets.pRimLightMaterial)          m["builtin/rim_light"]    = assets.pRimLightMaterial.get();
    if (assets.pDissolveMaterial)          m["builtin/dissolve"]     = assets.pDissolveMaterial.get();
    if (assets.pDefaultRenderableMaterial) m["builtin/default"]      = assets.pDefaultRenderableMaterial.get();
    if (assets.pLightObjectMaterial)       m["builtin/light_object"] = assets.pLightObjectMaterial.get();
    return m;
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

    // ---- Slime Doll（Animator-only，无 Renderable）-----------------------
    // 位置贴在 Glow Box 上方，方便在 Inspector 选实体时 viewport 大致定位；
    // 没有 Renderable 是刻意决定（参 entity 创建段注释）。
    {
        auto* tc = world.GetComponent<TransformComponent>(slimeDoll);
        if (tc != nullptr) { tc->position = glm::vec3(0.8f, 1.2f, 0.5f); }

        AnimatorComponent ac{};
        if (host.assets.pAnimators != nullptr)
        {
            ac.animator = host.assets.pAnimators->Create("procedural");
        }
        world.AddComponent<AnimatorComponent>(slimeDoll, std::move(ac));
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
