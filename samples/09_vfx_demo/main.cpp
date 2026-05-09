// samples/09_vfx_demo —— VFX 子系统视觉验收（粒子 + dissolve + emissive）。
//
// 屏幕内容：
//   * 中央偏下：fountain 发射器，粒子带 HDR 颜色（alpha > 1）从原点向
//     上喷出、受重力下拉，颜色从黄红渐变到深红。Pipeline 在主 pass 后 /
//     bloom 前调一次 VfxSystem::DrawParticles，粒子写到同一 HDR target
//     自然喂 bloom；
//   * 左侧：旋转 cube 走 dissolve 模板——shader 内部用 light UBO 的
//     uFrameInfo.x 自驱 dissolve_t（pingpong 0..1..0），呈现"逐格消融
//     + 边沿发光"循环；
//   * 右侧：静止 cube 走 emissive 模板——直接输出 HDR > 1 的暖白色，
//     bloom pass 自动给出光晕。
//
// 这三个子项是 VFX 子系统对游戏侧"史莱姆 dissolve / 发光眼 / 粒子尾迹"
// 等典型效果的 building block。per-instance 调参（dissolve 速度 / emissive
// 颜色等）等 Material UBO 路径上线后再补，本 sample 用内置 hardcode 值
// 即可视觉到位。

#include <orange/engine/app/AppConfig.h>
#include <orange/engine/app/AppHost.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/app/Layer.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/platform/WindowEvent.h>
#include <orange/engine/render/BuiltinPostProcessChain.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/render/ParticleEmitterComponent.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/PostProcessChain.h>
#include <orange/engine/render/PostProcessPasses.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/render/VfxSystem.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include "../common/CaptureLayer.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstdio>
#include <memory>
#include <utility>

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
using Orange::Engine::Render::ParticleEmitterComponent;
using Orange::Engine::Render::ParticleEmitterDesc;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::PostProcessChain;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Render::VfxSystem;
using Orange::Engine::Scene::TransformComponent;

namespace
{

// 立方体几何——与 04_3d_mesh_with_bloom 同布局：6 面 × 4 顶点，每面
// 自带本地 UV (0,0)..(1,1)。dissolve / emissive 都按 UV 采样阈值 / 强度。
struct CubeFace
{
    std::array<VertexPosition3, 4> positions;
};

constexpr std::array<CubeFace, 6> kCubeFaces = {{
    {{{{ 0.5f, -0.5f,  0.5f},
       { 0.5f, -0.5f, -0.5f},
       { 0.5f,  0.5f, -0.5f},
       { 0.5f,  0.5f,  0.5f}}}},
    {{{{-0.5f, -0.5f, -0.5f},
       {-0.5f, -0.5f,  0.5f},
       {-0.5f,  0.5f,  0.5f},
       {-0.5f,  0.5f, -0.5f}}}},
    {{{{-0.5f,  0.5f,  0.5f},
       { 0.5f,  0.5f,  0.5f},
       { 0.5f,  0.5f, -0.5f},
       {-0.5f,  0.5f, -0.5f}}}},
    {{{{-0.5f, -0.5f, -0.5f},
       { 0.5f, -0.5f, -0.5f},
       { 0.5f, -0.5f,  0.5f},
       {-0.5f, -0.5f,  0.5f}}}},
    {{{{-0.5f, -0.5f,  0.5f},
       { 0.5f, -0.5f,  0.5f},
       { 0.5f,  0.5f,  0.5f},
       {-0.5f,  0.5f,  0.5f}}}},
    {{{{ 0.5f, -0.5f, -0.5f},
       {-0.5f, -0.5f, -0.5f},
       {-0.5f,  0.5f, -0.5f},
       { 0.5f,  0.5f, -0.5f}}}},
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

    return std::make_unique<MeshAsset>(std::move(positions),
                                       std::move(uvs),
                                       std::move(indices));
}

class VfxLayer : public Layer
{
public:
    VfxLayer(Pipeline& pipeline, VfxSystem& vfx, World& world, Entity dissolveCube)
        : Layer("VfxLayer")
        , mPipeline(pipeline)
        , mVfx(vfx)
        , mWorld(world)
        , mDissolveCube(dissolveCube)
    {
    }

    void OnUpdate(const FrameContext& frame) override
    {
        // dissolve cube 慢转一下，让消融纹理在不同朝向上都能看到。
        if (auto* xf = mWorld.GetComponent<TransformComponent>(mDissolveCube))
        {
            const float angle = static_cast<float>(frame.time.totalSeconds) * 0.6f;
            xf->rotation = glm::angleAxis(angle, glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
        }

        // 把 elapsed 时间喂给 Pipeline——dissolve frag 通过 light UBO 的
        // uFrameInfo.x 读到，自驱 dissolve_t pingpong。
        mPipeline.SetFrameTime(static_cast<float>(frame.time.totalSeconds));

        // Sim 在 Render 之前推进——VfxSystem 不接管 sim 时序。
        mVfx.Tick(mWorld, static_cast<float>(frame.time.deltaSeconds));
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
    Pipeline&  mPipeline;
    VfxSystem& mVfx;
    World&     mWorld;
    Entity     mDissolveCube;
};

}  // namespace

int main(int argc, char** argv)
{
    const auto captureCli = OrangeSamples::ParseCaptureCli(argc, argv);

    AppConfig cfg{};
    cfg.window.title  = "OrangeEngine - 09 vfx_demo";
    cfg.window.width  = 1280;
    cfg.window.height = 720;

    auto hostResult = AppHost::Create(cfg);
    if (hostResult.IsErr())
    {
        std::fprintf(stderr,
                     "AppHost::Create failed (code=%u)\n",
                     static_cast<unsigned>(hostResult.Error()));
        return 1;
    }
    auto host = std::move(hostResult).Value();

    AssetRegistry assets;
    if (auto reg = assets.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
        reg.IsErr())
    {
        std::fprintf(stderr,
                     "AssetRegistry::RegisterLoader<ShaderAsset> failed (code=%u)\n",
                     static_cast<unsigned>(reg.Error()));
        return 1;
    }
    auto meshHandleResult = assets.Insert<MeshAsset>("builtin/cube", MakeCubeMesh());
    if (meshHandleResult.IsErr())
    {
        std::fprintf(stderr,
                     "AssetRegistry::Insert<MeshAsset> failed (code=%u)\n",
                     static_cast<unsigned>(meshHandleResult.Error()));
        return 1;
    }
    AssetHandle<MeshAsset> meshHandle = meshHandleResult.Value();

    MaterialSystem materials(assets);
    if (auto rb = materials.RegisterBuiltins(); rb.IsErr())
    {
        std::fprintf(stderr,
                     "MaterialSystem::RegisterBuiltins failed (code=%u)\n",
                     static_cast<unsigned>(rb.Error()));
        return 1;
    }
    auto dissolveInstance = materials.CreateInstance("dissolve");
    auto emissiveInstance = materials.CreateInstance("emissive");
    if (!dissolveInstance || !emissiveInstance)
    {
        std::fprintf(stderr,
                     "MaterialSystem::CreateInstance(dissolve/emissive) returned null\n");
        return 1;
    }

    World world;

    // dissolve cube：左侧、旋转、走 dissolve 模板
    Entity dissolveCube = world.CreateEntity();
    {
        TransformComponent tx{};
        tx.position = glm::vec3(-2.2f, 0.0f, 0.0f);
        world.AddComponent(dissolveCube, tx);

        RenderableComponent r;
        r.mesh             = meshHandle;
        r.materialInstance = dissolveInstance.get();
        world.AddComponent(dissolveCube, r);
    }

    // emissive cube：右侧、静止、走 emissive 模板
    Entity emissiveCube = world.CreateEntity();
    {
        TransformComponent tx{};
        tx.position = glm::vec3(2.2f, 0.0f, 0.0f);
        // 给 emissive cube 一个固定朝向，让能多看到几个面而不是正方
        // 形剪影；emissive 模板按 UV 做 vignette，正方形面看起来更立体。
        tx.rotation = glm::angleAxis(0.6f, glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
        world.AddComponent(emissiveCube, tx);

        RenderableComponent r;
        r.mesh             = meshHandle;
        r.materialInstance = emissiveInstance.get();
        world.AddComponent(emissiveCube, r);
    }

    // fountain 发射器：屏幕下方、HDR 粒子流
    Entity emitterEntity = world.CreateEntity();
    {
        TransformComponent tx{};
        tx.position = glm::vec3(0.0f, -1.5f, 0.0f);
        world.AddComponent(emitterEntity, tx);

        ParticleEmitterDesc desc{};
        desc.emissionRate       = 60.0f;
        desc.lifetimeMin        = 1.4f;
        desc.lifetimeMax        = 2.2f;
        desc.spawnOffsetMin     = {-0.05f, 0.0f};
        desc.spawnOffsetMax     = { 0.05f, 0.0f};
        desc.initialVelocityMin = {-1.2f, 2.6f};
        desc.initialVelocityMax = { 1.2f, 4.4f};
        desc.gravity            = { 0.0f, -6.0f};
        // Alpha 大幅过 1 让 bloom 拾取；粒子尺寸做大避免 Karis 平均把
        // 单像素 firefly 直接抑掉（bloom downsample 的天然行为）。
        desc.colorStart         = { 1.0f, 0.85f, 0.35f, 4.0f};
        desc.colorEnd           = { 0.95f, 0.20f, 0.05f, 0.0f};
        desc.sizeStart          = 0.18f;
        desc.sizeEnd            = 0.28f;
        desc.maxParticles       = 256;
        world.AddComponent<ParticleEmitterComponent>(emitterEntity, {desc, true});
    }

    // 主光：dissolve / emissive frag 都声明了 light UBO（set 0 binding 1）
    // 即使 emissive 不读 NdotL，light UBO 也得有合法内容，避免 fragment
    // 取到未初始化值。这条与其它 builtin sample 同模式。
    Entity lightEntity = world.CreateEntity();
    {
        DirectionalLight dl{};
        dl.direction   = glm::normalize(glm::vec3(0.3f, -1.0f, 0.4f));
        dl.color       = glm::vec3(1.0f, 0.97f, 0.92f);
        dl.intensity   = 1.2f;
        dl.castsShadow = false;
        world.AddComponent(lightEntity, dl);
    }

    Entity camEntity = world.CreateEntity();
    {
        const float aspect = static_cast<float>(cfg.window.width)
                           / static_cast<float>(cfg.window.height);
        Camera cam = Camera::Perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
        cam.view = glm::lookAt(glm::vec3(0.0f, 0.5f, 6.5f),
                               glm::vec3(0.0f, 0.0f, 0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent(camEntity, cam);
    }

    Pipeline pipeline;
    auto initResult = pipeline.Initialize(host->GetWindow(), assets);
    if (initResult.IsErr())
    {
        std::fprintf(stderr,
                     "Pipeline::Initialize failed (code=%u)\n",
                     static_cast<unsigned>(initResult.Error()));
        return 1;
    }

    PostProcessChain chain = CreateDefault();
    if (auto* bp = dynamic_cast<BloomPass*>(chain.FindByName("bloom")))
    {
        bp->threshold = 0.6f;
        bp->intensity = 1.0f;
    }
    // 启用 god rays —— sunWorldDir 选一个让 sun 投影落在屏内的方向：
    // 太阳本身（-sunDir 方向）应该出现在 camera frustum 内，否则屏幕
    // 空间径向模糊采不到 sun disk → 视觉上 god rays 不可见（与
    // PostProcessPasses.h 文档里"屏幕空间 god rays 的天然限制"一致）。
    // 这里调成 (0.3, -0.15, 0.6) → 太阳投影到 ~uv (0.25, 0.21)，屏幕
    // 左上区域，光柱朝右下扩散经过粒子流 + emissive cube。
    if (auto* gp = dynamic_cast<Orange::Engine::Render::GodRaysPass*>(
            chain.FindByName("god_rays")))
    {
        gp->enabled     = true;
        gp->sunWorldDir = glm::normalize(glm::vec3(0.3f, -0.15f, 0.6f));
        gp->sunColor    = glm::vec3(1.0f, 0.92f, 0.75f);
        gp->density     = 1.4f;
        gp->decay       = 0.97f;
        gp->weight      = 0.045f;
        gp->exposure    = 1.0f;
        gp->numSamples  = 96;
    }
    pipeline.SetPostProcessChain(&chain);
    pipeline.SetMaterialSystem(&materials);

    VfxSystem vfx;
    pipeline.SetVfxSystem(&vfx);

    if (!captureCli.outPath.empty())
    {
        host->PushLayer(std::make_unique<OrangeSamples::CaptureLayer>(
            pipeline, *host, captureCli.outPath, captureCli.captureFrame));
    }

    host->PushLayer(std::make_unique<VfxLayer>(pipeline, vfx, world, dissolveCube));

    const int rc = host->Run();

    pipeline.SetVfxSystem(nullptr);
    vfx.Shutdown();
    pipeline.Shutdown();
    return rc;
}
