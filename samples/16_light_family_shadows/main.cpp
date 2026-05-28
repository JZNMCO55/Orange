// samples/16_light_family_shadows —— 光源族 + 三类阴影同框演示（GAP-2026-05-26）
//
// 一块大地面 + 3 个球形 occluder，三种光源全部 castsShadow，让三类阴影在
// 同一帧内同时可见、空间上分区便于辨识：
//
//   * DirectionalLight（全局主光，暖白，从右上前斜下）：3 个球各投一道
//     **方向一致的平行硬阴影**（2D ortho shadow map）。
//   * SpotLight（左区，冷蓝，正上方朝下 -Y）：在左球处打出**锥形光斑**，
//     球在锥内投**透视阴影**（perspective shadow map，G2）。
//   * PointLight（右区，暖橙，贴近右球）：右球投**全向 radial 阴影**
//     （omnidirectional cubemap shadow，G3）。
//
// 视觉预期：
//   * 左球被冷蓝锥光罩住 + 一道朝外发散的锥形阴影；
//   * 右球周围一圈暖橙照明 + 随距离衰减，球背面侧地面有 radial 阴影；
//   * 三球都拖一道方向一致的平行阴影（directional）。
//
// 镜头：前上方俯视，地面 + 三球 + 三类阴影全部入画。后处理链复用
// BuiltinPostProcessChain::CreateDefault（bloom + tonemap），与编辑器观感一致；
// 在其上叠 SSAO/SSR/接触阴影/DoF/TAA/色彩分级 + 锐化（CAS）+ 镜头（色散+暗角），
// 各带 `--no-<x>` flag 做 before/after 对比（相机运动模糊需相机运动，静态 sample 不挂）。
//
// 注：directional shadow 的 ortho frustum 当前按 ±10 场景 bbox 估算
//（Pipeline::Impl::ComputeLightViewProj），故地面 / 球阵控制在该范围内。

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
using Orange::Engine::Render::BuiltinPostProcessChain::CreateDefault;
using Orange::Engine::Render::Camera;
using Orange::Engine::Render::DirectionalLight;
using Orange::Engine::Render::MakeDirectionalLightRotationFromDir;
using Orange::Engine::Render::MaterialInstance;
using Orange::Engine::Render::MaterialSystem;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::PointLight;
using Orange::Engine::Render::PostProcessChain;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Render::ShadowConfig;
using Orange::Engine::Render::SpotLight;
using Orange::Engine::Scene::TransformComponent;

namespace
{

// UV-sphere（lat/lon），与 13_pbr_direct 同构造。ComputeSmoothNormals 得平滑法线。
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
            indices.push_back(a); indices.push_back(c); indices.push_back(b);
            indices.push_back(a); indices.push_back(d); indices.push_back(c);
        }
    }
    auto pMesh = std::make_unique<MeshAsset>(std::move(positions), std::move(uvs),
                                             std::move(indices));
    pMesh->ComputeSmoothNormalsFromTriangles();
    return pMesh;
}

// XZ 平面地面 quad（法线 +Y 朝上）。winding 取从上方看 CCW，对齐 Pipeline
// FrontFace::CCW + CullMode::Back，使顶面为正面（俯视相机可见）。
std::unique_ptr<MeshAsset> MakeFloorMesh(float halfExtent)
{
    const float h = halfExtent;
    std::vector<VertexPosition3> positions = {
        {-h, 0.0f,  h}, { h, 0.0f,  h}, { h, 0.0f, -h}, {-h, 0.0f, -h},
    };
    std::vector<VertexUV2> uvs = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
    };
    std::vector<std::uint32_t> indices = {0, 1, 2, 0, 2, 3};
    auto pMesh = std::make_unique<MeshAsset>(std::move(positions), std::move(uvs),
                                             std::move(indices));
    pMesh->ComputeSmoothNormalsFromTriangles();  // winding → +Y 法线
    return pMesh;
}

class RenderLayer : public Layer
{
public:
    // capturePath 非空时进入"截图模式"：预热几帧让 shadow 资源 / pipeline
    // 就绪后 RequestCapture 一帧 PNG，再 RequestClose 自动退出 —— 供 CI /
    // 文档无人值守出图（RequestCapture 仅 window 模式生效）。空 → 正常交互。
    RenderLayer(Pipeline& pipeline, World& world, Platform::Window& window,
                std::string capturePath)
        : Layer("RenderLayer"), mPipeline(pipeline), mWorld(world), mWindow(window),
          mCapturePath(std::move(capturePath)) {}

    void OnUpdate(const FrameContext& /*frame*/) override
    {
        if (!mCapturePath.empty() && mFrame == kCaptureFrame)
        {
            mPipeline.RequestCapture(std::filesystem::path(mCapturePath));
        }
        mPipeline.Render(mWorld);
        if (!mCapturePath.empty() && mFrame >= kCaptureFrame + 1)
        {
            mWindow.RequestClose();  // 截图已写出，退出
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
    static constexpr std::uint64_t kCaptureFrame = 32;  // 预热 32 帧再截（够 TAA 时序收敛）
    Pipeline&         mPipeline;
    World&            mWorld;
    Platform::Window& mWindow;
    std::string       mCapturePath;
    std::uint64_t     mFrame{0};
};

// 在 (x, z) 放一个落在地面上的球 occluder（半径 r，球心 y=r），返回该 entity。
Entity SpawnSphere(World& world, AssetHandle<MeshAsset> mesh,
                   MaterialInstance* inst, float x, float z, float r)
{
    Entity e = world.CreateEntity();
    TransformComponent xf{};
    xf.position = {x, r, z};
    world.AddComponent(e, xf);
    RenderableComponent rc;
    rc.mesh             = mesh;
    rc.materialInstance = inst;
    rc.castsShadow      = true;  // 三类光源的 shadow pass 都收它作 caster
    world.AddComponent(e, rc);
    return e;
}

}  // namespace

int main(int argc, char** argv)
{
    // `--capture <path>`：渲一帧 PNG 后自动退（CI / 文档无人值守出图）。
    // `--no-ssao`：关闭 SSAO（做 before/after 对比）。
    std::string capturePath;
    bool        disableSsao = false;
    bool        disableSsr  = false;
    bool        disablePcss = false;
    bool        disableGtao = false;
    bool        disableContact = false;
    bool        disableDof = false;
    bool        disableTaa = false;
    bool        disableGrade = false;
    bool        disableLens = false;
    bool        disableSharpen = false;
    // GAP-2026-05-11 G3 PointLight halo toggle —— --halo 让 PointLight 挂
    // emissive sphere（Pipeline 自动 record），可见光晕由 BloomPass 自然散
    // 光；不传 flag 时不画 halo（与 G3 落地前视觉一致）。
    bool        enableHalo = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--capture" && i + 1 < argc) { capturePath = argv[i + 1]; ++i; }
        else if (a == "--no-ssao")            { disableSsao = true; }
        else if (a == "--no-ssr")             { disableSsr = true; }
        else if (a == "--no-pcss")            { disablePcss = true; }
        else if (a == "--no-gtao")            { disableGtao = true; }
        else if (a == "--no-contact")         { disableContact = true; }
        else if (a == "--no-dof")             { disableDof = true; }
        else if (a == "--no-taa")             { disableTaa = true; }
        else if (a == "--no-grade")           { disableGrade = true; }
        else if (a == "--no-lens")            { disableLens = true; }
        else if (a == "--no-sharpen")         { disableSharpen = true; }
        else if (a == "--halo")               { enableHalo = true; }
    }

    AppConfig cfg{};
    cfg.window.title  = "OrangeEngine - 16 light_family_shadows";
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

    auto sphereRes = assets.Insert<MeshAsset>("builtin/sphere", MakeSphereMesh(0.8f, 32, 16));
    auto floorRes  = assets.Insert<MeshAsset>("builtin/floor",  MakeFloorMesh(8.0f));
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

    World world;
    std::vector<std::unique_ptr<MaterialInstance>> instances;

    auto makePbr = [&](glm::vec4 baseColor, float metallic, float roughness)
        -> MaterialInstance*
    {
        auto inst = materials.CreateInstance("pbr");
        if (!inst) { return nullptr; }
        inst->SetUniform("uBaseColor", baseColor);
        inst->SetUniform("uMRA", glm::vec4(metallic, roughness, 1.0f, 0.0f));
        instances.push_back(std::move(inst));
        return instances.back().get();
    };

    // 地面：中性偏白、非金属、中等粗糙——既能清晰接收三类阴影，又不抢戏。
    MaterialInstance* floorMat = makePbr(glm::vec4(0.82f, 0.82f, 0.85f, 1.0f), 0.0f, 0.7f);
    MaterialInstance* sphereMat = makePbr(glm::vec4(0.9f, 0.9f, 0.92f, 1.0f), 0.05f, 0.45f);
    if (!floorMat || !sphereMat)
    {
        std::fprintf(stderr, "CreateInstance(\"pbr\") failed\n");
        return 1;
    }

    // 地面 entity（接收阴影；自身不投影 → castsShadow=false 避免无谓自遮挡）。
    {
        Entity e = world.CreateEntity();
        world.AddComponent(e, TransformComponent{});  // 原点，XZ 平面 y=0
        RenderableComponent rc;
        rc.mesh             = floor;
        rc.materialInstance = floorMat;
        rc.castsShadow      = false;
        world.AddComponent(e, rc);
    }

    // 三球 occluder：左区（spot）/ 中（directional 主秀）/ 右区（point）。
    SpawnSphere(world, sphere, sphereMat, -3.5f, 0.0f, 0.8f);  // 左：spot 罩
    SpawnSphere(world, sphere, sphereMat,  0.0f, 0.0f, 0.8f);  // 中：directional
    SpawnSphere(world, sphere, sphereMat,  3.5f, 0.0f, 0.8f);  // 右：point 旁

    // ---- 三种光源（全部 castsShadow）-------------------------------------

    // DirectionalLight：全局暖白主光，右上前斜下 → 三球各拖一道平行硬阴影。
    {
        Entity e = world.CreateEntity();
        TransformComponent xf{};
        xf.rotation = MakeDirectionalLightRotationFromDir(glm::vec3(0.45f, -1.0f, -0.35f));
        world.AddComponent(e, xf);
        DirectionalLight dl{};
        dl.color       = glm::vec3(1.0f, 0.96f, 0.88f);
        dl.intensity   = 2.0f;
        dl.castsShadow = true;
        world.AddComponent(e, dl);
    }

    // SpotLight：左区正上方朝下 -Y，冷蓝锥光罩住左球 + 投透视阴影。
    {
        Entity e = world.CreateEntity();
        TransformComponent xf{};
        xf.position = {-3.5f, 5.0f, 0.0f};
        xf.rotation = MakeDirectionalLightRotationFromDir(glm::vec3(0.0f, -1.0f, 0.0f));
        world.AddComponent(e, xf);
        SpotLight sl{};
        sl.color          = glm::vec3(0.35f, 0.55f, 1.0f);
        sl.intensity      = 60.0f;
        sl.range          = 12.0f;
        sl.innerConeAngle = 0.28f;
        sl.outerConeAngle = 0.42f;
        sl.castsShadow    = true;
        world.AddComponent(e, sl);
    }

    // PointLight：右区贴近右球（右上前），暖橙 radial 照明 + 全向阴影。
    // --halo 时挂 emissive sphere 让光源本身可见（GAP-2026-05-11 G3）。
    {
        Entity e = world.CreateEntity();
        TransformComponent xf{};
        xf.position = {3.5f, 2.6f, 2.4f};
        world.AddComponent(e, xf);
        PointLight pl{};
        pl.color       = glm::vec3(1.0f, 0.6f, 0.25f);
        pl.intensity   = 40.0f;
        pl.range       = 9.0f;
        pl.castsShadow = true;
        if (enableHalo)
        {
            pl.haloEnabled   = true;
            pl.haloRadius    = 0.25f;
            pl.haloIntensity = 0.5f;   // light intensity=40 比较高，halo 乘子
                                       //  降到 0.5 让球体不刺眼但 bloom 散光
                                       //  依然显眼
        }
        world.AddComponent(e, pl);
    }

    // Camera：前上方俯视，地面 + 三球 + 三类阴影全部入画。
    {
        Entity e = world.CreateEntity();
        const float aspect = static_cast<float>(cfg.window.width)
                           / static_cast<float>(cfg.window.height);
        Camera cam = Camera::Perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        cam.view = glm::lookAt(glm::vec3(0.0f, 6.0f, 9.5f),
                               glm::vec3(0.0f, 0.4f, 0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent(e, cam);
    }

    Pipeline pipeline;
    if (auto r = pipeline.Initialize(host->GetWindow(), assets); r.IsErr())
    {
        std::fprintf(stderr, "Pipeline::Initialize failed (code=%u)\n",
                     static_cast<unsigned>(r.Error()));
        return 1;
    }
    PostProcessChain chain = CreateDefault();
    // SSAO：挂进 post chain；`--no-ssao` 关闭做 before/after 对比。
    // 球↔地面接触处 + 球体下半的凹处会变暗，接触感更强。默认走 GTAO
    //（horizon-based，更准更平滑）；`--no-gtao` 退回半球 kernel SSAO 做对比。
    {
        auto ssao = std::make_unique<Orange::Engine::Render::SsaoPass>();
        ssao->enabled  = !disableSsao;
        ssao->radius   = 0.6f;
        ssao->strength = 1.0f;
        ssao->power    = 2.0f;
        ssao->useGtao  = !disableGtao;
        chain.AddPass(std::move(ssao));
    }
    // SSR：`--no-ssr` 关闭。地面会反射出上方的球体（湿表面/光泽感）。
    {
        auto ssr = std::make_unique<Orange::Engine::Render::SsrPass>();
        ssr->enabled     = !disableSsr;
        ssr->maxDistance = 14.0f;
        ssr->maxSteps    = 40.0f;
        ssr->thickness   = 0.8f;
        ssr->strength    = 0.7f;
        chain.AddPass(std::move(ssr));
    }
    // 接触阴影：`--no-contact` 关闭做对比。补 shadow map 在球↔地面接触处因
    // depthBias 抬起留下的漏光缝隙，接触线更"咬合"。
    {
        auto cs = std::make_unique<Orange::Engine::Render::ContactShadowPass>();
        cs->enabled   = !disableContact;
        cs->length    = 0.15f;   // 接触尺度（短）：只补接触线缝隙，不当粗阴影
        cs->maxSteps  = 16.0f;
        cs->thickness = 0.3f;
        cs->bias      = 0.015f;
        cs->strength  = 0.9f;
        chain.AddPass(std::move(cs));
    }
    // 景深：对焦在三球（相机 (0,6,9.5) 看向原点，球深度 ~10）；远处地面 + 近处
    // 地面虚化。`--no-dof` 关闭做对比。
    {
        auto dof = std::make_unique<Orange::Engine::Render::DofPass>();
        dof->enabled       = !disableDof;
        dof->focusDistance = 10.0f;
        dof->focusRange    = 5.0f;
        dof->maxCoCRadius  = 0.015f;
        chain.AddPass(std::move(dof));
    }
    // TAA：每帧 jitter + 历史 resolve，抗锯齿 + 把 GTAO/接触阴影/景深的 jitter
    // 噪点去噪。静态相机下数帧即收敛干净。`--no-taa` 关闭做对比（边缘锯齿 +
    // 屏幕空间噪点显现）。
    {
        auto taa = std::make_unique<Orange::Engine::Render::TaaPass>();
        taa->enabled  = !disableTaa;
        taa->feedback = 0.9f;
        chain.AddPass(std::move(taa));
    }
    // 色彩分级：暖调 + 轻微提对比/增艳的电影感定调。`--no-grade` 关闭做对比。
    {
        auto grade = std::make_unique<Orange::Engine::Render::ColorGradePass>();
        grade->enabled     = !disableGrade;
        grade->exposure    = 0.15f;
        grade->contrast    = 1.1f;
        grade->saturation  = 1.15f;
        grade->temperature = 0.25f;   // 暖
        grade->tint        = 0.0f;
        chain.AddPass(std::move(grade));
    }
    // 锐化（CAS）：恢复 TAA resolve 软化的高频细节。`--no-sharpen` 关闭做对比
    //（关掉后配合 TAA 画面更软）。Pipeline 内部录制在 TAA 之后、motion blur 之前。
    {
        auto sharpen = std::make_unique<Orange::Engine::Render::SharpenPass>();
        sharpen->enabled   = !disableSharpen;
        sharpen->sharpness = 0.4f;
        chain.AddPass(std::move(sharpen));
    }
    // 镜头效果（色散 + 暗角）：最后的"镜头"阶段，边缘轻微色散 + 暗角聚焦中心，
    // 给三球场景加一点电影镜头质感。`--no-lens` 关闭做对比。
    {
        auto lens = std::make_unique<Orange::Engine::Render::LensPass>();
        lens->enabled             = !disableLens;
        lens->chromaticAberration = 0.003f;
        lens->vignetteIntensity   = 0.35f;
        lens->vignetteSmoothness  = 0.5f;
        chain.AddPass(std::move(lens));
    }
    // 注：相机运动模糊（MotionBlurPass）需相机运动才有效果；本 sample 相机静态，
    // 挂上会是 no-op，故不挂——见 samples 注释或 docs/rendering-post-process.md。
    pipeline.SetPostProcessChain(&chain);
    pipeline.SetMaterialSystem(&materials);
    // PCSS 软阴影：受影体离遮挡面越远半影越宽（球底接触处硬、远处软）。
    // 2048 分辨率让软边更细腻；`--no-pcss` 关闭做 before/after 对比（退回固定
    // 半径 PCF）。lightSize 单位 = shadow map texel。
    {
        ShadowConfig sc{};
        sc.mapResolution = 2048;
        sc.pcssLightSize = disablePcss ? 0.0f : 12.0f;
        pipeline.SetShadowConfig(sc);
    }
    // 一点环境补光，让被阴影遮住、又不在 spot/point 影响范围内的地面不至全黑。
    pipeline.SetDummyIblAmbient(0.06f, 0.07f, 0.09f);

    // 命令行 `--capture <path>` → 截图模式：渲一帧 PNG 后自动退（CI / 文档
    // 无人值守出图）。不带参数 = 正常交互运行。
    host->PushLayer(std::make_unique<RenderLayer>(pipeline, world, host->GetWindow(),
                                                  capturePath));

    const int rc = host->Run();
    pipeline.Shutdown();
    return rc;
}
