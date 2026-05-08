// samples/07_full_pipeline —— Phase 3 视觉基线综合演示（Phase 3 真正
// 收尾里程碑）。
//
// 场景：
//   * 一块大 plane（textured Material）作地面；
//   * 一个 cube（toon Material）漂浮在 plane 上方左侧、高于地面 2 单位；
//   * 一个 sphere（rim_light Material）漂浮在 plane 上方右侧、高于地面 2 单位；
//   * 一个小 light marker 球（rim_light Material，castsShadow = false）
//     固定在 -lightDir * 4 处，作为"灯泡"可视化光源方向；
//   * 一个 DirectionalLight，方向随时间在 X-Z 平面上旋转——marker 跟着
//     转、cube/sphere 投在 plane 上的阴影也跟着转，验证 shadow pass 的
//     "光源转动阴影跟随"承诺。
//
// 双段 frame 流程：
//   * Stage A.0 —— shadow pass：Pipeline 把 3 个 mesh 从 light 视角渲到
//     shadow map；
//   * Stage A.1 —— 主 pass：Pipeline 按 per-template `RHIPipeline` 缓存
//     绘制每个 entity，descriptor set 0 = {shadow sampler, light UBO}；
//   * Stage A.2 —— bloom mip-chain（chain 含 BloomPass）；
//   * Stage B —— tonemap：HDR + bloom → ACES Narkowicz → swap-chain。

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
#include <glm/gtc/quaternion.hpp>

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

namespace
{

// ------------------------------------------------------------- mesh 工厂

// Plane：XZ 平面上的大矩形（10×10 单位），UV 0..1。两个三角形足够。
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
    // 索引按 "world-CW per triangle"（与 Camera Y-flip projection + Vulkan
    // CCW front-face 约定对齐——见 04_3d_mesh sample 注释）。
    std::vector<std::uint32_t> indices = {0, 2, 1, 0, 3, 2};
    return std::make_unique<MeshAsset>(std::move(positions),
                                       std::move(uvs),
                                       std::move(indices));
}

// Cube：每面独立 4 顶点（与 04_3d_mesh sample 同布局），UV 0..1。
struct CubeFace { std::array<VertexPosition3, 4> positions; };

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
    return std::make_unique<MeshAsset>(std::move(positions),
                                       std::move(uvs),
                                       std::move(indices));
}

// UV sphere：经度 lonSegments × 纬度 latSegments + 两极。坐标按
// "world-CW per triangle" 排序，与 plane / cube 同 winding 约定。
std::unique_ptr<MeshAsset> MakeSphereMesh(float radius, std::uint32_t lonSegments, std::uint32_t latSegments)
{
    std::vector<VertexPosition3> positions;
    std::vector<VertexUV2>       uvs;
    std::vector<std::uint32_t>   indices;

    // 顶点：从北极 (lat=0) 走到南极 (lat=latSegments)；每条 latitude 上
    // 等分 lonSegments+1 个点（最后一个与第一个共用经度，UV 不同）。
    for (std::uint32_t lat = 0; lat <= latSegments; ++lat)
    {
        const float v     = static_cast<float>(lat) / static_cast<float>(latSegments);
        const float theta = v * glm::pi<float>();             // 0..π，从北到南
        const float sinT  = std::sin(theta);
        const float cosT  = std::cos(theta);
        for (std::uint32_t lon = 0; lon <= lonSegments; ++lon)
        {
            const float u    = static_cast<float>(lon) / static_cast<float>(lonSegments);
            const float phi  = u * glm::two_pi<float>();      // 0..2π
            const float sinP = std::sin(phi);
            const float cosP = std::cos(phi);
            positions.push_back({radius * sinT * cosP,
                                 radius * cosT,
                                 radius * sinT * sinP});
            uvs.push_back({u, 1.0f - v});
        }
    }

    // 索引：每个 lat × lon quad 拆 2 三角形，winding world-CW。
    for (std::uint32_t lat = 0; lat < latSegments; ++lat)
    {
        for (std::uint32_t lon = 0; lon < lonSegments; ++lon)
        {
            const std::uint32_t a = lat       * (lonSegments + 1) + lon;
            const std::uint32_t b = (lat + 1) * (lonSegments + 1) + lon;
            const std::uint32_t c = (lat + 1) * (lonSegments + 1) + (lon + 1);
            const std::uint32_t d = lat       * (lonSegments + 1) + (lon + 1);
            // 两个三角形：(a, c, b) + (a, d, c) —— 与 cube 的 (0,2,1) / (0,3,2)
            // 模板同 winding。
            indices.push_back(a);
            indices.push_back(c);
            indices.push_back(b);
            indices.push_back(a);
            indices.push_back(d);
            indices.push_back(c);
        }
    }

    return std::make_unique<MeshAsset>(std::move(positions),
                                       std::move(uvs),
                                       std::move(indices));
}

// ------------------------------------------------------------- main loop

class RenderLayer : public Layer
{
public:
    RenderLayer(Pipeline& pipeline, World& world, Entity light, Entity marker)
        : Layer("RenderLayer"),
          mPipeline(pipeline),
          mWorld(world),
          mLight(light),
          mMarker(marker)
    {
    }

    void OnUpdate(const FrameContext& frame) override
    {
        // 让 light 方向在 X-Z 平面上转，验证 shadow 跟随。
        const float t  = frame.time.totalSeconds * 0.5f;
        const float cx = std::cos(t);
        const float cz = std::sin(t);
        // y 分量保持负（光从上往下打）。
        const glm::vec3 dir = glm::normalize(glm::vec3(cx * 0.6f, -1.0f, cz * 0.6f));

        if (auto* dl = mWorld.GetComponent<DirectionalLight>(mLight))
        {
            dl->direction = dir;
        }
        // marker 放在 -dir * D 处，相当于光源"位置"。D=4 让 marker 在
        // 摄像机视野内绕一圈都不出框。
        if (auto* xf = mWorld.GetComponent<TransformComponent>(mMarker))
        {
            xf->position = -dir * 4.0f;
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
    Pipeline& mPipeline;
    World&    mWorld;
    Entity    mLight;
    Entity    mMarker;
};

}  // namespace

int main()
{
    AppConfig cfg{};
    cfg.window.title  = "OrangeEngine - 07 full_pipeline";
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

    // Plane halfSize 取 2.5 → 5×5 单位地面，足够给 cube + sphere 投影；
    // 太大反而让 camera 拉远后 cube/sphere 占屏比例小。
    auto planeRes = assets.Insert<MeshAsset>("builtin/plane",  MakePlaneMesh(2.5f));
    auto cubeRes  = assets.Insert<MeshAsset>("builtin/cube",   MakeCubeMesh());
    auto sphereRes = assets.Insert<MeshAsset>("builtin/sphere", MakeSphereMesh(0.7f, 32, 16));
    if (planeRes.IsErr() || cubeRes.IsErr() || sphereRes.IsErr())
    {
        std::fprintf(stderr, "AssetRegistry::Insert<MeshAsset> failed\n");
        return 1;
    }
    AssetHandle<MeshAsset> planeHandle  = planeRes.Value();
    AssetHandle<MeshAsset> cubeHandle   = cubeRes.Value();
    AssetHandle<MeshAsset> sphereHandle = sphereRes.Value();

    MaterialSystem materials(assets);
    if (auto rb = materials.RegisterBuiltins(); rb.IsErr())
    {
        std::fprintf(stderr,
                     "MaterialSystem::RegisterBuiltins failed (code=%u)\n",
                     static_cast<unsigned>(rb.Error()));
        return 1;
    }
    auto planeInstance  = materials.CreateInstance("textured");
    auto cubeInstance   = materials.CreateInstance("toon");
    auto sphereInstance = materials.CreateInstance("rim_light");
    auto markerInstance = materials.CreateInstance("rim_light");
    if (!planeInstance || !cubeInstance || !sphereInstance || !markerInstance)
    {
        std::fprintf(stderr, "MaterialSystem::CreateInstance failed\n");
        return 1;
    }

    World world;

    // Plane：放在 y=0，作为 "地面"。
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

    // Cube：左侧上方，悬浮在 plane 上方 2 单位高度。
    Entity cubeEntity = world.CreateEntity();
    {
        TransformComponent xf{};
        xf.position = {-1.2f, 1.5f, 0.0f};
        world.AddComponent(cubeEntity, xf);
        RenderableComponent r;
        r.mesh             = cubeHandle;
        r.materialInstance = cubeInstance.get();
        world.AddComponent(cubeEntity, r);
    }

    // Sphere：右侧上方，与 cube 等高。
    Entity sphereEntity = world.CreateEntity();
    {
        TransformComponent xf{};
        xf.position = {1.2f, 1.5f, 0.0f};
        world.AddComponent(sphereEntity, xf);
        RenderableComponent r;
        r.mesh             = sphereHandle;
        r.materialInstance = sphereInstance.get();
        world.AddComponent(sphereEntity, r);
    }

    // Light marker：固定在 -dir * 4 处由 RenderLayer 每帧更新，scale 0.18
    // 让世界半径 ≈ 0.13；rim_light 材质让它像个发光球。castsShadow = false
    // 避免在地面上留 phantom 阴影。
    Entity markerEntity = world.CreateEntity();
    {
        TransformComponent xf{};
        xf.position = {0.0f, 4.0f, 0.0f};        // RenderLayer 会立刻覆盖
        xf.scale    = {0.18f, 0.18f, 0.18f};
        world.AddComponent(markerEntity, xf);
        RenderableComponent r;
        r.mesh             = sphereHandle;
        r.materialInstance = markerInstance.get();
        r.castsShadow      = false;
        world.AddComponent(markerEntity, r);
    }

    // 主光：方向斜下，主色白光，castsShadow = true。
    Entity lightEntity = world.CreateEntity();
    {
        DirectionalLight dl{};
        dl.direction   = glm::normalize(glm::vec3(0.6f, -1.0f, 0.4f));
        dl.color       = glm::vec3(1.0f, 0.95f, 0.85f);
        dl.intensity   = 1.2f;
        dl.castsShadow = true;
        world.AddComponent(lightEntity, dl);
    }

    // 透视相机：从前上方看向场景中心。
    Entity camEntity = world.CreateEntity();
    {
        const float aspect = static_cast<float>(cfg.window.width)
                           / static_cast<float>(cfg.window.height);
        Camera cam = Camera::Perspective(glm::radians(50.0f), aspect, 0.1f, 100.0f);
        // Camera 站在 plane 边沿之外（plane halfSize=2.5，camera Z=4.5），
        // 略仰俯看向场景中心 (0, 0.3, 0)——让 cube + sphere 占据画面中
        // 偏上 1/3，plane + 阴影 占下 2/3。
        // cube/sphere 抬到 y=1.5 后，眼点同步抬高、lookAt 抬到中段；
        // marker 在 y≈3.4 处转一圈 也都在视野内。
        cam.view = glm::lookAt(glm::vec3(4.0f, 3.0f, 5.5f),
                               glm::vec3(0.0f, 1.0f, 0.0f),
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

    // 默认 4-pass chain：HDR + Bloom + Tonemap + LUT(no-op)。
    PostProcessChain chain = CreateDefault();
    if (auto* bp = dynamic_cast<BloomPass*>(chain.FindByName("bloom")))
    {
        // 阈值 0.7 让 plane 上"warm cell + 强光照"才触发 bloom；intensity
        // 0.4 弱化 bloom 让画面不糊掉。
        bp->threshold = 0.7f;
        bp->intensity = 0.4f;
    }
    pipeline.SetPostProcessChain(&chain);
    pipeline.SetMaterialSystem(&materials);

    // ShadowConfig 用默认值（1024 / 3×3 PCF / depthBias 0.005 / normalBias 0.01）。
    pipeline.SetShadowConfig(ShadowConfig{});

    host->PushLayer(std::make_unique<RenderLayer>(pipeline, world, lightEntity, markerEntity));

    const int rc = host->Run();
    pipeline.Shutdown();
    return rc;
}
