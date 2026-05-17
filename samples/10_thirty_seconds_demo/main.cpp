// samples/10_thirty_seconds_demo —— 引擎子系统组合验收 demo。
//
// 启动 → 异步加载资源 → JSON scene 加载 → 玩家走完一小段平台 → 触
// 发胜利或 30 秒超时，全程引擎零改动。
//
// 关卡：
//   * player：dynamic RigidBody，左下出生（-6, 1），左 / 右走，空格跳
//   * ground：static RigidBody，y=-1.5
//   * 三个跳跃平台 platform_a / b / c：从左下到右上递增高度
//   * goal：右上 sensor + emissive material，玩家接触 → "Victory!" + 退出
//   * sun：DirectionalLight，主光投影
//
// 三个机制：
//   * 跳跃 + 走平台（Mechanic 1）—— SetLinearVelocity + grounded check
//   * 物理碰撞                 （Mechanic 2）—— Box2D 自动处理（scene
//                              加载时通过 SceneLoadOptions.physicsWorld
//                              把 RigidBody+Collider 配对 attach 到
//                              PhysicsWorld）
//   * 距离触发胜利             （Mechanic 3）—— 主循环每帧查玩家与
//                              goal 的 xy 距离 < 0.6m 即胜利
//
// camera 不进 scene（schema v1 暂未把 Camera 当 component），sample 在
// 这里手动加一个 entity 挂 Camera 组件。MaterialInstance 也不进 scene
// （runtime-only raw 指针），sample 在 SceneLoad 之后按 NameComponent
// 反查 entity 把 toon / textured / emissive 三个 instance 一一挂上。

#include "../common/CaptureLayer.h"

#include <orange/engine/app/AppConfig.h>
#include <orange/engine/app/AppHost.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/app/Layer.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/input/ActionMap.h>
#include <orange/engine/input/InputContext.h>
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
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
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
using Orange::Engine::Render::BuiltinPostProcessChain::CreateDefault;
using Orange::Engine::Render::Camera;
using Orange::Engine::Render::DirectionalLight;
using Orange::Engine::Render::MaterialInstance;
using Orange::Engine::Render::MaterialSystem;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::PostProcessChain;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Scene::NameComponent;
using Orange::Engine::Scene::TransformComponent;
namespace In   = Orange::Engine::Input;
namespace Phys = Orange::Engine::Physics;
namespace Sce  = Orange::Engine::Scene;

namespace
{

// ---------- Mesh 工厂：单位立方体（与 sample 04/09 同布局）----------

struct CubeFace
{
    std::array<VertexPosition3, 4> positions;
};

constexpr std::array<CubeFace, 6> kCubeFaces = {{
    {{{{ 0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f,  0.5f}}}},
    {{{{-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f, -0.5f}}}},
    {{{{-0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f}}}},
    {{{{-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f,  0.5f}, {-0.5f, -0.5f,  0.5f}}}},
    {{{{-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}}}},
    {{{{ 0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}}}},
}};

constexpr std::array<VertexUV2, 4> kFaceUVs = {{
    {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
}};

std::unique_ptr<MeshAsset> MakeCubeMesh()
{
    std::vector<VertexPosition3> positions;
    std::vector<VertexUV2>       uvs;
    std::vector<std::uint32_t>   indices;
    positions.reserve(24);
    uvs.reserve(24);
    indices.reserve(36);

    for (std::uint32_t face = 0; face < kCubeFaces.size(); ++face)
    {
        const std::uint32_t base = face * 4;
        for (int i = 0; i < 4; ++i)
        {
            positions.push_back(kCubeFaces[face].positions[i]);
            uvs.push_back(kFaceUVs[i]);
        }
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        indices.push_back(base + 0);
        indices.push_back(base + 3);
        indices.push_back(base + 2);
    }
    auto pMesh = std::make_unique<MeshAsset>(std::move(positions),
                                             std::move(uvs),
                                             std::move(indices));
    pMesh->ComputeSmoothNormalsFromTriangles();
    return pMesh;
}

// 按 NameComponent.name 在 World 里反查 entity。SceneLoad 之后 entity
// 数值与 JSON 里的 "id" 字段无关——内部按 EnTT 重新分配，调用方只能
// 走 name 反查。
Entity FindEntityByName(World& world, std::string_view name)
{
    auto& reg  = world.Registry();
    auto  view = reg.view<NameComponent>();
    for (auto e : view)
    {
        if (reg.get<NameComponent>(e).name == name)
        {
            return World::FromEntt(e);
        }
    }
    return Entity::Invalid();
}

// ---------- GameplayLayer：input → physics → entity transform sync ----------
//
// 单 layer 把 player input + physics step + transform sync 三件事放一
// 起——demo 范围足够小，拆三 layer 反而繁琐。OnUpdate 顺序：读 input
// → 写 player velocity → physics.Step → sync 所有 dynamic body 的
// xy 到 entity transform → 帧末 input.BeginFrame()。

constexpr float kWalkSpeed       = 4.5f;
constexpr float kJumpVelocity    = 8.0f;
constexpr float kGroundedYThresh = 0.5f;
constexpr float kVictoryDistance = 0.6f;
constexpr float kDemoTimeoutSeconds = 30.0f;

class GameplayLayer : public Layer
{
public:
    GameplayLayer(In::InputContext&   input,
                  Phys::PhysicsWorld& phys,
                  World&              world,
                  Entity              playerEntity,
                  Entity              goalEntity,
                  AppHost&            host)
        : Layer("GameplayLayer")
        , mInput(input)
        , mPhys(phys)
        , mWorld(world)
        , mPlayer(playerEntity)
        , mGoal(goalEntity)
        , mHost(host)
    {
    }

    void OnUpdate(const FrameContext& frame) override
    {
        float dt = static_cast<float>(frame.time.deltaSeconds);
        if (dt > 1.0f / 30.0f)
        {
            dt = 1.0f / 30.0f;
        }

        // ---- input → player velocity ----
        const In::ActionMap* topMap = mInput.Top();
        bool victory = false;
        if (topMap != nullptr)
        {
            const bool leftHeld  = In::IsHeld    (topMap->GetState("move_left"));
            const bool rightHeld = In::IsHeld    (topMap->GetState("move_right"));
            const bool jumpDown  = In::IsTriggered(topMap->GetState("jump"));

            const auto* rb = mWorld.GetComponent<Phys::RigidBodyComponent>(mPlayer);
            if (rb != nullptr && rb->handle.IsValid())
            {
                const float horizontalDir =
                    (rightHeld ? 1.0f : 0.0f) - (leftHeld ? 1.0f : 0.0f);
                auto vel = mPhys.GetLinearVelocity(rb->handle);
                vel.x = horizontalDir * kWalkSpeed;
                if (jumpDown && std::fabs(vel.y) < kGroundedYThresh)
                {
                    vel.y = kJumpVelocity;
                }
                mPhys.SetLinearVelocity(rb->handle, vel);
            }
        }

        // ---- physics step ----
        mPhys.Step(dt);

        // ---- sync dynamic bodies → entity Transform ----
        // 主循环需要的就这一个 entity（玩家），但保持遍历范式与 sample 06
        // 一致——后续若加更多 dynamic body 不需要改循环。
        auto& reg = mWorld.Registry();
        auto  view = reg.view<Phys::RigidBodyComponent, TransformComponent>();
        for (auto e : view)
        {
            const auto& rbComp = reg.get<Phys::RigidBodyComponent>(e);
            if (!rbComp.handle.IsValid() || rbComp.type != Phys::BodyType::Dynamic)
            {
                continue;
            }
            const auto bxf = mPhys.GetBodyTransform(rbComp.handle);
            auto& xf = reg.get<TransformComponent>(e);
            xf.position.x = bxf.position.x;
            xf.position.y = bxf.position.y;
            // z 不动——保留 ECS 端 2.5D 高度（虽然本 sample 都在 z=0）。
        }

        // ---- victory check ----
        if (mPlayer.IsValid() && mGoal.IsValid())
        {
            const auto* px = mWorld.GetComponent<TransformComponent>(mPlayer);
            const auto* gx = mWorld.GetComponent<TransformComponent>(mGoal);
            if (px != nullptr && gx != nullptr)
            {
                const float dx = px->position.x - gx->position.x;
                const float dy = px->position.y - gx->position.y;
                const float dist2 = dx * dx + dy * dy;
                if (dist2 < kVictoryDistance * kVictoryDistance)
                {
                    victory = true;
                }
            }
        }

        // ---- timeout check ----
        mElapsed += dt;
        const bool timeout = mElapsed > kDemoTimeoutSeconds;

        if ((victory || timeout) && !mFinished)
        {
            std::fprintf(stdout,
                         "[10_thirty_seconds_demo] %s after %.2fs — exiting.\n",
                         victory ? "Victory!" : "Timed out",
                         static_cast<double>(mElapsed));
            mFinished = true;
            mHost.RequestExit();
        }

        // ---- 帧末把 InputContext 状态机推进一步 ----
        mInput.BeginFrame();
    }

    bool OnEvent(const Platform::WindowEvent& event) override
    {
        if (auto* key = std::get_if<Platform::KeyEvent>(&event))
        {
            const bool isDown = key->action != Platform::KeyAction::Release;
            mInput.PostKeyEvent(static_cast<In::KeyCode>(key->key), isDown);
            return false;
        }
        return false;
    }

private:
    In::InputContext&   mInput;
    Phys::PhysicsWorld& mPhys;
    World&              mWorld;
    Entity              mPlayer;
    Entity              mGoal;
    AppHost&            mHost;
    float               mElapsed{0.0f};
    bool                mFinished{false};
};

// 简单的 RenderLayer——只在 GameplayLayer 之后调一次 Pipeline.Render。
class RenderLayer : public Layer
{
public:
    RenderLayer(Pipeline& pipeline, World& world)
        : Layer("RenderLayer"), mPipeline(pipeline), mWorld(world) {}

    void OnUpdate(const FrameContext& frame) override
    {
        mPipeline.SetFrameTime(static_cast<float>(frame.time.totalSeconds));
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

}  // namespace

int main(int argc, char** argv)
{
    const auto captureCli = OrangeSamples::ParseCaptureCli(argc, argv);

    // ---- AppHost ----
    AppConfig cfg{};
    cfg.window.title  = "OrangeEngine - 10 thirty_seconds_demo";
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

    // ---- AssetRegistry ----
    AssetRegistry assets;
    if (auto rc = assets.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
        rc.IsErr())
    {
        std::fprintf(stderr, "RegisterLoader<ShaderAsset> failed\n");
        return 1;
    }

    // 程序式 mesh —— scene JSON 通过 path "builtin/cube" 引用。Insert 让
    // 后续 SceneLoad 的 Load<MeshAsset>("builtin/cube") 走 dedup 命中。
    auto meshHandleResult = assets.Insert<MeshAsset>("builtin/cube", MakeCubeMesh());
    if (meshHandleResult.IsErr())
    {
        std::fprintf(stderr, "Insert<MeshAsset>(builtin/cube) failed\n");
        return 1;
    }

    // ---- Async asset 加载演示（异步加载 API 路径）----
    // 既有 mesh 是 Insert 来的，async load 同 path 会瞬秒命中 dedup —— 用
    // 来证明 LoadAsync + WaitFor API 在 sample 路径上调通即可，不强求
    // 真挂 IO 时长。真正的 disk-IO async load 等游戏仓库 fork 出去后接
    // 真实关卡资源时再上。
    {
        auto async1 = assets.LoadAsync<MeshAsset>("builtin/cube");
        if (async1.IsOk())
        {
            const bool ready =
                assets.WaitFor(async1.Value(), std::chrono::milliseconds{500});
            std::fprintf(stdout,
                         "[startup] LoadAsync<MeshAsset>(\"builtin/cube\") WaitFor → %s\n",
                         ready ? "Ready" : "Timeout");
        }
    }

    // ---- MaterialSystem + 三个 instance ----
    MaterialSystem materials(assets);
    if (auto rc = materials.RegisterBuiltins(); rc.IsErr())
    {
        std::fprintf(stderr, "RegisterBuiltins failed\n");
        return 1;
    }
    // 视觉分工：player 用 toon（warm/mid/cool 三档形成清晰立体感，比 rim
    // _light 在深色背景下更易读）；ground 用 textured（程序式 checker 当
    // 地砖纹理）；3 个跳跃平台用 toon 复用同一个 instance；3 条 platform
    // 顶面高光条复用 goalMat 的 emissive，做成"踩面提示 + 微 bloom"；
    // goal 自身 emissive 体积更大触发更强 bloom。背景刻意不放 backdrop
    // entity——Pipeline 默认 clear color 是深蓝 (0.05, 0.07, 0.10)，作为
    // 夜空既能与 emissive 形成强对比，又不与 toon 的暖色调撞色。
    auto playerMat   = materials.CreateInstance("toon");
    auto groundMat   = materials.CreateInstance("textured");
    auto platformMat = materials.CreateInstance("toon");
    auto goalMat     = materials.CreateInstance("emissive");
    if (!playerMat || !groundMat || !platformMat || !goalMat)
    {
        std::fprintf(stderr, "CreateInstance(toon/textured/toon/emissive) returned null\n");
        return 1;
    }

    // ---- Input ----
    In::InputContext input;
    auto actionMapResult = In::LoadActionMapFromFile(ORANGE_SAMPLE_10_ACTIONS_PATH);
    if (actionMapResult.IsErr())
    {
        std::fprintf(stderr,
                     "LoadActionMapFromFile failed (path=%s, code=%u)\n",
                     ORANGE_SAMPLE_10_ACTIONS_PATH,
                     static_cast<unsigned>(actionMapResult.Error()));
        return 1;
    }
    input.Push(std::move(actionMapResult).Value());

    // ---- PhysicsWorld（默认 gravity (0, -9.81)）----
    Phys::PhysicsWorld physWorld;

    // ---- World + Scene 加载 ----
    World world;
    {
        Sce::LoadOptions opt{};
        opt.assetRegistry = &assets;
        opt.physicsWorld  = &physWorld;
        auto loadResult = Sce::Load(ORANGE_SAMPLE_10_SCENE_PATH, world, opt);
        if (loadResult.IsErr())
        {
            std::fprintf(stderr,
                         "SceneSerialization::Load failed (path=%s, code=%u)\n",
                         ORANGE_SAMPLE_10_SCENE_PATH,
                         static_cast<unsigned>(loadResult.Error()));
            return 1;
        }
        std::fprintf(stdout,
                     "[startup] scene loaded: %zu entities, %zu physics bodies\n",
                     world.Size(), physWorld.BodyCount());
    }

    // ---- 反查 entities + 挂 MaterialInstance ----
    Entity playerEntity   = FindEntityByName(world, "player");
    Entity goalEntity     = FindEntityByName(world, "goal");
    Entity groundEntity   = FindEntityByName(world, "ground");
    Entity platAEntity    = FindEntityByName(world, "platform_a");
    Entity platBEntity    = FindEntityByName(world, "platform_b");
    Entity platCEntity    = FindEntityByName(world, "platform_c");
    if (!playerEntity.IsValid() || !goalEntity.IsValid())
    {
        std::fprintf(stderr, "Scene 缺 player / goal entity\n");
        return 1;
    }

    auto attachMaterial = [&](Entity e, MaterialInstance* mat)
    {
        if (!e.IsValid()) return;
        if (auto* r = world.GetComponent<RenderableComponent>(e))
        {
            r->materialInstance = mat;
        }
    };
    attachMaterial(playerEntity, playerMat.get());
    attachMaterial(groundEntity, groundMat.get());
    attachMaterial(platAEntity,  platformMat.get());
    attachMaterial(platBEntity,  platformMat.get());
    attachMaterial(platCEntity,  platformMat.get());
    attachMaterial(goalEntity,   goalMat.get());

    // 平台顶面高光条（仅 Transform + Renderable，不进物理）—— 借 emissive
    // 模板做"踩面提示"，让玩家从相机视角一眼看出哪条边是落脚平面。
    // 名字必须与 scene.json 一致，缺失则 attachMaterial 因 IsValid()
    // == false 直接跳过，不报错。
    attachMaterial(FindEntityByName(world, "platform_a_top"), goalMat.get());
    attachMaterial(FindEntityByName(world, "platform_b_top"), goalMat.get());
    attachMaterial(FindEntityByName(world, "platform_c_top"), goalMat.get());

    // ---- Camera entity（schema v1 暂不把 Camera 当 component） ----
    Entity camEntity = world.CreateEntity();
    {
        const float aspect = static_cast<float>(cfg.window.width)
                           / static_cast<float>(cfg.window.height);
        // 50° FOV + 稍高稍近的位置：广角下相机看出来的"平台向左下倾斜"
        // 透视失真在 50° 收敛回正；锚点对着关卡中部偏上 (-0.5, 1.6)，让
        // 起点 (-6,1) 与终点 (3.8, 2.95) 都落在画面里且不挤压。
        Camera cam = Camera::Perspective(glm::radians(50.0f), aspect, 0.1f, 100.0f);
        cam.view = glm::lookAt(glm::vec3(-0.5f, 2.4f, 9.5f),
                               glm::vec3(-0.5f, 1.6f, 0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent(camEntity, cam);
    }

    // ---- Pipeline + PostProcessChain ----
    Pipeline pipeline;
    if (auto rc = pipeline.Initialize(host->GetWindow(), assets); rc.IsErr())
    {
        std::fprintf(stderr, "Pipeline::Initialize failed\n");
        return 1;
    }
    PostProcessChain chain = CreateDefault();
    if (auto* bp = dynamic_cast<BloomPass*>(chain.FindByName("bloom")))
    {
        // emissive 模板片元 hardcode kIntensity=4.0，本身就远超 1.0 → 默
        // 认 0.85 阈值已经触发；这里把阈值压到 0.8 让边沿微暗的高光条也
        // 进 bloom，强度抬到 0.55（从 0.7 微调）让 goal 出明显光晕但不至
        // 于把 3 条 platform 顶面高光条糊成一片。
        bp->threshold = 0.8f;
        bp->intensity = 0.55f;
    }
    pipeline.SetPostProcessChain(&chain);
    pipeline.SetMaterialSystem(&materials);

    // ---- Layers（push 顺序 = OnUpdate 顺序）----
    if (!captureCli.outPath.empty())
    {
        host->PushLayer(std::make_unique<OrangeSamples::CaptureLayer>(
            pipeline, *host, captureCli.outPath, captureCli.captureFrame));
    }
    host->PushLayer(std::make_unique<GameplayLayer>(
        input, physWorld, world, playerEntity, goalEntity, *host));
    host->PushLayer(std::make_unique<RenderLayer>(pipeline, world));

    const int rc = host->Run();
    pipeline.Shutdown();
    return rc;
}
