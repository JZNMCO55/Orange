// samples/18_csm_large_scene —— CSM (Cascaded Shadow Maps) 大场景 showcase fixture
// （GAP-2026-05-27-cascaded-shadow-maps C1 + C2 + C3 验收 follow-up sample）。
//
// 与 sample 16（光源族 + ±10 场景）不同，本 sample 故意把 ground 拉到 ±50 + 把
// 5 个 sphere caster 沿 +Z 放在 (3, 10, 22, 40, 70) 五段距离，相机低角度看向 +Z
// 让 ground 拉成"远方延伸"——这是 directional shadow 必须靠 CSM 才能不漏 +
// 不糊的典型 outdoor 配置。
//
// 看点（cascadeCount=3 默认开 CSM 时）：
//   * 近 caster (z=3, 10) 落在 cascade 0/1，shadow 边沿锐
//   * 远 caster (z=40, 70) 落在 cascade 2，shadow 仍可见而不是"超出 ±10 box
//     就漏"——这是 C1 单 cascade 固定 ortho 解决不了的
//   * 三段 cascade 边界 5% NDC 区由 C3 cross-cascade blend 平滑过渡，无硬切
//
// 视觉对照（命令行 flag）：
//   --no-csm  : 强制 shadowConfig.cascadeCount=1（C1 fallback 路径），远 caster
//               阴影直接漏（落在 ±10 ortho box 外不写 shadow map），近 caster
//               阴影也粗（分辨率均摊到 20×20 单位）
//   --pcss N  : 启用 PCSS 软阴影，lightSize=N texel（默认 0 = 关，纯 PCF 锐边
//               便于辨识 CSM 分辨率差异）
//   --tint    : cascade tint overlay —— pbr.frag 在最终输出上 mix per-cascade
//               颜色（cascade 0=红 / 1=绿 / 2=蓝 / 3=黄），让 cascade 分段直观
//               可见。配合 --no-csm 看到整画面单色（cascade 0），开 CSM 看到
//               场景按距离染三段色 = CSM 分段的最直白证据
//   --motion  : 相机 sin-wave 左右 + 前后摇摆 —— 验 texel snap anti-shimmer
//               是否工作（CSM 路径阴影 edge 应沿 caster 稳定，不抖；C1 fallback
//               同样不抖因为 ortho 也是世界静止）
//   --capture <path>  : 渲一帧 PNG 后自动退（CI / 文档无人值守出图）
//
// 故意不挂的：spot / point light（干扰纯 directional CSM 分析）+ SSAO/SSR/DoF/TAA
// 等高级后处理（同上，影响纯 shadow 观察）。后处理只有 bloom + tonemap 维持
// HDR → LDR 收尾。
//
// CSM 数学要求相机 perspective（cascade split 用 camera near/far），无法用
// orthographic camera；本 sample 走 perspective 60° fov / near=0.5 / far=120。

#include <orange/engine/app/AppConfig.h>
#include <orange/engine/app/AppHost.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/app/Layer.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/platform/Window.h>
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
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
using Orange::Engine::Asset::VertexPosition3;
using Orange::Engine::Asset::VertexUV2;
using Orange::Engine::Render::Camera;
using Orange::Engine::Render::DirectionalLight;
using Orange::Engine::Render::MakeDirectionalLightRotationFromDir;
using Orange::Engine::Render::MaterialInstance;
using Orange::Engine::Render::MaterialSystem;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::PostProcessChain;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Render::ShadowConfig;
using Orange::Engine::Render::BuiltinPostProcessChain::CreateDefault;
using Orange::Engine::Scene::TransformComponent;

namespace
{

    // UV-sphere（与 sample 16 同构造）
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
                const float u   = static_cast<float>(j) / static_cast<float>(lon);
                const float phi = u * glm::two_pi<float>();
                positions.push_back({radius * sinT * std::cos(phi),
                                     radius * cosT,
                                     radius * sinT * std::sin(phi)});
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
        auto pMesh = std::make_unique<MeshAsset>(std::move(positions), std::move(uvs),
                                                 std::move(indices));
        pMesh->ComputeSmoothNormalsFromTriangles();
        return pMesh;
    }

    // 大 XZ 平面 ground（halfExtent=50 → 100×100 单位）—— 故意远 > ±10 box，让 C1
    // fallback (--no-csm) 在远处直接漏阴影 / CSM 默认路径远处仍有阴影的对比戏剧化。
    std::unique_ptr<MeshAsset> MakeFloorMesh(float halfExtent)
    {
        const float                  h         = halfExtent;
        std::vector<VertexPosition3> positions = {
            {-h, 0.0f, h},
            {h, 0.0f, h},
            {h, 0.0f, -h},
            {-h, 0.0f, -h},
        };
        std::vector<VertexUV2> uvs = {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
            {0.0f, 1.0f},
        };
        std::vector<std::uint32_t> indices = {0, 1, 2, 0, 2, 3};
        auto                       pMesh   = std::make_unique<MeshAsset>(std::move(positions), std::move(uvs),
                                                                         std::move(indices));
        pMesh->ComputeSmoothNormalsFromTriangles();
        return pMesh;
    }

    class RenderLayer : public Layer
    {
    public:
        RenderLayer(Pipeline& pipeline, World& world, Platform::Window& window,
                    std::string capturePath, bool motionEnabled, Entity cameraEntity)
            : Layer("RenderLayer"), mPipeline(pipeline), mWorld(world), mWindow(window),
              mCapturePath(std::move(capturePath)),
              mMotionEnabled(motionEnabled),
              mCameraEntity(cameraEntity) {}

        void OnUpdate(const FrameContext& /*frame*/) override
        {
            // 运动相机：每帧改 Camera.view（sin-wave 左右 + 前后摇摆）。capture 模式
            // 在 kCaptureFrame 那一帧的位置可复现（mFrame 是单调递增）；交互模式肉眼
            // 看 shadow edge 跟 caster 稳定不抖 = texel snap anti-shimmer 工作。
            if (mMotionEnabled)
            {
                const float t     = static_cast<float>(mFrame) * (1.0f / 60.0f); // 假设 ~60fps
                const float swayX = std::sin(t * 0.7f) * 6.0f;
                const float swayZ = std::cos(t * 0.5f) * 4.0f;
                auto&       cam   = mWorld.Registry().get<Camera>(World::ToEntt(mCameraEntity));
                cam.view          = glm::lookAt(glm::vec3(swayX, 3.5f, -6.0f + swayZ),
                                                glm::vec3(swayX * 0.3f, 0.5f, 30.0f),
                                                glm::vec3(0.0f, 1.0f, 0.0f));
            }

            if (!mCapturePath.empty() && mFrame == kCaptureFrame)
            {
                mPipeline.RequestCapture(std::filesystem::path(mCapturePath));
            }
            mPipeline.Render(mWorld);
            if (!mCapturePath.empty() && mFrame >= kCaptureFrame + 1)
            {
                mWindow.RequestClose();
            }
            ++mFrame;
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
        static constexpr std::uint64_t kCaptureFrame = 8; // 无 TAA，少预热几帧即可
        Pipeline&                      mPipeline;
        World&                         mWorld;
        Platform::Window&              mWindow;
        std::string                    mCapturePath;
        bool                           mMotionEnabled{false};
        Entity                         mCameraEntity{};
        std::uint64_t                  mFrame{0};
    };

    // 在 (x, z) 放一个落在地面上的球 caster（半径 r，球心 y=r）。
    Entity SpawnSphere(World& world, AssetHandle<MeshAsset> mesh,
                       MaterialInstance* inst, float x, float z, float r)
    {
        Entity             e = world.CreateEntity();
        TransformComponent xf{};
        xf.position = {x, r, z};
        world.AddComponent(e, xf);
        RenderableComponent rc;
        rc.mesh             = mesh;
        rc.materialInstance = inst;
        rc.castsShadow      = true;
        world.AddComponent(e, rc);
        return e;
    }

} // namespace

int main(int argc, char** argv)
{
    std::string capturePath;
    bool        forceSingleCascade = false;
    float       pcssLightSize      = 0.0f;
    bool        tintEnabled        = false;
    bool        motionEnabled      = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--capture" && i + 1 < argc)
        {
            capturePath = argv[i + 1];
            ++i;
        }
        else if (a == "--no-csm")
        {
            forceSingleCascade = true;
        }
        else if (a == "--tint")
        {
            tintEnabled = true;
        }
        else if (a == "--motion")
        {
            motionEnabled = true;
        }
        else if (a == "--pcss" && i + 1 < argc)
        {
            pcssLightSize = static_cast<float>(std::atof(argv[i + 1]));
            ++i;
        }
    }

    AppConfig cfg{};
    {
        std::string title = "OrangeEngine - 18 csm_large_scene (";
        title += forceSingleCascade ? "C1 fallback cascadeCount=1" : "CSM cascadeCount=3";
        if (tintEnabled)
        {
            title += " | tint";
        }
        if (motionEnabled)
        {
            title += " | motion";
        }
        title += ")";
        cfg.window.title = title;
    }
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

    // 球 caster 半径 1.0（远小于 cascade 0 extent，便于看锐边）；ground 半边
    // 50 单位（100×100，远 > ±10 box 让 C1 fallback 漏阴影戏剧化）。
    auto sphereRes = assets.Insert<MeshAsset>("builtin/sphere", MakeSphereMesh(1.0f, 32, 16));
    auto floorRes  = assets.Insert<MeshAsset>("builtin/floor", MakeFloorMesh(50.0f));
    if (sphereRes.IsErr() || floorRes.IsErr())
    {
        std::fprintf(stderr, "Insert<MeshAsset> failed\n");
        return 1;
    }
    AssetHandle<MeshAsset> sphere = sphereRes.Value();
    AssetHandle<MeshAsset> floor  = floorRes.Value();

    MaterialSystem materials(assets);
    if (auto rb = materials.RegisterBuiltins(); rb.IsErr())
    {
        std::fprintf(stderr, "RegisterBuiltins failed\n");
        return 1;
    }

    World                                          world;
    std::vector<std::unique_ptr<MaterialInstance>> instances;

    auto makePbr = [&](glm::vec4 baseColor, float metallic, float roughness)
        -> MaterialInstance*
    {
        auto inst = materials.CreateInstance("pbr");
        if (!inst)
        {
            return nullptr;
        }
        inst->SetUniform("uBaseColor", baseColor);
        inst->SetUniform("uMRA", glm::vec4(metallic, roughness, 1.0f, 0.0f));
        instances.push_back(std::move(inst));
        return instances.back().get();
    };

    // 中性灰地面 + 略偏暖的球（让阴影对比清晰）
    MaterialInstance* floorMat  = makePbr(glm::vec4(0.78f, 0.78f, 0.80f, 1.0f), 0.0f, 0.85f);
    MaterialInstance* sphereMat = makePbr(glm::vec4(0.85f, 0.78f, 0.65f, 1.0f), 0.05f, 0.55f);
    if (!floorMat || !sphereMat)
    {
        std::fprintf(stderr, "CreateInstance(\"pbr\") failed\n");
        return 1;
    }

    // 地面 entity（接收阴影；自身不投影避免无谓自遮挡）
    {
        Entity e = world.CreateEntity();
        world.AddComponent(e, TransformComponent{});
        RenderableComponent rc;
        rc.mesh             = floor;
        rc.materialInstance = floorMat;
        rc.castsShadow      = false;
        world.AddComponent(e, rc);
    }

    // 5 个 sphere caster 沿 +Z 摆，距离覆盖 cascade 0/1/2：
    //   z=3   → 应落 cascade 0（近，锐）
    //   z=10  → 应落 cascade 0/1 边界
    //   z=22  → 应落 cascade 1
    //   z=40  → 应落 cascade 1/2 边界
    //   z=70  → 应落 cascade 2（远，C1 fallback 直接漏）
    SpawnSphere(world, sphere, sphereMat, -1.5f, 3.0f, 1.0f);
    SpawnSphere(world, sphere, sphereMat, 1.5f, 10.0f, 1.0f);
    SpawnSphere(world, sphere, sphereMat, -1.5f, 22.0f, 1.0f);
    SpawnSphere(world, sphere, sphereMat, 1.5f, 40.0f, 1.0f);
    SpawnSphere(world, sphere, sphereMat, -1.5f, 70.0f, 1.0f);

    // 单一 directional light：太阳类，从右上前斜下，绕 +Y 倾斜 30° 让阴影沿 -X
    // 拖出长条（不沿轴向，便于看 cascade 分辨率差异）
    {
        Entity             e = world.CreateEntity();
        TransformComponent xf{};
        xf.rotation = MakeDirectionalLightRotationFromDir(glm::vec3(0.55f, -1.0f, -0.25f));
        world.AddComponent(e, xf);
        DirectionalLight dl{};
        dl.color       = glm::vec3(1.0f, 0.96f, 0.88f);
        dl.intensity   = 2.5f;
        dl.castsShadow = true;
        world.AddComponent(e, dl);
    }

    // Camera：低角度看向 +Z，eye=(0, 3.5, -6) 让 ground 拉成远方延伸；fov=60°
    // / near=0.5 / far=120 给 CSM 一个真实大 frustum 做 split。--motion 启用
    // 时 RenderLayer 每帧改 view 矩阵（sway 模式）。
    Entity cameraEntity;
    {
        cameraEntity       = world.CreateEntity();
        const float aspect = static_cast<float>(cfg.window.width) / static_cast<float>(cfg.window.height);
        Camera      cam    = Camera::Perspective(glm::radians(60.0f), aspect, 0.5f, 120.0f);
        cam.view           = glm::lookAt(glm::vec3(0.0f, 3.5f, -6.0f),
                                         glm::vec3(0.0f, 0.5f, 30.0f),
                                         glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent(cameraEntity, cam);
    }

    Pipeline pipeline;
    if (auto r = pipeline.Initialize(host->GetWindow(), assets); r.IsErr())
    {
        std::fprintf(stderr, "Pipeline::Initialize failed (code=%u)\n",
                     static_cast<unsigned>(r.Error()));
        return 1;
    }
    PostProcessChain chain = CreateDefault(); // bloom + tonemap 收尾，无 SSAO/SSR/DoF/TAA
    pipeline.SetPostProcessChain(&chain);
    pipeline.SetMaterialSystem(&materials);

    // ShadowConfig：核心配置点，CSM showcase 在这里决定 cascadeCount。
    //   * 默认 (`cascadeCount=3`，由 ShadowConfig 默认值给) = 真 CSM
    //   * `--no-csm` → cascadeCount=1 = C1 fallback 单 cascade ±10 ortho box
    // shadow map 分辨率 2048 让 cascade 0 接近 4K shadow 等效（每 cascade
    // 独占 2048×2048）。
    {
        ShadowConfig sc{};
        sc.mapResolution    = 2048;
        sc.pcfKernelRadius  = 1; // 3×3 PCF 轻软边，仍能看清 cascade 分辨率差异
        sc.depthBias        = 0.0008f;
        sc.pcssLightSize    = pcssLightSize;
        sc.cascadeCount     = forceSingleCascade ? 1u : 3u;
        sc.debugCascadeTint = tintEnabled;
        pipeline.SetShadowConfig(sc);
    }
    // 一点环境补光防阴影区全黑
    pipeline.SetDummyIblAmbient(0.08f, 0.085f, 0.10f);

    host->PushLayer(std::make_unique<RenderLayer>(pipeline, world, host->GetWindow(),
                                                  capturePath, motionEnabled, cameraEntity));

    const int rc = host->Run();
    pipeline.Shutdown();
    return rc;
}
