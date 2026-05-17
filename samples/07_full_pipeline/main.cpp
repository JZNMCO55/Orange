// samples/07_full_pipeline —— 全流程综合演示
//
// 把视觉基线（plane + DirectionalLight + shadow + bloom + tonemap）
// 与全部子系统串起来：
//
//   * Animation：DragonBones mecha_1406 骨架；按角色当前速度切 idle/
//     walk/jump，joint 用 17 个 rim_light 小球可视化（当前 GPU
//     skinning 暂缺，joint marker 表达"骨头在动"即够看）；
//   * Physics：Box2D 3.x dynamic body（CircleCollider）做角色，static
//     ground 保住 sphere 不掉穿；
//   * Input：assets/configs/default.actions.json + InputContext 栈；
//     A/D 走、Space 跳、Esc 退；GameLayer 把 GLFW key event 转发进
//     ctx.PostKeyEvent；
//   * Audio：AudioEngine（默认 backend）+ 程序生成 880Hz beep。Space 跳
//     的瞬间 SoundInstance.Restart 重触发——验证 Audio 模块在主循环里
//     真接通到声卡。
//
// 双段 frame 流程（沿用 sample 07 旧版）：shadow pass → 主 pass + per-
// template RHIPipeline → bloom mip-chain → tonemap。
//
// CLI：
//   --capture <path>          截图 + 退出（CI / 验收）
//   --frames N                截图发生在第 N 帧（默认 60）

#include "animation/dragonbones/DragonBonesContext.h"  // src/

#include "BeepWav.h"          // samples/common/
#include "CaptureLayer.h"     // samples/common/

#include <orange/engine/animation/SkeletalAnimator.h>
#include <orange/engine/app/AppConfig.h>
#include <orange/engine/app/AppHost.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/app/Layer.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/asset/SkeletonAsset.h>
#include <orange/engine/asset/SkeletonLoader.h>
#include <orange/engine/asset/SoundAsset.h>
#include <orange/engine/audio/AudioEngine.h>
#include <orange/engine/audio/Sound.h>
#include <orange/engine/audio/SoundInstance.h>
#include <orange/engine/input/Action.h>
#include <orange/engine/input/ActionMap.h>
#include <orange/engine/input/InputContext.h>
#include <orange/engine/input/InputDevice.h>
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
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace Orange::Engine;
using Orange::Engine::Asset::AssetHandle;
using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Asset::MeshAsset;
using Orange::Engine::Asset::ShaderAsset;
using Orange::Engine::Asset::ShaderLoader;
using Orange::Engine::Asset::SkeletonAsset;
using Orange::Engine::Asset::SkeletonLoader;
using Orange::Engine::Asset::SoundAsset;
using Orange::Engine::Asset::VertexPosition3;
using Orange::Engine::Asset::VertexUV2;
using Orange::Engine::Audio::AudioEngine;
using Orange::Engine::Audio::SoundInstance;
using Orange::Engine::Render::BloomPass;
using Orange::Engine::Render::BuiltinPostProcessChain::CreateDefault;
using Orange::Engine::Render::Camera;
using Orange::Engine::Render::DirectionalLight;
using Orange::Engine::Render::MaterialInstance;
using Orange::Engine::Render::MaterialSystem;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::PostProcessChain;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Render::ShadowConfig;
using Orange::Engine::Scene::TransformComponent;
namespace DBB = Orange::Engine::Animation::DragonBonesBackend;
namespace Ani = Orange::Engine::Animation;
namespace In  = Orange::Engine::Input;
namespace Phys = Orange::Engine::Physics;

namespace
{

#ifndef ORANGE_ENGINE_SKELETON_DATA_DIR
#  error "ORANGE_ENGINE_SKELETON_DATA_DIR must be defined by CMake"
#endif
#ifndef ORANGE_ENGINE_ACTIONS_DIR
#  error "ORANGE_ENGINE_ACTIONS_DIR must be defined by CMake"
#endif

// mecha_1406 AABB ≈ 248×239 px；缩 1/80 → 渲染单位 ~3.1×3.0，与 plane
// halfSize=2.5 同量级，肉眼能看清骨头分布而不是一坨小点。
constexpr float kSkeletonScale = 1.0f / 80.0f;
// DragonBones 默认 yDown=true（数据按屏幕坐标存储）；渲染端 y-up，
// 翻一下让 mecha 站立。
constexpr float kYAxisFlip     = -1.0f;

// 角色物理参数。
constexpr float kCharacterRadius   = 0.4f;     // 物理 collider 半径
constexpr float kWalkSpeed         = 2.5f;     // m/s
constexpr float kJumpVelocity      = 5.5f;     // m/s（瞬时冲量后初速度）
constexpr float kGroundedYThresh   = 0.05f;    // |vy| < 该阈值视为站地
constexpr float kGroundY           = -2.5f;    // 视觉 plane 顶面

// ---------- mesh 工厂（与 sample 06/07 旧版同布局）----------
std::unique_ptr<MeshAsset> MakePlaneMesh(float halfSize)
{
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
            const std::uint32_t a = i       * (lon + 1) + j;
            const std::uint32_t b = (i + 1) * (lon + 1) + j;
            const std::uint32_t c = (i + 1) * (lon + 1) + (j + 1);
            const std::uint32_t d = i       * (lon + 1) + (j + 1);
            indices.push_back(a); indices.push_back(c); indices.push_back(b);
            indices.push_back(a); indices.push_back(d); indices.push_back(c);
        }
    }
    return std::make_unique<MeshAsset>(std::move(positions),
                                       std::move(uvs),
                                       std::move(indices));
}

// ---------- GameLayer：input → physics → animator → joint markers ----------
//
// 主循环的"游戏逻辑层"：
//   1. BeginFrame：InputContext 推进栈顶 ActionMap 状态机；
//   2. Read input：A/D 决定 horizontal velocity；Space 触发跳跃（仅在
//      站地状态下生效——按 |vy| < 阈值粗判）；
//   3. PhysicsWorld.Step(dt)；
//   4. 把 character body 的 XY position 写到一个 mRootPosition 缓存；
//   5. 根据 horizontal velocity / vy 切动画状态：jump / walk / idle；
//   6. animator.Tick(dt)；
//   7. 把 joint marker 的世界位置 = mRootPosition + joint local offset
//      映射到对应 Entity TransformComponent。
//
// OnEvent 把 GLFW KeyEvent 转发进 InputContext.PostKeyEvent；ActionMap
// 内部 binding 解析按 GLFW-aligned KeyCode（与 InputDevice.h 注释一致）。
class GameLayer : public Layer
{
public:
    GameLayer(In::InputContext&       input,
              Phys::PhysicsWorld&     phys,
              Phys::BodyHandle        charBody,
              Ani::SkeletalAnimator&  animator,
              World&                  world,
              std::vector<Entity>     jointEntities,
              SoundInstance&          jumpSound)
        : Layer("GameLayer")
        , mInput(input)
        , mPhys(phys)
        , mCharBody(charBody)
        , mAnimator(animator)
        , mWorld(world)
        , mJointEntities(std::move(jointEntities))
        , mJumpSound(jumpSound)
    {
    }

    bool OnEvent(const Platform::WindowEvent& event) override
    {
        if (auto* k = std::get_if<Platform::KeyEvent>(&event))
        {
            const bool isDown = k->action != Platform::KeyAction::Release;
            mInput.PostKeyEvent(static_cast<In::KeyCode>(k->key), isDown);
        }
        return false;  // 不消费事件——RenderLayer 还需要 ResizeEvent
    }

    void OnUpdate(const FrameContext& frame) override
    {
        float dt = frame.time.deltaSeconds;
        if (dt > 1.0f / 30.0f) { dt = 1.0f / 30.0f; }

        // 顺序约定：PollEvents 已经把本帧物理事件 post 进了 InputContext
        // （通过 OnEvent 转发）；现在直接读状态、用、然后**在帧末**
        // BeginFrame 把 Pressed → Held / Released → Idle 推一次，让下一
        // 帧的 PollEvents 又能把新 key down 当 Pressed 触发。
        // BeginFrame 不能放在 OnUpdate 开头——那样会把 PollEvents 刚刚
        // post 的 Pressed 立刻吞掉，IsTriggered 永远拿不到。

        // ---- 读 input → 物理 velocity ----
        const In::ActionMap* topMap = mInput.Top();
        if (topMap == nullptr) { return; }

        const bool leftHeld  = In::IsHeld    (topMap->GetState("move_left"));
        const bool rightHeld = In::IsHeld    (topMap->GetState("move_right"));
        const bool jumpDown  = In::IsTriggered(topMap->GetState("jump"));

        const float horizontalDir = (rightHeld ? 1.0f : 0.0f) - (leftHeld ? 1.0f : 0.0f);
        const auto curVel = mPhys.GetLinearVelocity(mCharBody);

        // 水平：直接 SetLinearVelocity 把 X 分量替换；纵向（重力 + 跳）
        // 由 Box2D 自己积分，本侧只在跳跃瞬间盖一次 vy。
        glm::vec2 newVel{horizontalDir * kWalkSpeed, curVel.y};

        const bool grounded = std::fabs(curVel.y) < kGroundedYThresh;
        if (jumpDown && grounded)
        {
            newVel.y = kJumpVelocity;
            // 跳跃瞬间重触发音效。AudioEngine 没起来时 Restart 是 no-op。
            mJumpSound.Restart();
        }
        mPhys.SetLinearVelocity(mCharBody, newVel);

        // ---- 物理推进 ----
        mPhys.Step(dt);

        // ---- 角色根位置同步 + 动画状态切 ----
        const auto bxf = mPhys.GetBodyTransform(mCharBody);
        mRootPos = glm::vec3{bxf.position.x, bxf.position.y, 0.0f};

        // 状态判定：
        //   * 跳跃中 |vy| 大 → "jump"
        //   * 否则水平有动 → "walk"
        //   * 否则 → "idle"
        const auto vyNow = mPhys.GetLinearVelocity(mCharBody);
        std::string desired;
        if (std::fabs(vyNow.y) > 0.5f)
        {
            desired = "jump";
        }
        else if (std::fabs(horizontalDir) > 0.0f)
        {
            desired = "walk";
        }
        else
        {
            desired = "idle";
        }
        if (desired != mCurrentAnim)
        {
            // 0.1s fade-in 让切换平滑；playTimes=0 = loop。
            mAnimator.Play(desired, 0.1f, 0);
            mCurrentAnim = desired;
        }

        mAnimator.Tick(dt);

        // ---- joint marker 跟随 ----
        const auto pose = mAnimator.Pose();
        const std::size_t n = std::min(pose.size(), mJointEntities.size());
        for (std::size_t i = 0; i < n; ++i)
        {
            const float lx = pose[i][3][0] * kSkeletonScale;
            const float ly = pose[i][3][1] * kSkeletonScale * kYAxisFlip;
            if (auto* xf = mWorld.GetComponent<TransformComponent>(mJointEntities[i]))
            {
                xf->position = mRootPos + glm::vec3{lx, ly, 0.0f};
            }
        }

        // ---- 帧末把 InputContext 状态机推进一步 ----
        mInput.BeginFrame();
    }

private:
    In::InputContext&     mInput;
    Phys::PhysicsWorld&   mPhys;
    Phys::BodyHandle      mCharBody;
    Ani::SkeletalAnimator& mAnimator;
    World&                mWorld;
    std::vector<Entity>   mJointEntities;
    SoundInstance&        mJumpSound;
    glm::vec3             mRootPos{0.0f, 0.0f, 0.0f};
    std::string           mCurrentAnim;
};

// ---------- ScenarioLayer：CLI 脚本化输入，验收用 ----------
//
// CI / 验收阶段需要在没人按键的情况下把角色推到 walk / jump 状态截图。
// ScenarioLayer 接管 InputContext 注入合成 key event：
//   * scenario="walk"：从第 5 帧起持续按住 D；
//   * scenario="jump"：从第 5 帧起按住 D（向右走），第 30 帧按下 Space
//     一次（产生 Pressed 触发）然后松开，第 60 帧已在跳跃中段。
// 其它（包括默认空字符串）= 不注入，走交互模式。
//
// ScenarioLayer 必须在 GameLayer **之前** push——它的 PostKeyEvent 要
// 让 GameLayer 同帧的 OnUpdate 看到 Pressed/Held。
class ScenarioLayer : public Layer
{
public:
    ScenarioLayer(In::InputContext& input, std::string scenario)
        : Layer("ScenarioLayer"), mInput(input), mScenario(std::move(scenario))
    {
    }

    void OnUpdate(const FrameContext& /*frame*/) override
    {
        ++mFrame;
        if (mScenario == "walk")
        {
            // 从第 5 帧起持续按 D；本侧每帧重新发一次 Down 模拟"键盘 repeat"
            // —— InputContext 在 Held 状态收到重复 Down 不会再 trigger，
            // 状态保持 Held。
            if (mFrame >= 5)
            {
                mInput.PostKeyEvent(In::KeyCode::D, true);
            }
        }
        else if (mScenario == "jump")
        {
            if (mFrame >= 5)
            {
                mInput.PostKeyEvent(In::KeyCode::D, true);
            }
            // 在指定帧 down → 下一帧 up，让 GameLayer 在 down 帧把它读
            // 成 Pressed（IsTriggered=true）触发跳跃 + 重触发音效。
            if (mFrame == 30)
            {
                mInput.PostKeyEvent(In::KeyCode::Space, true);
            }
            else if (mFrame == 31)
            {
                mInput.PostKeyEvent(In::KeyCode::Space, false);
            }
        }
    }

private:
    In::InputContext& mInput;
    std::string       mScenario;
    int               mFrame{0};
};

// ---------- LightRotationLayer：沿用旧版的灯光旋转，shadow 跟随 ----------
class LightRotationLayer : public Layer
{
public:
    LightRotationLayer(World& world, Entity light, Entity marker)
        : Layer("LightRotationLayer"), mWorld(world), mLight(light), mMarker(marker)
    {
    }

    void OnUpdate(const FrameContext& frame) override
    {
        const float t  = frame.time.totalSeconds * 0.5f;
        const float cx = std::cos(t);
        const float cz = std::sin(t);
        const glm::vec3 dir = glm::normalize(glm::vec3(cx * 0.6f, -1.0f, cz * 0.6f));

        // 方向由 light entity 的 Transform.rotation 派生 —— 直接改 rotation
        // 让 Pipeline 取到新方向。MakeDirectionalLightRotationFromDir 把
        // 世界方向反推回 quat。
        if (auto* lightXf = mWorld.GetComponent<TransformComponent>(mLight))
        {
            lightXf->rotation = Orange::Engine::Render::
                MakeDirectionalLightRotationFromDir(dir);
        }
        if (auto* xf = mWorld.GetComponent<TransformComponent>(mMarker))
        {
            xf->position = -dir * 4.0f;
        }
    }

private:
    World& mWorld;
    Entity mLight;
    Entity mMarker;
};

// ---------- RenderLayer：照搬旧版 ----------
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

}  // namespace

int main(int argc, char** argv)
{
    const auto captureCli = OrangeSamples::ParseCaptureCli(argc, argv);

    AppConfig cfg{};
    cfg.window.title  = "OrangeEngine - 07 full_pipeline (Phase 4 capstone)";
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

    // ---------- DragonBones runtime + skeleton ----------
    DBB::DragonBonesContext dbCtx;
    if (auto reg = assets.RegisterLoader<SkeletonAsset>(std::make_unique<SkeletonLoader>(dbCtx));
        reg.IsErr())
    {
        std::fprintf(stderr, "RegisterLoader<SkeletonAsset> failed\n");
        return 1;
    }

    const std::string skelPath = std::string(ORANGE_ENGINE_SKELETON_DATA_DIR)
                               + "/mecha_1406_ske.json";
    auto skelResult = assets.Load<SkeletonAsset>(skelPath);
    if (skelResult.IsErr())
    {
        std::fprintf(stderr, "Load<SkeletonAsset> failed (code=%u): %s\n",
                     static_cast<unsigned>(skelResult.Error()), skelPath.c_str());
        return 1;
    }
    const SkeletonAsset* skel = assets.Get(skelResult.Value());
    if (skel == nullptr || skel->Empty() || skel->Armatures().empty())
    {
        std::fprintf(stderr, "SkeletonAsset is empty\n");
        return 1;
    }
    const std::string armatureName = std::string(skel->Armatures()[0].name);
    Ani::SkeletalAnimator animator(dbCtx, *skel, armatureName);
    if (animator.BoneCount() == 0)
    {
        std::fprintf(stderr, "SkeletalAnimator BoneCount=0\n");
        return 1;
    }
    animator.Play("idle", 0.0f, /*playTimes=*/0);

    // ---------- Audio ----------
    AudioEngine audio;
    // 程序生成 880Hz 120ms beep——避免引入磁盘音频资源。AudioEngine
    // init 失败（没声卡 / WSL）时 SoundInstance 会变 invalid，Restart no-op，
    // demo 仍能跑（只是无声）。
    auto beepBytes = OrangeSamples::MakeBeepWav(/*freq=*/880.0f,
                                                /*durMs=*/120,
                                                /*sr=*/44100,
                                                /*vol=*/0.4f);
    SoundAsset jumpAsset(std::move(beepBytes), "<beep>");
    SoundInstance jumpSound = audio.CreateInstance(jumpAsset);

    // ---------- Mesh / Material ----------
    auto planeRes  = assets.Insert<MeshAsset>("builtin/plane",  MakePlaneMesh(2.5f));
    auto sphereRes = assets.Insert<MeshAsset>("builtin/sphere", MakeSphereMesh(0.7f, 32, 16));
    auto jointRes  = assets.Insert<MeshAsset>("builtin/joint_marker",
                                              MakeSphereMesh(0.06f, 12, 8));
    if (planeRes.IsErr() || sphereRes.IsErr() || jointRes.IsErr())
    {
        std::fprintf(stderr, "Insert<MeshAsset> failed\n");
        return 1;
    }
    const AssetHandle<MeshAsset> planeHandle  = planeRes.Value();
    const AssetHandle<MeshAsset> sphereHandle = sphereRes.Value();
    const AssetHandle<MeshAsset> jointHandle  = jointRes.Value();

    MaterialSystem materials(assets);
    if (auto rb = materials.RegisterBuiltins(); rb.IsErr())
    {
        std::fprintf(stderr, "RegisterBuiltins failed\n");
        return 1;
    }
    auto planeInstance  = materials.CreateInstance("textured");
    auto sphereInstance = materials.CreateInstance("rim_light");
    auto markerInstance = materials.CreateInstance("rim_light");
    auto jointInstance  = materials.CreateInstance("rim_light");
    if (!planeInstance || !sphereInstance || !markerInstance || !jointInstance)
    {
        std::fprintf(stderr, "CreateInstance failed\n");
        return 1;
    }

    // ---------- 场景 ----------
    World world;

    // Plane：地面，y=-2.5（与 ground physics body 顶面对齐）。
    Entity planeEntity = world.CreateEntity();
    {
        TransformComponent xf{};
        xf.position = {0.0f, -2.5f, 0.0f};
        world.AddComponent(planeEntity, xf);
        RenderableComponent r;
        r.mesh             = planeHandle;
        r.materialInstance = planeInstance.get();
        world.AddComponent(planeEntity, r);
    }

    // 装饰球：右侧悬浮，提供"非 mecha 物体"参照系。
    Entity sphereEntity = world.CreateEntity();
    {
        TransformComponent xf{};
        xf.position = {2.0f, -1.0f, 0.0f};
        world.AddComponent(sphereEntity, xf);
        RenderableComponent r;
        r.mesh             = sphereHandle;
        r.materialInstance = sphereInstance.get();
        world.AddComponent(sphereEntity, r);
    }

    // Light marker：旋转灯光的可视化。
    Entity markerEntity = world.CreateEntity();
    {
        TransformComponent xf{};
        xf.position = {0.0f, 4.0f, 0.0f};
        xf.scale    = {0.18f, 0.18f, 0.18f};
        world.AddComponent(markerEntity, xf);
        RenderableComponent r;
        r.mesh             = sphereHandle;
        r.materialInstance = markerInstance.get();
        r.castsShadow      = false;
        world.AddComponent(markerEntity, r);
    }

    // Joint markers：每根 mecha 骨头一个 entity。castsShadow=false 避免
    // 17 个小 caster 把 shadow map 喂爆 + 视觉上太杂。
    std::vector<Entity> jointEntities;
    jointEntities.reserve(animator.BoneCount());
    for (std::size_t i = 0; i < animator.BoneCount(); ++i)
    {
        Entity e = world.CreateEntity();
        TransformComponent xf{};
        xf.position = {0.0f, 0.0f, 0.0f};
        world.AddComponent(e, xf);
        RenderableComponent r;
        r.mesh             = jointHandle;
        r.materialInstance = jointInstance.get();
        r.castsShadow      = false;
        world.AddComponent(e, r);
        jointEntities.push_back(e);
    }

    // 主光：方向斜下，每帧旋转（LightSpinLayer 改 Transform.rotation）。
    Entity lightEntity = world.CreateEntity();
    {
        TransformComponent lightXf{};
        lightXf.rotation = Orange::Engine::Render::MakeDirectionalLightRotationFromDir(
            glm::vec3(0.6f, -1.0f, 0.4f));
        world.AddComponent(lightEntity, lightXf);

        DirectionalLight dl{};
        dl.color       = glm::vec3(1.0f, 0.95f, 0.85f);
        dl.intensity   = 1.2f;
        dl.castsShadow = true;
        world.AddComponent(lightEntity, dl);
    }

    // 摄像机：从前上方看，让 mecha 居中、装饰球与 plane 都在视野内。
    Entity camEntity = world.CreateEntity();
    {
        const float aspect = static_cast<float>(cfg.window.width)
                           / static_cast<float>(cfg.window.height);
        Camera cam = Camera::Perspective(glm::radians(50.0f), aspect, 0.1f, 100.0f);
        cam.view = glm::lookAt(glm::vec3(0.0f, 0.5f, 6.5f),
                               glm::vec3(0.0f, -0.5f, 0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent(camEntity, cam);
    }

    // ---------- Physics ----------
    Phys::PhysicsWorld physWorld;

    // Static ground：BoxCollider 顶面 y=-2.5（与视觉 plane 对齐）。
    {
        Phys::RigidBodyComponent rb;
        rb.type            = Phys::BodyType::Static;
        rb.initialPosition = {0.0f, kGroundY - 0.25f};
        Phys::ColliderComponent col;
        col.shape       = Phys::BoxDesc{{50.0f, 0.25f}, {0.0f, 0.0f}};
        col.friction    = 0.5f;
        col.restitution = 0.0f;
        if (!physWorld.AddBody(rb, col).IsValid())
        {
            std::fprintf(stderr, "AddBody(ground) failed\n");
            return 1;
        }
    }

    // Dynamic 角色：CircleCollider 半径 0.4，初始位置在 ground 上方一点。
    Phys::BodyHandle charBody;
    {
        Phys::RigidBodyComponent rb;
        rb.type            = Phys::BodyType::Dynamic;
        rb.initialPosition = {0.0f, kGroundY + kCharacterRadius + 0.05f};
        rb.fixedRotation   = true;   // 角色不该随物理倒下
        Phys::ColliderComponent col;
        col.shape       = Phys::CircleDesc{kCharacterRadius, {0.0f, 0.0f}};
        col.density     = 1.0f;
        col.friction    = 0.4f;
        col.restitution = 0.0f;
        charBody = physWorld.AddBody(rb, col);
        if (!charBody.IsValid())
        {
            std::fprintf(stderr, "AddBody(character) failed\n");
            return 1;
        }
    }

    // ---------- Input ----------
    In::InputContext input;
    {
        const std::string actionsPath = std::string(ORANGE_ENGINE_ACTIONS_DIR)
                                      + "/default.actions.json";
        auto mapResult = In::LoadActionMapFromFile(actionsPath);
        if (mapResult.IsErr())
        {
            std::fprintf(stderr, "LoadActionMapFromFile failed (code=%u): %s\n",
                         static_cast<unsigned>(mapResult.Error()), actionsPath.c_str());
            return 1;
        }
        input.Push(std::move(mapResult.Value()));
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

    // ---------- Layers ----------
    // ScenarioLayer 在 GameLayer 之前 push——脚本化 key event 在 GameLayer
    // 同帧 OnUpdate 内被读到。空 scenario = 不注入，走交互模式。
    if (!captureCli.scenario.empty())
    {
        host->PushLayer(std::make_unique<ScenarioLayer>(input, captureCli.scenario));
    }
    // GameLayer：input + 物理 + 动画——主逻辑层。
    host->PushLayer(std::make_unique<GameLayer>(input, physWorld, charBody,
                                                animator, world,
                                                jointEntities, jumpSound));
    // 灯光旋转放在 Game 之后，避免影响角色时序。
    host->PushLayer(std::make_unique<LightRotationLayer>(world, lightEntity, markerEntity));
    // CaptureLayer 必须在 RenderLayer 之前 push——它的 RequestCapture 要
    // 让本帧 Render 抓走。
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
