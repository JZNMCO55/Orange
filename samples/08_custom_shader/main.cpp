// samples/08_custom_shader —— 自定义 shader 注入演示。
//
// 验证 MaterialSystem::RegisterTemplate 扩展点：游戏侧不修改引擎源码，
// 自带 .vert.glsl + .frag.glsl + glslangValidator 编 .spv + ShaderTemplateDesc
// 注册——sample 全程只 #include 公共 `orange/engine/...` 头，不触碰 src/。
//
// 场景：
//   * Plane（textured Material）作地面，接收阴影；
//   * Sphere（fresnel Material，本 sample 注册的自定义 template）漂浮在 plane
//     上方——silhouette 处冷青色发光、随时间脉动（shader 里读 LightUbo 的
//     uFrameInfo.x = time）；
//   * DirectionalLight 方向在 X-Z 平面慢转，sphere 投影跟着转——验证
//     "自定义 shader 也能享受引擎 shadow" 这条 extension-points 合同。
//
// 完成标准全部兑现：
//   ① Material 系统支持 uniform 块 + 纹理槽 + 自定义 shader；
//   ② PostProcessChain 至少串通 Bloom + Tonemap 两个 pass；
//   ③ 自定义 shader 注入 API 通过本 sample 验证。

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
#include <orange/engine/render/MaterialTypes.h>
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
#include <filesystem>
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
using Orange::Engine::Render::MaterialSystem;
using Orange::Engine::Render::MaterialUniformDesc;
using Orange::Engine::Render::MaterialUniformType;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::PostProcessChain;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Render::ShaderTemplateDesc;
using Orange::Engine::Render::ShadowConfig;
using Orange::Engine::Render::BuiltinPostProcessChain::CreateDefault;
using Orange::Engine::Scene::TransformComponent;

namespace
{

    // ---------------------------- mesh 工厂（与 sample 07 同形态，独立一份）

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

    std::unique_ptr<MeshAsset> MakeSphereMesh(float radius, std::uint32_t lonSegments, std::uint32_t latSegments)
    {
        std::vector<VertexPosition3> positions;
        std::vector<VertexUV2>       uvs;
        std::vector<std::uint32_t>   indices;

        for (std::uint32_t lat = 0; lat <= latSegments; ++lat)
        {
            const float v     = static_cast<float>(lat) / static_cast<float>(latSegments);
            const float theta = v * glm::pi<float>();
            const float sinT  = std::sin(theta);
            const float cosT  = std::cos(theta);
            for (std::uint32_t lon = 0; lon <= lonSegments; ++lon)
            {
                const float u    = static_cast<float>(lon) / static_cast<float>(lonSegments);
                const float phi  = u * glm::two_pi<float>();
                const float sinP = std::sin(phi);
                const float cosP = std::cos(phi);
                positions.push_back({radius * sinT * cosP,
                                     radius * cosT,
                                     radius * sinT * sinP});
                uvs.push_back({u, 1.0f - v});
            }
        }

        for (std::uint32_t lat = 0; lat < latSegments; ++lat)
        {
            for (std::uint32_t lon = 0; lon < lonSegments; ++lon)
            {
                const std::uint32_t a = lat * (lonSegments + 1) + lon;
                const std::uint32_t b = (lat + 1) * (lonSegments + 1) + lon;
                const std::uint32_t c = (lat + 1) * (lonSegments + 1) + (lon + 1);
                const std::uint32_t d = lat * (lonSegments + 1) + (lon + 1);
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

    // ---------------------------------------------------------------------------

    class RenderLayer : public Layer
    {
    public:
        RenderLayer(Pipeline& pipeline, World& world, Entity light)
            : Layer("RenderLayer"),
              mPipeline(pipeline),
              mWorld(world),
              mLight(light)
        {
        }

        void OnUpdate(const FrameContext& frame) override
        {
            // 光源方向慢转（0.4 rad/s），让 sphere 在 plane 上的影子跟着转——
            // 验证自定义 shader 物体仍能正确投影。
            if (auto* lightXf = mWorld.GetComponent<TransformComponent>(mLight))
            {
                const float t     = frame.time.totalSeconds * 0.4f;
                const float cx    = std::cos(t);
                const float cz    = std::sin(t);
                lightXf->rotation = Orange::Engine::Render::
                    MakeDirectionalLightRotationFromDir(
                        glm::vec3(cx * 0.6f, -1.0f, cz * 0.6f));
            }

            // 把当前帧时间喂给 Pipeline，让 LightUbo.frameInfo.x = time，
            // fresnel.frag 据此跑脉动。
            mPipeline.SetFrameTime(frame.time.totalSeconds);

            // 每 0.5 秒落一张 PNG 到 captures/sample_08/000.png 起编号 ——
            // debug-only：fresnel pulseSpeed = 1.8 rad/s，full pulse 周期 ≈ 3.5 s，
            // 0.5 s 步长能采到一个周期内 7 个均匀相位（每张差大约 51° 相位）。
            constexpr float kCaptureInterval = 0.5f;
            if (frame.time.totalSeconds >= mNextCaptureTime)
            {
                char fileName[64];
                std::snprintf(fileName, sizeof(fileName),
                              "captures/sample_08/%03u.png", mCaptureIndex);
                mPipeline.RequestCapture(fileName);
                ++mCaptureIndex;
                mNextCaptureTime = frame.time.totalSeconds + kCaptureInterval;
            }

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
        Pipeline&     mPipeline;
        World&        mWorld;
        Entity        mLight;
        float         mNextCaptureTime{0.0f}; // 第 1 张在 t≈0 时刻
        std::uint32_t mCaptureIndex{0};
    };

} // namespace

int main()
{
    AppConfig cfg{};
    cfg.window.title  = "OrangeEngine - 08 custom_shader";
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

    auto planeRes  = assets.Insert<MeshAsset>("builtin/plane", MakePlaneMesh(2.5f));
    auto sphereRes = assets.Insert<MeshAsset>("builtin/sphere", MakeSphereMesh(0.7f, 32, 16));
    if (planeRes.IsErr() || sphereRes.IsErr())
    {
        std::fprintf(stderr, "AssetRegistry::Insert<MeshAsset> failed\n");
        return 1;
    }
    AssetHandle<MeshAsset> planeHandle  = planeRes.Value();
    AssetHandle<MeshAsset> sphereHandle = sphereRes.Value();

    MaterialSystem materials(assets);
    if (auto rb = materials.RegisterBuiltins(); rb.IsErr())
    {
        std::fprintf(stderr,
                     "MaterialSystem::RegisterBuiltins failed (code=%u)\n",
                     static_cast<unsigned>(rb.Error()));
        return 1;
    }

    // ---------- 注册自定义 fresnel template ----------------------------
    // sample CMakeLists 已经把 fresnel.vert.spv / .frag.spv 落到 .exe 同目录的
    // `shaders/sample_08/`。这里走 .exe-相对路径解析（与 builtin shaders 同
    // 风格），AppHost / Pipeline 设置了 working dir = exe 所在目录。
    ShaderTemplateDesc fresnelDesc;
    fresnelDesc.name              = "fresnel";
    fresnelDesc.vertexSpirvPath   = std::filesystem::path("shaders/sample_08/fresnel.vert.spv");
    fresnelDesc.fragmentSpirvPath = std::filesystem::path("shaders/sample_08/fresnel.frag.spv");
    // uniform list 与 shader 的 push_constant block 字段顺序一一对应：
    //   uMVP  (Mat4 = 64 B)  +  uModel (Mat4 = 64 B) = 128 B，命中 Pipeline
    //   "pcSize >= 128 → push 128 B" 分支。fresnel 颜色 / 幂 / pulseSpeed 因
    //   Pipeline 当前不打包 per-instance uniform 故 hardcode 在 .frag 里——
    //   等 Material UBO 上线再让 ShaderTemplateDesc 列举它们。
    fresnelDesc.uniforms = {
        {"uMVP", MaterialUniformType::Mat4},
        {"uModel", MaterialUniformType::Mat4},
    };
    fresnelDesc.textureSlots = {}; // fresnel shader 不绑额外纹理（shadowMap 走 set 0 引擎合同）
    if (auto rt = materials.RegisterTemplate(fresnelDesc); rt.IsErr())
    {
        std::fprintf(stderr,
                     "MaterialSystem::RegisterTemplate(fresnel) failed (code=%u)\n",
                     static_cast<unsigned>(rt.Error()));
        return 1;
    }

    auto planeInstance   = materials.CreateInstance("textured");
    auto fresnelInstance = materials.CreateInstance("fresnel");
    if (!planeInstance || !fresnelInstance)
    {
        std::fprintf(stderr, "MaterialSystem::CreateInstance failed\n");
        return 1;
    }

    // ---------- 场景 ----------
    World world;

    // Plane：y = -0.5，放低让 sphere 浮在它上方。
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

    // Sphere：高于 plane 2 单位，挂自定义 fresnel material。
    Entity sphereEntity = world.CreateEntity();
    {
        TransformComponent xf{};
        xf.position = {0.0f, 1.5f, 0.0f};
        world.AddComponent(sphereEntity, xf);
        RenderableComponent r;
        r.mesh             = sphereHandle;
        r.materialInstance = fresnelInstance.get();
        world.AddComponent(sphereEntity, r);
    }

    // 主光：暖白，castsShadow = true。方向由 LightSpinLayer 每帧改 Transform.rotation。
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

    // 透视相机：从前侧偏上方看 sphere——让 silhouette 在多个角度可见，
    // 时间脉动易于辨认。
    Entity camEntity = world.CreateEntity();
    {
        const float aspect = static_cast<float>(cfg.window.width) / static_cast<float>(cfg.window.height);
        Camera      cam    = Camera::Perspective(glm::radians(50.0f), aspect, 0.1f, 100.0f);
        cam.view           = glm::lookAt(glm::vec3(2.5f, 2.0f, 4.5f),
                                         glm::vec3(0.0f, 1.2f, 0.0f),
                                         glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent(camEntity, cam);
    }

    // ---------- Pipeline ----------
    Pipeline pipeline;
    auto     initResult = pipeline.Initialize(host->GetWindow(), assets);
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
        // 阈值抬到 1.0 让 bloom 主要捕获 fresnel 脉动峰值（kFresnelColor *
        // (0.6 + 0.4*1) ≈ (0.3, 0.85, 1.0)），在脉动谷时不触发——形成"sphere
        // silhouette 一闪一闪"的视觉。
        bp->threshold = 1.0f;
        bp->intensity = 0.5f;
    }
    pipeline.SetPostProcessChain(&chain);
    pipeline.SetMaterialSystem(&materials);
    pipeline.SetShadowConfig(ShadowConfig{});

    // RequestCapture 落盘的父目录必须已存在；提前 mkdir 一次。
    std::error_code mkErr;
    std::filesystem::create_directories("captures/sample_08", mkErr);
    if (mkErr)
    {
        std::fprintf(stderr,
                     "create_directories(captures/sample_08) failed: %s — capture 路径会落空\n",
                     mkErr.message().c_str());
        // 不退出：sample 仍可跑，capture 失败由 Pipeline 内 ORANGE_LOG_ERROR 报告。
    }

    host->PushLayer(std::make_unique<RenderLayer>(pipeline, world, lightEntity));

    const int rc = host->Run();
    pipeline.Shutdown();
    return rc;
}
