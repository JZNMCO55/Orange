// samples/05_skeletal_animation —— Phase 4 / Task 10
//
// 场景：
//   * 一块 plane（textured Material）作背景；
//   * 17 个小球（rim_light Material）作 mecha 骨架的 joint marker——每
//     帧从 SkeletalAnimator.Pose() 取每根骨骼的世界变换，把 translation
//     映射到 joint marker 的 TransformComponent.position；
//   * DirectionalLight + 默认 PostProcessChain（沿用 sample 07 视觉基线）。
//
// Phase 4 范围 SkeletalAnimator 只算 CPU palette，**不**做 GPU skinning——
// 把 vertex 绑到 bone 索引、palette 走 UBO push、vertex shader 累乘
// skin matrix 等渲染端工作量留 Phase 6 / Material UBO 落地配套做。本
// sample 用"骨头位置画成小球"的方式可视化，让人眼能验证 idle 动画
// 时骨架在动。
//
// DragonBones 资源（mecha_1004d_show_ske.json）的 AABB ≈ 757×577 px，
// 与渲染器 [-2..2] 单位空间不在同一量级——sample 端 KSkeletonScale =
// 1/200 缩成 ~3.8×2.9 单位的人形，刚好在 plane 上居中可视。

#include "animation/dragonbones/DragonBonesContext.h"  // src/

#include "CaptureLayer.h"  // samples/common/

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

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Orange::Engine;
using Orange::Engine::Asset::AssetHandle;
using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Asset::MeshAsset;
using Orange::Engine::Asset::ShaderAsset;
using Orange::Engine::Asset::ShaderLoader;
using Orange::Engine::Asset::SkeletonAsset;
using Orange::Engine::Asset::SkeletonLoader;
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
using Orange::Engine::Render::ShadowConfig;
using Orange::Engine::Scene::TransformComponent;
namespace DBB = Orange::Engine::Animation::DragonBonesBackend;
namespace Ani = Orange::Engine::Animation;

namespace
{

#ifndef ORANGE_ENGINE_SKELETON_DATA_DIR
#  error "ORANGE_ENGINE_SKELETON_DATA_DIR must be defined by CMake"
#endif

// Skeleton 像素 → 渲染单位的缩放系数。mecha AABB ≈ 757×577 px → 缩成
// ~3.8×2.9 单位，与 sample 07 的 plane halfSize=2.5 / cube 量级一致。
constexpr float kSkeletonScale = 1.0f / 200.0f;

// DragonBones 默认 y 轴 down（屏幕坐标）；本侧渲染器 y 轴 up——可视
// 化时把 y 取反，让 mecha 站立而不是头朝下。
constexpr float kYAxisFlip = -1.0f;

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

class SkeletonLayer : public Layer
{
public:
    SkeletonLayer(Ani::SkeletalAnimator& animator,
                  World&                 world,
                  std::vector<Entity>    jointEntities)
        : Layer("SkeletonLayer")
        , mAnimator(animator)
        , mWorld(world)
        , mJointEntities(std::move(jointEntities))
    {
    }

    void OnUpdate(const FrameContext& frame) override
    {
        float dt = frame.time.deltaSeconds;
        if (dt > 1.0f / 30.0f) { dt = 1.0f / 30.0f; }
        mAnimator.Tick(dt);

        // Pose 与 SkeletonAsset.boneNames 同 sortedBones 顺序——本期 joint
        // marker 数组按这个顺序构造，下标一一对应。
        const auto pose = mAnimator.Pose();
        const std::size_t n = std::min(pose.size(), mJointEntities.size());
        for (std::size_t i = 0; i < n; ++i)
        {
            // Pose 内是 ToMat4 嵌入的 2D affine：tx 在 m[3][0]、ty 在 m[3][1]。
            // 缩放 + Y 翻转，z=0（mecha 是 2D 骨架，让人形在 X-Y 平面）。
            const float wx = pose[i][3][0] * kSkeletonScale;
            const float wy = pose[i][3][1] * kSkeletonScale * kYAxisFlip;
            if (auto* xf = mWorld.GetComponent<TransformComponent>(mJointEntities[i]))
            {
                xf->position = glm::vec3{wx, wy, 0.0f};
            }
        }
    }

private:
    Ani::SkeletalAnimator&  mAnimator;
    World&                  mWorld;
    std::vector<Entity>     mJointEntities;
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

}  // namespace

int main(int argc, char** argv)
{
    const auto captureCli = OrangeSamples::ParseCaptureCli(argc, argv);

    AppConfig cfg{};
    cfg.window.title  = "OrangeEngine - 05 skeletal_animation";
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

    // ---------- DragonBones runtime ----------
    DBB::DragonBonesContext dbCtx;
    if (auto reg = assets.RegisterLoader<SkeletonAsset>(std::make_unique<SkeletonLoader>(dbCtx));
        reg.IsErr())
    {
        std::fprintf(stderr, "RegisterLoader<SkeletonAsset> failed\n");
        return 1;
    }

    const std::string skelPath = std::string(ORANGE_ENGINE_SKELETON_DATA_DIR)
                               + "/mecha_1004d_show_ske.json";
    auto skelHandle = assets.Load<SkeletonAsset>(skelPath);
    if (skelHandle.IsErr())
    {
        std::fprintf(stderr, "Load<SkeletonAsset> failed (code=%u): %s\n",
                     static_cast<unsigned>(skelHandle.Error()), skelPath.c_str());
        return 1;
    }
    const SkeletonAsset* skel = assets.Get(skelHandle.Value());
    if (skel == nullptr || skel->Empty())
    {
        std::fprintf(stderr, "SkeletonAsset is empty\n");
        return 1;
    }

    // SkeletalAnimator 用第一条 armature——mecha 资源里只有一条，
    // 名字 "mecha_1004d_show"。
    const auto armatures = skel->Armatures();
    if (armatures.empty())
    {
        std::fprintf(stderr, "No armature in SkeletonAsset\n");
        return 1;
    }
    const std::string armatureName = armatures[0].name;
    Ani::SkeletalAnimator animator(dbCtx, *skel, armatureName);
    if (animator.BoneCount() == 0)
    {
        std::fprintf(stderr, "SkeletalAnimator BoneCount=0 (armature build failed)\n");
        return 1;
    }
    // mecha_1004d_show_ske.json 内动画 "idle" 80 帧（24fps，约 3.3 秒）；
    // playTimes=0 → loop 永远播。
    if (!armatures[0].animationNames.empty())
    {
        animator.Play(armatures[0].animationNames[0], 0.0f, /*playTimes=*/0);
    }

    // ---------- mesh / material ----------
    auto planeRes  = assets.Insert<MeshAsset>("builtin/plane",  MakePlaneMesh(3.0f));
    auto sphereRes = assets.Insert<MeshAsset>("builtin/joint_marker",
                                              MakeSphereMesh(0.05f, 12, 8));
    if (planeRes.IsErr() || sphereRes.IsErr())
    {
        std::fprintf(stderr, "Insert<MeshAsset> failed\n");
        return 1;
    }
    const AssetHandle<MeshAsset> planeHandle  = planeRes.Value();
    const AssetHandle<MeshAsset> sphereHandle = sphereRes.Value();

    MaterialSystem materials(assets);
    if (auto rb = materials.RegisterBuiltins(); rb.IsErr())
    {
        std::fprintf(stderr, "RegisterBuiltins failed\n");
        return 1;
    }
    auto planeInstance  = materials.CreateInstance("textured");
    auto jointInstance  = materials.CreateInstance("rim_light");
    if (!planeInstance || !jointInstance)
    {
        std::fprintf(stderr, "CreateInstance failed\n");
        return 1;
    }

    // ---------- 场景 ----------
    World world;

    // 背景 plane：放在 y=-2.5（mecha 脚下）。
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

    // Joint marker：每根骨头一个 entity；初始位置都在 origin，由
    // SkeletonLayer 每帧从 Pose() 更新。所有 joint 共享同一个
    // jointInstance（rim_light），让 mecha 整体看起来像一组发光节点。
    std::vector<Entity> jointEntities;
    jointEntities.reserve(animator.BoneCount());
    for (std::size_t i = 0; i < animator.BoneCount(); ++i)
    {
        Entity e = world.CreateEntity();
        TransformComponent xf{};
        xf.position = {0.0f, 0.0f, 0.0f};
        world.AddComponent(e, xf);
        RenderableComponent r;
        r.mesh             = sphereHandle;
        r.materialInstance = jointInstance.get();
        r.castsShadow      = false;  // joint 太多，关掉 shadow 减负
        world.AddComponent(e, r);
        jointEntities.push_back(e);
    }

    // 主光：方向斜下。
    Entity lightEntity = world.CreateEntity();
    {
        DirectionalLight dl{};
        dl.direction   = glm::normalize(glm::vec3(0.4f, -1.0f, 0.6f));
        dl.color       = glm::vec3(1.0f, 0.95f, 0.85f);
        dl.intensity   = 1.2f;
        dl.castsShadow = true;
        world.AddComponent(lightEntity, dl);
    }

    // 相机：从前方正视 mecha。mecha 中心约在 (0, 0, 0)，AABB ~3.8×2.9 单
    // 位，camera Z=6 让人形占画面 1/2 高度。
    Entity camEntity = world.CreateEntity();
    {
        const float aspect = static_cast<float>(cfg.window.width)
                           / static_cast<float>(cfg.window.height);
        Camera cam = Camera::Perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        cam.view = glm::lookAt(glm::vec3(0.0f, 0.5f, 6.0f),
                               glm::vec3(0.0f, 0.0f, 0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent(camEntity, cam);
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

    host->PushLayer(std::make_unique<SkeletonLayer>(animator, world, jointEntities));
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
