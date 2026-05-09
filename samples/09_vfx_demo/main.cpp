// samples/09_vfx_demo —— VfxSystem 粒子骨架的视觉验收。
//
// 屏幕中央一个 fountain 发射器：粒子带 HDR 颜色（alpha > 1）从 entity
// 原点向上喷出，受重力下拉、寿命中颜色从黄红渐变到深红，alpha 衰减
// 到 0。Pipeline 在主 pass 之后 / bloom 之前调一次 VfxSystem::DrawParticles，
// 把粒子写到 HDR target——bloom pass 自动拾取超亮像素生成光晕。
//
// 后续若加内置 dissolve / emissive 模板，会在同 sample 里追加一个走
// dissolve 的旋转 mesh + 一个 emissive mesh，做完整 VFX 视觉收尾。

#include <orange/engine/app/AppConfig.h>
#include <orange/engine/app/AppHost.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/app/Layer.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/platform/WindowEvent.h>
#include <orange/engine/render/BuiltinPostProcessChain.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/ParticleEmitterComponent.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/PostProcessChain.h>
#include <orange/engine/render/PostProcessPasses.h>
#include <orange/engine/render/VfxSystem.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include "../common/CaptureLayer.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>
#include <memory>
#include <utility>

using namespace Orange::Engine;
using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Asset::ShaderAsset;
using Orange::Engine::Asset::ShaderLoader;
using Orange::Engine::Render::BloomPass;
using Orange::Engine::Render::BuiltinPostProcessChain::CreateDefault;
using Orange::Engine::Render::Camera;
using Orange::Engine::Render::ParticleEmitterComponent;
using Orange::Engine::Render::ParticleEmitterDesc;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::PostProcessChain;
using Orange::Engine::Render::VfxSystem;
using Orange::Engine::Scene::TransformComponent;

namespace
{

class VfxLayer : public Layer
{
public:
    VfxLayer(Pipeline& pipeline, VfxSystem& vfx, World& world)
        : Layer("VfxLayer"), mPipeline(pipeline), mVfx(vfx), mWorld(world)
    {
    }

    void OnUpdate(const FrameContext& frame) override
    {
        // Sim 在 Pipeline.Render 之前推进——VfxSystem 不接管 sim 时序，
        // 让调用方把 Tick 安排在希望的"逻辑相位"。
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

    // 世界：一个相机 + 一个 fountain 发射器。
    World world;

    Entity emitterEntity = world.CreateEntity();
    {
        TransformComponent tx{};
        tx.position = glm::vec3(0.0f, -1.5f, 0.0f);  // 屏幕下方一点
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

    Entity camEntity = world.CreateEntity();
    {
        const float aspect = static_cast<float>(cfg.window.width)
                           / static_cast<float>(cfg.window.height);
        Camera cam = Camera::Perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
        cam.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f),
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
        bp->threshold = 0.6f;   // 低阈值 + 高粒子 alpha → halo 显著
        bp->intensity = 1.0f;
    }
    pipeline.SetPostProcessChain(&chain);

    VfxSystem vfx;
    pipeline.SetVfxSystem(&vfx);   // 自动 Initialize VfxSystem 的 GPU 资源

    if (!captureCli.outPath.empty())
    {
        // CaptureLayer 必须在 RenderLayer 之前 push（先请求 capture，
        // 同帧 RenderLayer 触发 readback）。
        host->PushLayer(std::make_unique<OrangeSamples::CaptureLayer>(
            pipeline, *host, captureCli.outPath, captureCli.captureFrame));
    }

    host->PushLayer(std::make_unique<VfxLayer>(pipeline, vfx, world));

    const int rc = host->Run();

    pipeline.SetVfxSystem(nullptr);
    vfx.Shutdown();
    pipeline.Shutdown();
    return rc;
}
