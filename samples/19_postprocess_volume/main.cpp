// samples/19_postprocess_volume —— PostProcess V2 Local Volume 视觉证据 fixture
// （GAP-2026-05-27-postprocess-component-local-volume 落地节 "留待后续" #3）。
//
// V2 把 PostProcessComponent 从 v1 "first-found 全局单例" 升级为 "collect-all
// + 相机位置混合 + Local 盒"。本 sample 是 V2 的 **戏剧化** 视觉证据：用
// 一个 Global 中性底 + 两个语义对立的 Local 盒（高对比 grading + 冷色温
// grading），相机沿 -Z→+Z 推进穿过两个盒；用户在 capture 模式可定位 4 个
// 关键相机位置出对照图，在 --motion 模式肉眼看 blendDistance smoothstep
// 平滑过渡。
//
// 场景几何（故意简洁，让 post 效果而非几何复杂度主导画面）：
//   * 100×100 中性灰 ground（接收效果，不投影避免自遮挡）
//   * 5 个 sphere 沿 +Z (z = -3, 3, 8, 13, 18) 排成"路径标记"，让相机推
//     进时有视觉参照物显示 post 字段（grade contrast / 色温 / vignette）变化
//   * 1 个 directional light（暖白，斜上）
//
// 3 个 PostProcessComponent（V2 collect-all 同时生效）：
//   * Global 底（mode=Global, priority=0）：中性 grading，仅 SSAO 轻开
//   * Local 盒 A（mode=Local, position=(0,1,3), extent=(3,2.5,3), priority=1,
//     blendDistance=1.5）：高对比 grading（contrast=2.0 / saturation=1.8） +
//     强 vignette，戏剧化光影
//   * Local 盒 B（mode=Local, position=(0,1,13), extent=(3,2.5,3), priority=1,
//     blendDistance=1.5）：冷色温（temperature=-0.7 / tint=+0.2）+ 中等
//     vignette，营造夜冷感
//
// 视觉对照（命令行 flag）：
//   --position {outside|box-a|transition|box-b}  : 预设相机 z 位置
//     - outside     : z=-8（在两盒之外，看 Global 底）
//     - box-a       : z=3 （盒 A 中心，看高对比 grading）
//     - transition  : z=6.5（box-a 边界外 0.5m，blendDistance=1.5 → smoothstep
//                    weight ≈ 0.74，看 box-a grading 部分淡入 = 真过渡带证据）
//     - box-b       : z=13（盒 B 中心，看冷色温 grading）
//   --motion           : 相机 6 秒内沿 z 从 -8 推到 +18，肉眼看混合
//   --capture <path>   : 渲一帧 PNG 后自动退（CI / 文档无人值守出图）
//
// 故意不挂的：spot / point light（避免干扰 grading 对照）、TAA / DoF / motion
// blur（不影响本 sample 的 V2 局部混合主题）；bloom + tonemap 通过默认
// chain 保留（HDR → LDR 收尾，与 sample 18 同款）。
//
// 与 sample 18 的对照：sample 18 是 CSM 视觉证据（shadow 分段），用 --no-csm
// 切回 fallback 出 before/after；sample 19 是 PostProcess V2 视觉证据（局部
// 混合），用 --position 切相机位置出"盒内 / 盒外 / 过渡带" 3+1 张对照图。
// 两个 sample 是 post 架构闭环的两个端点 fixture。

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
#include <orange/engine/render/PostProcessComponent.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
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
using Orange::Engine::Render::PostProcessComponent;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Render::BuiltinPostProcessChain::CreateDefault;
using Orange::Engine::Scene::TransformComponent;

namespace
{

    // UV-sphere（与 sample 16 / 18 同构造）
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

    // 大 XZ 平面 ground（halfExtent=50 → 100×100 单位）
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

    // 相机位置预设：4 个关键 z 位置，对应 GAP 留待后续 "盒内 / 盒外 / 过渡带"
    // 3+1 张对照图。--motion 不启用时按 mPositionZ 单一位置出图（capture 友好）。
    enum class Position
    {
        Outside    = 0, // z=-8 ，两盒外，看 Global 中性底
        BoxA       = 1, // z=3  ，盒 A 中心，看高对比 grading
        Transition = 2, // z=6.5，box-a 边界外 blendDistance smoothstep ~74% 区
        BoxB       = 3, // z=13 ，盒 B 中心，看冷色温 grading
    };

    float PositionToZ(Position p)
    {
        switch (p)
        {
            case Position::Outside:
                return -8.0f;
            case Position::BoxA:
                return 3.0f;
            // box-a 外边界 z=6（box center z=3 + extent.z=3），距离 0.5 → smoothstep
            // (0, 1.5, 0.5) ≈ 0.26，weight = 1 - 0.26 = 0.74 → 看 box-a grading 74%
            // 强度部分淡入（vs outside=纯 Global / box-a=100% grading 中间态）。
            case Position::Transition:
                return 6.5f;
            case Position::BoxB:
                return 13.0f;
        }
        return -8.0f;
    }

    const char* PositionName(Position p)
    {
        switch (p)
        {
            case Position::Outside:
                return "outside";
            case Position::BoxA:
                return "box-a";
            case Position::Transition:
                return "transition";
            case Position::BoxB:
                return "box-b";
        }
        return "outside";
    }

    class RenderLayer : public Layer
    {
    public:
        RenderLayer(Pipeline& pipeline, World& world, Platform::Window& window,
                    std::string capturePath, bool motionEnabled, float positionZ,
                    Entity cameraEntity)
            : Layer("RenderLayer"), mPipeline(pipeline), mWorld(world), mWindow(window),
              mCapturePath(std::move(capturePath)),
              mMotionEnabled(motionEnabled),
              mPositionZ(positionZ),
              mCameraEntity(cameraEntity) {}

        void OnUpdate(const FrameContext& /*frame*/) override
        {
            // 相机推进：--motion 时 6 秒内沿 z 从 -8 推到 +18（双盒外→盒 A→过渡
            // →盒 B→盒外）；非 motion 时停在 mPositionZ。
            float camZ = mPositionZ;
            if (mMotionEnabled)
            {
                const float t = std::clamp(static_cast<float>(mFrame) / 360.0f, 0.0f, 1.0f);
                camZ          = glm::mix(-8.0f, 18.0f, t);
            }
            auto& cam = mWorld.Registry().get<Camera>(World::ToEntt(mCameraEntity));
            cam.view  = glm::lookAt(glm::vec3(0.0f, 2.0f, camZ),
                                    glm::vec3(0.0f, 1.0f, camZ + 6.0f),
                                    glm::vec3(0.0f, 1.0f, 0.0f));

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
        static constexpr std::uint64_t kCaptureFrame = 8; // 无 TAA 预热几帧即可
        Pipeline&                      mPipeline;
        World&                         mWorld;
        Platform::Window&              mWindow;
        std::string                    mCapturePath;
        bool                           mMotionEnabled{false};
        float                          mPositionZ{-8.0f};
        Entity                         mCameraEntity{};
        std::uint64_t                  mFrame{0};
    };

    // 在 (x, z) 放一个落地球 caster（半径 r，球心 y=r），用作相机推进时的视觉
    // 标尺 + 让 SSAO / 阴影有"被遮挡几何"可作用。
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
    bool        motionEnabled = false;
    Position    position      = Position::Outside;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--capture" && i + 1 < argc)
        {
            capturePath = argv[i + 1];
            ++i;
        }
        else if (a == "--motion")
        {
            motionEnabled = true;
        }
        else if (a == "--position" && i + 1 < argc)
        {
            const std::string v = argv[i + 1];
            ++i;
            if (v == "outside")
            {
                position = Position::Outside;
            }
            else if (v == "box-a")
            {
                position = Position::BoxA;
            }
            else if (v == "transition")
            {
                position = Position::Transition;
            }
            else if (v == "box-b")
            {
                position = Position::BoxB;
            }
            else
            {
                std::fprintf(stderr,
                             "Unknown --position value '%s' (want outside|box-a|transition|box-b)\n",
                             v.c_str());
                return 1;
            }
        }
    }

    AppConfig cfg{};
    {
        std::string title = "OrangeEngine - 19 postprocess_volume (";
        title += motionEnabled ? "motion" : PositionName(position);
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

    auto sphereRes = assets.Insert<MeshAsset>("builtin/sphere", MakeSphereMesh(0.6f, 32, 16));
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

    // 中性灰地面（让 grading 偏移在屏幕上最显眼）+ 略偏暖、稍粗糙的球
    MaterialInstance* floorMat  = makePbr(glm::vec4(0.78f, 0.78f, 0.80f, 1.0f), 0.0f, 0.85f);
    MaterialInstance* sphereMat = makePbr(glm::vec4(0.82f, 0.78f, 0.70f, 1.0f), 0.05f, 0.55f);
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

    // 5 个 sphere 沿 +Z 摆作路径标记 / 视觉参照物。半径 0.6 + 球心 y=r。
    SpawnSphere(world, sphere, sphereMat, -1.5f, -3.0f, 0.6f);
    SpawnSphere(world, sphere, sphereMat, 1.5f, 3.0f, 0.6f);
    SpawnSphere(world, sphere, sphereMat, -1.5f, 8.0f, 0.6f);
    SpawnSphere(world, sphere, sphereMat, 1.5f, 13.0f, 0.6f);
    SpawnSphere(world, sphere, sphereMat, -1.5f, 18.0f, 0.6f);

    // 单一 directional light：偏暖太阳光，从右上斜下
    {
        Entity             e = world.CreateEntity();
        TransformComponent xf{};
        xf.rotation = MakeDirectionalLightRotationFromDir(glm::vec3(0.45f, -1.0f, -0.2f));
        world.AddComponent(e, xf);
        DirectionalLight dl{};
        dl.color       = glm::vec3(1.0f, 0.96f, 0.88f);
        dl.intensity   = 2.5f;
        dl.castsShadow = true;
        world.AddComponent(e, dl);
    }

    // —— PostProcess V2 3 个 volume：1 Global 底 + 2 Local 盒 ——
    //
    // V2 collect-all 算法（Pipeline::SyncPostProcessFromWorld 实现）：相机
    // world position 落在每个 Local 盒内 → weight=1，盒外 blendDistance 内
    // → smoothstep 淡入；按 priority 升序对标量字段 lerp，bool/离散字段按
    // weight>0 中最高 priority 接管。Global 当 priority=0 的底。
    //
    // ① Global 中性底：仅 SSAO 轻开 + 默认 grading（exposure=0/contrast=1/
    //    saturation=1/temperature=0），让盒外是"未加滤镜"的 PBR 基线，
    //    盒内的 grading 偏移最戏剧化。
    {
        Entity             e = world.CreateEntity();
        TransformComponent xf{};
        xf.position = {0.0f, 0.0f, 0.0f};
        world.AddComponent(e, xf);
        PostProcessComponent pp{};
        pp.mode     = PostProcessComponent::Mode::Global;
        pp.priority = 0.0f;
        // Global 底刻意全留默认值：SSAO on / grading off / vignette off
        world.AddComponent(e, pp);
    }

    // ② Local 盒 A（高对比 grading）：相机 z ≈ 3 时画面 contrast↑/sat↑ + 强
    //    vignette → "戏剧化光影盒"。
    {
        Entity             e = world.CreateEntity();
        TransformComponent xf{};
        xf.position = {0.0f, 1.0f, 3.0f};
        world.AddComponent(e, xf);
        PostProcessComponent pp{};
        pp.mode                   = PostProcessComponent::Mode::Local;
        pp.localExtent            = {3.0f, 2.5f, 3.0f};
        pp.priority               = 1.0f;
        pp.blendDistance          = 1.5f;
        pp.gradeEnabled           = true;
        pp.gradeContrast          = 2.0f;
        pp.gradeSaturation        = 1.8f;
        pp.gradeExposure          = 0.2f;
        pp.lensEnabled            = true;
        pp.lensVignetteIntensity  = 0.6f;
        pp.lensVignetteSmoothness = 0.4f;
        world.AddComponent(e, pp);
    }

    // ③ Local 盒 B（冷色温 + 中等 vignette）：相机 z ≈ 13 时画面 temperature↓
    //    （偏蓝）+ tint↑（偏品红）→ "夜冷感盒"。
    {
        Entity             e = world.CreateEntity();
        TransformComponent xf{};
        xf.position = {0.0f, 1.0f, 13.0f};
        world.AddComponent(e, xf);
        PostProcessComponent pp{};
        pp.mode                   = PostProcessComponent::Mode::Local;
        pp.localExtent            = {3.0f, 2.5f, 3.0f};
        pp.priority               = 1.0f;
        pp.blendDistance          = 1.5f;
        pp.gradeEnabled           = true;
        pp.gradeTemperature       = -0.7f;
        pp.gradeTint              = 0.2f;
        pp.gradeSaturation        = 0.9f; // 略降饱和加冷感
        pp.lensEnabled            = true;
        pp.lensVignetteIntensity  = 0.35f;
        pp.lensVignetteSmoothness = 0.5f;
        world.AddComponent(e, pp);
    }

    // Camera Perspective 60° / near=0.5 / far=80。view 由 RenderLayer 每帧
    // 按 motion / position 重算（确保单一相机数据源 = layer 设置的位置）。
    Entity cameraEntity;
    {
        cameraEntity       = world.CreateEntity();
        const float aspect = static_cast<float>(cfg.window.width) / static_cast<float>(cfg.window.height);
        Camera      cam    = Camera::Perspective(glm::radians(60.0f), aspect, 0.5f, 80.0f);
        cam.view           = glm::lookAt(glm::vec3(0.0f, 2.0f, PositionToZ(position)),
                                         glm::vec3(0.0f, 1.0f, PositionToZ(position) + 6.0f),
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
    // 默认 chain（bloom + tonemap）保 HDR→LDR 收尾；SSAO/SSR/grade/vignette
    // 等 always-on internal pass 由 PostProcessComponent collect-mix 后驱动。
    PostProcessChain chain = CreateDefault();
    pipeline.SetPostProcessChain(&chain);
    pipeline.SetMaterialSystem(&materials);

    // 一点环境补光防阴影区全黑
    pipeline.SetDummyIblAmbient(0.08f, 0.085f, 0.10f);

    host->PushLayer(std::make_unique<RenderLayer>(
        pipeline, world, host->GetWindow(), capturePath, motionEnabled,
        PositionToZ(position), cameraEntity));

    const int rc = host->Run();
    pipeline.Shutdown();
    return rc;
}
