// samples/12_layer_partition_demo —— Scene layer / WorldPartition 演示
//
// 验收对应：docs/engine-known-gaps.md `GAP-2026-05-17-scene-layer-component`
//
// 场景拓扑：
//   * background layer：textured plane（地面）+ 静态 BoxCollider；
//     toon sphere 作为视觉锚点；DirectionalLight + Camera 也归 background。
//   * foreground layer：两个 dynamic box（rim_light Material），初始
//     y=4，受重力下落到 plane 上反弹。
//
// 自动行为：VisibilityToggleLayer 每 ~3 秒翻转 foreground.visible：
//   * Pipeline 端：RenderScene::Collect 跳过 foreground 上 drawable，
//     viewport 只剩 background plane + sphere。
//   * Physics 端：ApplyLayerVisibility 同步 SetBodyEnabled(false) →
//     foreground box 物理不再 tick，下落 / 反弹冻结在 Disable 当帧位置。
//
// 与 v1.1 单文件 scene 完全独立：sample 不写盘，纯运行时构造 partition +
// LayerComponent 验证引擎能力。SaveSplit / LoadSplit 的写盘 + reload
// 验证留给 tests / 编辑器 v0.6 后续 session 消费。
//
// 操作：无键盘交互；自动循环 visible→hide→show→...，便于截屏验收。

#include <orange/engine/app/AppConfig.h>
#include <orange/engine/app/AppHost.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/app/Layer.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/physics/BodyHandle.h>
#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/LayerVisibilitySync.h>
#include <orange/engine/physics/PhysicsWorld.h>
#include <orange/engine/physics/RigidBodyComponent.h>
#include <orange/engine/platform/WindowEvent.h>
#include <orange/engine/render/BuiltinPostProcessChain.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/PostProcessChain.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/render/ShadowConfig.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/LayerComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/scene/WorldPartition.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

using namespace Orange::Engine;
using Orange::Engine::Asset::AssetHandle;
using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Asset::MeshAsset;
using Orange::Engine::Asset::ShaderAsset;
using Orange::Engine::Asset::ShaderLoader;
using Orange::Engine::Asset::VertexPosition3;
using Orange::Engine::Asset::VertexUV2;
using Orange::Engine::Render::Camera;
using Orange::Engine::Render::DirectionalLight;
using Orange::Engine::Render::MaterialInstance;
using Orange::Engine::Render::MaterialSystem;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::PostProcessChain;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Render::ShadowConfig;
using Orange::Engine::Render::BuiltinPostProcessChain::CreateDefault;
using Orange::Engine::Scene::LayerComponent;
using Orange::Engine::Scene::LayerInfo;
using Orange::Engine::Scene::TransformComponent;
using Orange::Engine::Scene::WorldPartition;
namespace Phys = Orange::Engine::Physics;

namespace
{

    constexpr const char* kBackgroundLayerId = "background";
    constexpr const char* kForegroundLayerId = "foreground";

    // 翻转间隔：3 秒一次切换；视觉上 visible 3s → hidden 3s → ... 循环。
    constexpr float kToggleIntervalSeconds = 3.0f;

    // ---------- mesh 工厂（与其它 sample 同布局，含 UV）----------
    std::unique_ptr<MeshAsset> MakePlaneMesh(float halfSize)
    {
        std::vector<VertexPosition3> positions = {
            {-halfSize, 0.0f, -halfSize},
            {halfSize, 0.0f, -halfSize},
            {halfSize, 0.0f, halfSize},
            {-halfSize, 0.0f, halfSize},
        };
        std::vector<VertexUV2> uvs = {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
            {0.0f, 1.0f},
        };
        std::vector<std::uint32_t> indices = {0, 2, 1, 0, 3, 2};
        auto                       pMesh   = std::make_unique<MeshAsset>(std::move(positions),
                                                                         std::move(uvs),
                                                                         std::move(indices));
        pMesh->ComputeSmoothNormalsFromTriangles();
        return pMesh;
    }

    std::unique_ptr<MeshAsset> MakeCubeMesh(float halfSize)
    {
        const float h = halfSize;
        // 六面 24 顶点（每面 4 个）+ UV，让 textured / dissolve 等 UV-aware
        // material 在 cube 上有合理 mapping。
        std::vector<VertexPosition3> positions = {
            // +X
            {h, -h, -h},
            {h, h, -h},
            {h, h, h},
            {h, -h, h},
            // -X
            {-h, -h, h},
            {-h, h, h},
            {-h, h, -h},
            {-h, -h, -h},
            // +Y
            {-h, h, -h},
            {-h, h, h},
            {h, h, h},
            {h, h, -h},
            // -Y
            {-h, -h, h},
            {-h, -h, -h},
            {h, -h, -h},
            {h, -h, h},
            // +Z
            {-h, -h, h},
            {h, -h, h},
            {h, h, h},
            {-h, h, h},
            // -Z
            {h, -h, -h},
            {-h, -h, -h},
            {-h, h, -h},
            {h, h, -h},
        };
        std::vector<VertexUV2> uvs;
        uvs.reserve(positions.size());
        for (std::size_t f = 0; f < 6; ++f)
        {
            uvs.push_back({0.0f, 0.0f});
            uvs.push_back({1.0f, 0.0f});
            uvs.push_back({1.0f, 1.0f});
            uvs.push_back({0.0f, 1.0f});
        }
        std::vector<std::uint32_t> indices;
        indices.reserve(36);
        for (std::uint32_t face = 0; face < 6; ++face)
        {
            const std::uint32_t base = face * 4;
            // CCW winding 与 Pipeline FrontFace=CCW + CullMode=Back 对齐
            // （参 GAP-2026-05-22-samples-cube-mesh-winding-bug）。
            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 0);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
        }
        auto pMesh = std::make_unique<MeshAsset>(std::move(positions),
                                                 std::move(uvs),
                                                 std::move(indices));
        pMesh->ComputeSmoothNormalsFromTriangles();
        return pMesh;
    }

    std::unique_ptr<MeshAsset> MakeSphereMesh(float radius, std::uint32_t lon, std::uint32_t lat)
    {
        std::vector<VertexPosition3> positions;
        std::vector<VertexUV2>       uvs;
        std::vector<std::uint32_t>   indices;
        for (std::uint32_t i = 0; i <= lat; ++i)
        {
            const float v     = static_cast<float>(i) / static_cast<float>(lat);
            const float theta = v * glm::pi<float>();
            const float sinT  = std::sin(theta);
            const float cosT  = std::cos(theta);
            for (std::uint32_t j = 0; j <= lon; ++j)
            {
                const float u    = static_cast<float>(j) / static_cast<float>(lon);
                const float phi  = u * glm::two_pi<float>();
                const float sinP = std::sin(phi);
                const float cosP = std::cos(phi);
                positions.push_back({radius * sinT * cosP,
                                     radius * cosT,
                                     radius * sinT * sinP});
                uvs.push_back({u, 1.0f - v});
            }
        }
        for (std::uint32_t i = 0; i < lat; ++i)
        {
            for (std::uint32_t j = 0; j < lon; ++j)
            {
                const std::uint32_t a = i * (lon + 1) + j;
                const std::uint32_t b = (i + 1) * (lon + 1) + j;
                const std::uint32_t c = (i + 1) * (lon + 1) + (j + 1);
                const std::uint32_t d = i * (lon + 1) + (j + 1);
                indices.push_back(a);
                indices.push_back(c);
                indices.push_back(b);
                indices.push_back(a);
                indices.push_back(d);
                indices.push_back(c);
            }
        }
        auto pMesh = std::make_unique<MeshAsset>(std::move(positions),
                                                 std::move(uvs),
                                                 std::move(indices));
        pMesh->ComputeSmoothNormalsFromTriangles();
        return pMesh;
    }

    // ---------- VisibilityToggleLayer ----------
    //
    // 每帧累计 dt；超过 kToggleIntervalSeconds 时翻转 foreground.visible，
    // 然后调一次 ApplyLayerVisibility 把状态同步到 PhysicsWorld。
    class VisibilityToggleLayer : public Layer
    {
    public:
        VisibilityToggleLayer(WorldPartition&     partition,
                              const World&        world,
                              Phys::PhysicsWorld& physics)
            : Layer("VisibilityToggleLayer"), mPartition(partition), mWorld(world), mPhysics(physics)
        {
            // 启动期同步一次——保证 foreground.visible 与 partition manifest
            // 的 initial 状态一致下发到所有 body 上。
            Phys::ApplyLayerVisibility(mWorld, mPartition, mPhysics);
        }

        void OnUpdate(const FrameContext& frame) override
        {
            mAccum += frame.time.deltaSeconds;
            if (mAccum < kToggleIntervalSeconds)
            {
                return;
            }
            mAccum -= kToggleIntervalSeconds;

            const bool wasVisible = mPartition.IsLayerVisible(kForegroundLayerId);
            const bool now        = !wasVisible;
            mPartition.SetLayerVisible(kForegroundLayerId, now);
            Phys::ApplyLayerVisibility(mWorld, mPartition, mPhysics);

            std::fprintf(stdout,
                         "[layer_partition_demo] toggled foreground.visible -> %s\n",
                         now ? "true" : "false");
            std::fflush(stdout);
        }

    private:
        WorldPartition&     mPartition;
        const World&        mWorld;
        Phys::PhysicsWorld& mPhysics;
        float               mAccum{0.0f};
    };

    // ---------- PhysicsLayer：标准 Step + sync 路径 ----------
    class PhysicsLayer : public Layer
    {
    public:
        PhysicsLayer(Phys::PhysicsWorld& phys, World& ecs,
                     std::vector<std::pair<Entity, Phys::BodyHandle>> dynamicBodies)
            : Layer("PhysicsLayer"), mPhys(phys), mEcs(ecs), mDynamicBodies(std::move(dynamicBodies))
        {
        }

        void OnUpdate(const FrameContext& frame) override
        {
            float dt = frame.time.deltaSeconds;
            if (dt > 1.0f / 30.0f)
            {
                dt = 1.0f / 30.0f;
            }
            mPhys.Step(dt);

            for (auto& [entity, body] : mDynamicBodies)
            {
                if (!body.IsValid())
                {
                    continue;
                }
                const auto bxf = mPhys.GetBodyTransform(body);
                if (auto* xf = mEcs.GetComponent<TransformComponent>(entity))
                {
                    xf->position.x = bxf.position.x;
                    xf->position.y = bxf.position.y;
                }
            }
        }

    private:
        Phys::PhysicsWorld&                              mPhys;
        World&                                           mEcs;
        std::vector<std::pair<Entity, Phys::BodyHandle>> mDynamicBodies;
    };

    class RenderLayer : public Layer
    {
    public:
        RenderLayer(Pipeline& pipeline, World& world)
            : Layer("RenderLayer"), mPipeline(pipeline), mWorld(world)
        {
        }

        void OnUpdate(const FrameContext& /*frame*/) override
        {
            mPipeline.Render(mWorld);
        }

        bool OnEvent(const Platform::WindowEvent& event) override
        {
            if (auto* resize = std::get_if<Platform::WindowResizeEvent>(&event))
            {
                mPipeline.OnResize(resize->width, resize->height);
            }
            return false;
        }

    private:
        Pipeline& mPipeline;
        World&    mWorld;
    };

} // namespace

int main(int /*argc*/, char** /*argv*/)
{
    AppConfig cfg{};
    cfg.window.title  = "OrangeEngine - 12 layer_partition_demo";
    cfg.window.width  = 1280;
    cfg.window.height = 720;

    auto hostResult = AppHost::Create(cfg);
    if (hostResult.IsErr())
    {
        std::fprintf(stderr, "AppHost::Create failed (code=%u)\n",
                     static_cast<unsigned>(hostResult.Error()));
        return 1;
    }
    auto host = std::move(hostResult).Value();

    AssetRegistry assets;
    if (auto reg = assets.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
        reg.IsErr())
    {
        std::fprintf(stderr, "RegisterLoader<ShaderAsset> failed\n");
        return 1;
    }

    auto planeRes  = assets.Insert<MeshAsset>("builtin/plane", MakePlaneMesh(3.0f));
    auto cubeRes   = assets.Insert<MeshAsset>("builtin/cube", MakeCubeMesh(0.4f));
    auto sphereRes = assets.Insert<MeshAsset>("builtin/sphere", MakeSphereMesh(0.5f, 32, 16));
    if (planeRes.IsErr() || cubeRes.IsErr() || sphereRes.IsErr())
    {
        std::fprintf(stderr, "Insert<MeshAsset> failed\n");
        return 1;
    }
    AssetHandle<MeshAsset> planeHandle  = planeRes.Value();
    AssetHandle<MeshAsset> cubeHandle   = cubeRes.Value();
    AssetHandle<MeshAsset> sphereHandle = sphereRes.Value();

    MaterialSystem materials(assets);
    if (auto rb = materials.RegisterBuiltins(); rb.IsErr())
    {
        std::fprintf(stderr, "RegisterBuiltins failed\n");
        return 1;
    }
    auto planeInstance  = materials.CreateInstance("textured");
    auto sphereInstance = materials.CreateInstance("toon");
    auto cubeInstance   = materials.CreateInstance("rim_light");
    if (!planeInstance || !sphereInstance || !cubeInstance)
    {
        std::fprintf(stderr, "CreateInstance failed\n");
        return 1;
    }

    World          world;
    WorldPartition partition;

    // 注册两个 layer。default layer 由 WorldPartition 构造自动补；
    // 这里显式再加 background / foreground。source 字段在本 sample 不
    // 走 SaveSplit / LoadSplit 路径，留空也行，但顺手填上让"manifest
    // 写盘后什么样"对验收者直观。
    partition.AddLayer(LayerInfo{
        .id          = kBackgroundLayerId,
        .displayName = "Background",
        .visible     = true,
        .source      = "background.scene.json",
    });
    partition.AddLayer(LayerInfo{
        .id          = kForegroundLayerId,
        .displayName = "Foreground",
        .visible     = true,
        .source      = "foreground.scene.json",
    });

    // ---------- background layer entities ----------

    // 静态地面 plane（视觉 + 物理 BoxCollider）
    Entity planeEntity = world.CreateEntity();
    {
        TransformComponent xf{};
        xf.position = {0.0f, -0.5f, 0.0f};
        world.AddComponent(planeEntity, xf);
        RenderableComponent r;
        r.mesh             = planeHandle;
        r.materialInstance = planeInstance.get();
        world.AddComponent(planeEntity, r);
    }
    partition.SetLayerOf(world, planeEntity, kBackgroundLayerId);

    // 视觉锚点 sphere（无物理；hide foreground 后球仍在 → 让用户清楚
    // background layer 没被一起 hide）
    Entity sphereEntity = world.CreateEntity();
    {
        TransformComponent xf{};
        xf.position = {-1.2f, 0.0f, 0.0f};
        world.AddComponent(sphereEntity, xf);
        RenderableComponent r;
        r.mesh             = sphereHandle;
        r.materialInstance = sphereInstance.get();
        world.AddComponent(sphereEntity, r);
    }
    partition.SetLayerOf(world, sphereEntity, kBackgroundLayerId);

    // DirectionalLight + Camera 也归 background——保证 hide foreground
    // 后仍能看到正常照明的背景（不是黑屏）。
    Entity lightEntity = world.CreateEntity();
    {
        TransformComponent lightXf{};
        lightXf.rotation = Orange::Engine::Render::MakeDirectionalLightRotationFromDir(
            glm::vec3(0.2f, -1.0f, 0.3f));
        world.AddComponent(lightEntity, lightXf);

        DirectionalLight dl{};
        dl.color       = glm::vec3(1.0f, 0.95f, 0.85f);
        dl.intensity   = 1.2f;
        dl.castsShadow = true;
        world.AddComponent(lightEntity, dl);
    }
    partition.SetLayerOf(world, lightEntity, kBackgroundLayerId);

    Entity camEntity = world.CreateEntity();
    {
        const float aspect = static_cast<float>(cfg.window.width) / static_cast<float>(cfg.window.height);
        Camera      cam    = Camera::Perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        cam.view           = glm::lookAt(glm::vec3(2.5f, 2.5f, 5.0f),
                                         glm::vec3(0.0f, 0.5f, 0.0f),
                                         glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent(camEntity, cam);
    }
    partition.SetLayerOf(world, camEntity, kBackgroundLayerId);

    // ---------- Physics world ----------
    Phys::PhysicsWorld physWorld;

    // 静态地面：BoxCollider 大半 50×0.25，顶面 y=-0.5（与 visual plane 平齐）
    {
        Phys::RigidBodyComponent rb;
        rb.type            = Phys::BodyType::Static;
        rb.initialPosition = {0.0f, -0.75f};
        Phys::ColliderComponent col;
        col.shape                   = Phys::BoxDesc{{50.0f, 0.25f}, {0.0f, 0.0f}};
        col.friction                = 0.6f;
        col.restitution             = 0.0f;
        Phys::BodyHandle groundBody = physWorld.AddBody(rb, col);
        if (!groundBody.IsValid())
        {
            std::fprintf(stderr, "PhysicsWorld.AddBody(ground) failed\n");
            return 1;
        }
        // 地面 body 不挂 RigidBodyComponent 到 ECS——sample 06 也是这样
        // （静态地面纯物理）。这意味着它不参与 ApplyLayerVisibility
        // 的 ECS-driven enable/disable 路径；想让地面也按 layer 控的话
        // 把 rb / col / groundBody 写进 ECS 即可。
    }

    // ---------- foreground layer entities ----------
    //
    // 两个 dynamic box：初始 y=4，受重力下落到 plane 上反弹。
    // 二者既挂 RenderableComponent（视觉）也挂 RigidBodyComponent
    // （物理）+ LayerComponent{"foreground"}。
    std::vector<std::pair<Entity, Phys::BodyHandle>> foregroundBodies;
    const std::array<glm::vec3, 2>                   spawnPositions = {
        glm::vec3{0.6f, 4.0f, 0.0f},
        glm::vec3{1.5f, 5.0f, 0.0f},
    };
    for (const auto& spawn : spawnPositions)
    {
        Entity             boxEntity = world.CreateEntity();
        TransformComponent xf{};
        xf.position = spawn;
        world.AddComponent(boxEntity, xf);

        RenderableComponent r;
        r.mesh             = cubeHandle;
        r.materialInstance = cubeInstance.get();
        world.AddComponent(boxEntity, r);

        Phys::RigidBodyComponent rb;
        rb.type            = Phys::BodyType::Dynamic;
        rb.initialPosition = {spawn.x, spawn.y};
        rb.linearDamping   = 0.1f;

        Phys::ColliderComponent col;
        col.shape       = Phys::BoxDesc{{0.4f, 0.4f}, {0.0f, 0.0f}};
        col.density     = 1.0f;
        col.friction    = 0.5f;
        col.restitution = 0.45f;

        Phys::BodyHandle handle = physWorld.AddBody(rb, col);
        if (!handle.IsValid())
        {
            std::fprintf(stderr, "PhysicsWorld.AddBody(foreground cube) failed\n");
            return 1;
        }
        rb.handle = handle;
        world.AddComponent(boxEntity, rb);
        world.AddComponent(boxEntity, col);
        partition.SetLayerOf(world, boxEntity, kForegroundLayerId);

        foregroundBodies.emplace_back(boxEntity, handle);
    }

    // ---------- Pipeline ----------
    Pipeline pipeline;
    if (auto r = pipeline.Initialize(host->GetWindow(), assets); r.IsErr())
    {
        std::fprintf(stderr, "Pipeline::Initialize failed (code=%u)\n",
                     static_cast<unsigned>(r.Error()));
        return 1;
    }
    PostProcessChain chain = CreateDefault();
    pipeline.SetPostProcessChain(&chain);
    pipeline.SetMaterialSystem(&materials);
    pipeline.SetShadowConfig(ShadowConfig{});
    pipeline.SetWorldPartition(&partition);

    // Layer 顺序：visibility toggle 最先（决定本帧 visibility）→ physics
    // step（按当前 enable 状态决定 body 受重力情况）→ render（pipeline
    // 用最新 visibility 过滤 drawable）。
    host->PushLayer(std::make_unique<VisibilityToggleLayer>(partition, world, physWorld));
    host->PushLayer(std::make_unique<PhysicsLayer>(physWorld, world, std::move(foregroundBodies)));
    host->PushLayer(std::make_unique<RenderLayer>(pipeline, world));

    const int rc = host->Run();
    pipeline.Shutdown();
    return rc;
}
