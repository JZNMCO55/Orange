// samples/06_physics_platformer —— 物理平台跳跃演示
//
// 场景：
//   * 静态地面 plane（textured Material）+ 静态 ground BoxCollider 在 y=-0.5；
//   * 动态 ball（rim_light Material）+ dynamic CircleCollider，初始 y=3，
//     受重力（默认 (0, -9.81)）下落，撞到地面后按 restitution 反弹；
//   * DirectionalLight 投影 + 完整 PostProcessChain（HDR/Bloom/Tonemap），
//     视觉风格沿用 sample 07。
//
// 物理 ↔ ECS 同步：每帧 Step(dt) 后扫所有挂 dynamic body 的 entity，
// 把 PhysicsWorld.GetBodyTransform(handle).position（XY 平面）写回
// TransformComponent.position（保持 Z 不动）——与 OrangeEngine "2D / 2.5D"
// 定位一致；Box2D 是 2D 物理，Z 维度由消费方在 ECS 端自己保留。
//
// 用 PhysicsLayer 做 Step + sync，置于 RenderLayer 之前——保证 Render 取到
// 的 Transform 已是本帧物理解出的最新位置。

#include "CaptureLayer.h" // samples/common/

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
#include <orange/engine/render/PostProcessPasses.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/render/ShadowConfig.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

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
using Orange::Engine::Render::BloomPass;
using Orange::Engine::Render::Camera;
using Orange::Engine::Render::DirectionalLight;
using Orange::Engine::Render::MaterialInstance;
using Orange::Engine::Render::MaterialSystem;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::PostProcessChain;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Render::ShadowConfig;
using Orange::Engine::Render::BuiltinPostProcessChain::CreateDefault;
using Orange::Engine::Scene::TransformComponent;
namespace Phys = Orange::Engine::Physics;

namespace
{

    // ---------- mesh 工厂（与 sample 07 同布局）----------
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

    // ---------- 物理 ↔ ECS sync ----------
    //
    // PhysicsLayer 持 PhysicsWorld + 一个 dynamic body handle / entity 对——
    // 每帧 Step + 把 body XY position 同步回 entity Transform。
    // Z 维度由本侧手动保留：sample 让 ball 在 z=0 的 2D 平面上动，
    // 视觉上仍占 3D plane 中央。
    class PhysicsLayer : public Layer
    {
    public:
        PhysicsLayer(Phys::PhysicsWorld& world, World& ecs,
                     Entity ballEntity, Phys::BodyHandle ballBody)
            : Layer("PhysicsLayer"), mPhys(world), mEcs(ecs), mBallEntity(ballEntity), mBallBody(ballBody)
        {
        }

        void OnUpdate(const FrameContext& frame) override
        {
            // dt 上限 1/30 秒，避免窗口切回前台时的"长帧"导致一帧推太远。
            float dt = frame.time.deltaSeconds;
            if (dt > 1.0f / 30.0f)
            {
                dt = 1.0f / 30.0f;
            }
            mPhys.Step(dt);

            const auto bxf = mPhys.GetBodyTransform(mBallBody);
            if (auto* xf = mEcs.GetComponent<TransformComponent>(mBallEntity))
            {
                // 物理 XY ↔ 渲染 XY；Z 不动（sample 让球在 Z=0 平面上动）。
                xf->position.x = bxf.position.x;
                xf->position.y = bxf.position.y;
            }

            // 兜底：球掉出视野（y < -10）时拉回原始高度，让 demo 持续可视。
            if (bxf.position.y < -10.0f)
            {
                Phys::BodyTransform reset{};
                reset.position = {0.0f, 4.0f};
                mPhys.SetBodyTransform(mBallBody, reset);
                mPhys.SetLinearVelocity(mBallBody, {0.0f, 0.0f});
            }
        }

    private:
        Phys::PhysicsWorld& mPhys;
        World&              mEcs;
        Entity              mBallEntity;
        Phys::BodyHandle    mBallBody;
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

int main(int argc, char** argv)
{
    const auto captureCli = OrangeSamples::ParseCaptureCli(argc, argv);

    AppConfig cfg{};
    cfg.window.title  = "OrangeEngine - 06 physics_platformer";
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

    // plane halfSize=1.5（旧版 3.0）：缩小让球（半径 0.5）成为画面主体；
    // 旧版 6×6 棋盘把球挤成画面中央一个小点，加上 camera 俯视角让球
    // 看似"埋在 plane 中"。
    auto planeRes  = assets.Insert<MeshAsset>("builtin/plane", MakePlaneMesh(1.5f));
    auto sphereRes = assets.Insert<MeshAsset>("builtin/sphere", MakeSphereMesh(0.5f, 32, 16));
    if (planeRes.IsErr() || sphereRes.IsErr())
    {
        std::fprintf(stderr, "Insert<MeshAsset> failed\n");
        return 1;
    }
    AssetHandle<MeshAsset> planeHandle  = planeRes.Value();
    AssetHandle<MeshAsset> sphereHandle = sphereRes.Value();

    MaterialSystem materials(assets);
    if (auto rb = materials.RegisterBuiltins(); rb.IsErr())
    {
        std::fprintf(stderr, "RegisterBuiltins failed\n");
        return 1;
    }
    auto planeInstance = materials.CreateInstance("textured");
    // 球用 toon：从 45° 俯视看 rim_light 的"暗内 + 边亮"会被读成"plane
    // 上一个洞"，cell shading 的硬阶过渡反而给球一个清晰可辨的实体。
    auto sphereInstance = materials.CreateInstance("toon");
    if (!planeInstance || !sphereInstance)
    {
        std::fprintf(stderr, "CreateInstance failed\n");
        return 1;
    }

    World world;

    // 静态地面：在物理上是一个 BoxCollider，在视觉上是一块 5×5 plane。
    // PhysicsWorld 默认重力 (0, -9.81)：地面顶面坐标 y=-0.5，让球从 y=3 落下
    // 后稳定停在 y ≈ 0（球半径 0.5）。
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

    // 动态球：初始 y=3，重力下落。Restitution=0.6 给中等弹性，
    // 视觉上落地后能看到几次反弹再静止。
    Entity ballEntity = world.CreateEntity();
    {
        TransformComponent xf{};
        xf.position = {0.0f, 3.0f, 0.0f};
        world.AddComponent(ballEntity, xf);
        RenderableComponent r;
        r.mesh             = sphereHandle;
        r.materialInstance = sphereInstance.get();
        world.AddComponent(ballEntity, r);
    }

    // 调试 marker：两个小球（半径 0.05）静态放在已知 world 位置。
    //   * (0, 0, 0)    ——若 ball.y 物理上为 0（settle 状态），应与球心重合；
    //   * (0, -0.5, 0) ——plane 顶面中心，应贴在 plane 表面上。
    // 若两 marker 视觉位置与"ball 物理位置 / plane 表面"对齐，说明
    // 视觉上的"球漂浮"是 perspective 错觉而非 ECS 渲染 bug。
    // 主光：接近正上方（仅 0.2 / 0.3 的 X / Z 偏移）+ warm 白。这样：
    //   * 球上半完整映入 toon warm 段，camera 看到的球面以亮色为主；
    //   * 阴影正好落在球正下方稍偏 plane 中段、跟球分得开。
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

    // 透视相机：从前上方看场景。
    Entity camEntity = world.CreateEntity();
    {
        const float aspect = static_cast<float>(cfg.window.width) / static_cast<float>(cfg.window.height);
        Camera      cam    = Camera::Perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        // 中等角度（约 30° 俯 + 一点侧偏）：camera (1.5, 1.5, 3.5) lookAt
        // (0, 0.1, 0)。比 45° 俯视更接近"水平眼睛位"——球占画面更主体、
        // toon 三段分布在球可见面上更均匀（不会出现"中心 cool / 边沿
        // warm"的反向 banding）；plane 透视压缩适中、不会读成翘起。
        cam.view = glm::lookAt(glm::vec3(1.5f, 1.5f, 3.5f),
                               glm::vec3(0.0f, 0.1f, 0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent(camEntity, cam);
    }

    // ---------- 物理 ----------
    Phys::PhysicsWorld physWorld; // 默认 gravity (0, -9.81)、4 substep

    // 静态 ground：BoxDesc 大半 50×0.25，位置 (0, -0.75)——刚好让顶面
    // 在 y=-0.5（与视觉 plane 重合）。half-extents.y = 0.25。
    Phys::BodyHandle groundBody;
    {
        Phys::RigidBodyComponent rb;
        rb.type            = Phys::BodyType::Static;
        rb.initialPosition = {0.0f, -0.75f};
        Phys::ColliderComponent col;
        col.shape       = Phys::BoxDesc{{50.0f, 0.25f}, {0.0f, 0.0f}};
        col.friction    = 0.6f;
        col.restitution = 0.0f;
        groundBody      = physWorld.AddBody(rb, col);
        if (!groundBody.IsValid())
        {
            std::fprintf(stderr, "PhysicsWorld.AddBody(ground) failed\n");
            return 1;
        }
    }

    // 动态 ball：CircleDesc 半径 0.5、density 1。
    // restitution 0.35 让球落地后只反弹 2-3 次就稳定（旧版 0.6 + friction
    // 0.4 要弹 10 秒才静止，sample 早期截图常落在"还在弹"中段，看上
    // 去像球漂在 plane 上方）；linear damping 0.2 给水平 / 残余速度
    // 一点摩擦衰减；高 friction 让滚动早早停下。
    Phys::BodyHandle ballBody;
    {
        Phys::RigidBodyComponent rb;
        rb.type            = Phys::BodyType::Dynamic;
        rb.initialPosition = {0.0f, 3.0f};
        rb.linearDamping   = 0.2f;
        Phys::ColliderComponent col;
        col.shape       = Phys::CircleDesc{0.5f, {0.0f, 0.0f}};
        col.density     = 1.0f;
        col.friction    = 0.6f;
        col.restitution = 0.35f;
        ballBody        = physWorld.AddBody(rb, col);
        if (!ballBody.IsValid())
        {
            std::fprintf(stderr, "PhysicsWorld.AddBody(ball) failed\n");
            return 1;
        }
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
    if (auto* bp = dynamic_cast<BloomPass*>(chain.FindByName("bloom")))
    {
        bp->threshold = 0.7f;
        bp->intensity = 0.4f;
    }
    pipeline.SetPostProcessChain(&chain);
    pipeline.SetMaterialSystem(&materials);
    pipeline.SetShadowConfig(ShadowConfig{});

    // PhysicsLayer 在 RenderLayer 之前 push：layer stack 的 OnUpdate 顺序
    // 自底向上，先压的先 update——这样 Render 看到的 transform 已是本帧
    // 物理解出的最新值。
    host->PushLayer(std::make_unique<PhysicsLayer>(physWorld, world, ballEntity, ballBody));
    if (!captureCli.outPath.empty())
    {
        host->PushLayer(std::make_unique<OrangeSamples::CaptureLayer>(
            pipeline, *host, captureCli.outPath, captureCli.captureFrame));
    }
    host->PushLayer(std::make_unique<RenderLayer>(pipeline, world));

    const int rc = host->Run();
    pipeline.Shutdown();
    return rc;
}
