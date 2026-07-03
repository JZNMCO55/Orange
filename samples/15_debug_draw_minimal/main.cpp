// samples/15_debug_draw_minimal —— v0.9 DebugDrawScene 公共面端到端验证。
//
// 场景：一个旋转的 cube + DirectionalLight + perspective camera。
// 每帧通过 `pipeline.GetDebugDrawScene()` 提交：
//   * 紧贴 cube 外的 wireframe AABB（绿色，1.05 倍放大避 Z-fight 视觉）
//   * 围在 cube 周围的 wireframe sphere（白色，半径 1.0）
//   * 3 条坐标轴 line（X 红 / Y 绿 / Z 蓝，从原点 → 单位长）
//   * 1 个填充三角形（橙色，标记 +Y 上方 1.5 单位处）
//
// 视觉预期：
//   * cube 旋转中，AABB 紧贴外侧、sphere 球面环住 cube（debug 几何不
//     被 cube 遮挡，always-on-top；与 Lumix / Godot debug viewport 同
//     款节奏）
//   * 3 条坐标轴从原点向各正方向延伸，颜色明确（验证 ABGR 编码低
//     8 位 R / 高 8 位 A 的 endian）
//
// 用法：直接运行 build/bin/Debug/15_debug_draw_minimal.exe；ESC 退出。

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
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/DebugDrawScene.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
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
using Orange::Engine::Render::Camera;
using Orange::Engine::Render::DebugDrawScene;
using Orange::Engine::Render::DirectionalLight;
using Orange::Engine::Render::MaterialInstance;
using Orange::Engine::Render::MaterialSystem;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Scene::TransformComponent;

namespace
{

    // ABGR packed：低 8 位 R，高 8 位 A（0xAA_BB_GG_RR）。
    constexpr std::uint32_t kRed    = 0xFF0000FFu; // R=FF
    constexpr std::uint32_t kGreen  = 0xFF00FF00u; // G=FF
    constexpr std::uint32_t kBlue   = 0xFFFF0000u; // B=FF
    constexpr std::uint32_t kWhite  = 0xFFFFFFFFu;
    constexpr std::uint32_t kOrange = 0xFF1A78FFu; // R=FF G=78 B=1A — 暖橙

    // 6 面 24 顶点立方体（与 sample 04 同款，简化版只保留单一材质 UV 全 1）。
    std::unique_ptr<MeshAsset> MakeUnitCubeMesh()
    {
        constexpr float                               h     = 0.5f;
        std::array<std::array<VertexPosition3, 4>, 6> faces = {{
            {{{h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}}},     // +X
            {{{-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}}}, // -X
            {{{-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}}},     // +Y
            {{{-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}}}, // -Y
            {{{-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}}},     // +Z
            {{{h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}}}, // -Z
        }};

        std::vector<VertexPosition3> positions;
        std::vector<VertexUV2>       uvs;
        std::vector<std::uint32_t>   indices;
        positions.reserve(24);
        uvs.reserve(24);
        indices.reserve(36);

        for (const auto& face : faces)
        {
            const std::uint32_t base = static_cast<std::uint32_t>(positions.size());
            for (int i = 0; i < 4; ++i)
            {
                positions.push_back(face[i]);
                uvs.push_back({0.0f, 0.0f});
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

    class RenderLayer : public Layer
    {
    public:
        RenderLayer(Pipeline& pipeline, World& world, Entity cube)
            : Layer("RenderLayer"), mPipeline(pipeline), mWorld(world), mCube(cube) {}

        void OnUpdate(const FrameContext& frame) override
        {
            // cube 绕非主轴匀速旋转 —— sample 04 同款节奏。
            if (auto* xf = mWorld.GetComponent<TransformComponent>(mCube))
            {
                const float     angle = frame.time.totalSeconds * 0.6f;
                const glm::vec3 axis  = glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f));
                xf->rotation          = glm::angleAxis(angle, axis);
            }

            // 提交本帧 debug 几何。
            if (auto* dbg = mPipeline.GetDebugDrawScene())
            {
                // 1. AABB（紧贴 cube 外，1.05 倍放大）
                const glm::vec3 aabbHalf{0.525f};
                dbg->AddAabb(-aabbHalf, aabbHalf, kGreen);

                // 2. wireframe sphere（半径 1.0，包住 cube + AABB）
                dbg->AddSphere(glm::vec3(0.0f), 1.0f, kWhite, 16);

                // 3. 坐标轴 3 条线（从原点 → 单位向量）
                dbg->AddLine(glm::vec3(0.0f), glm::vec3(1.5f, 0.0f, 0.0f), kRed);
                dbg->AddLine(glm::vec3(0.0f), glm::vec3(0.0f, 1.5f, 0.0f), kGreen);
                dbg->AddLine(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.5f), kBlue);

                // 4. 一个填充三角形（cube 正上方 1.5 单位的橙色标记）
                dbg->AddTriangle(glm::vec3(-0.2f, 1.5f, 0.0f),
                                 glm::vec3(0.2f, 1.5f, 0.0f),
                                 glm::vec3(0.0f, 1.8f, 0.0f),
                                 kOrange);
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
        Entity    mCube;
    };

} // namespace

int main()
{
    AppConfig cfg{};
    cfg.window.title  = "OrangeEngine - 15 debug_draw_minimal";
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
        std::fprintf(stderr, "RegisterLoader<ShaderAsset> failed (code=%u)\n",
                     static_cast<unsigned>(reg.Error()));
        return 1;
    }

    auto meshHandleResult = assets.Insert<MeshAsset>("builtin/cube", MakeUnitCubeMesh());
    if (meshHandleResult.IsErr())
    {
        std::fprintf(stderr, "Insert<MeshAsset> failed (code=%u)\n",
                     static_cast<unsigned>(meshHandleResult.Error()));
        return 1;
    }
    AssetHandle<MeshAsset> meshHandle = meshHandleResult.Value();

    MaterialSystem materials(assets);
    if (auto rb = materials.RegisterBuiltins(); rb.IsErr())
    {
        std::fprintf(stderr, "RegisterBuiltins failed (code=%u)\n",
                     static_cast<unsigned>(rb.Error()));
        return 1;
    }
    auto cubeInstance = materials.CreateInstance("textured");
    if (!cubeInstance)
    {
        std::fprintf(stderr, "CreateInstance(\"textured\") returned null\n");
        return 1;
    }

    World world;

    Entity cubeEntity = world.CreateEntity();
    world.AddComponent(cubeEntity, TransformComponent{});
    {
        RenderableComponent r;
        r.mesh             = meshHandle;
        r.materialInstance = cubeInstance.get();
        world.AddComponent(cubeEntity, r);
    }

    Entity lightEntity = world.CreateEntity();
    {
        TransformComponent xf;
        xf.rotation = glm::quatLookAt(
            glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f)),
            glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent(lightEntity, xf);

        DirectionalLight light;
        light.color     = glm::vec3(1.0f, 0.95f, 0.85f);
        light.intensity = 1.0f;
        world.AddComponent(lightEntity, light);
    }

    Entity camEntity = world.CreateEntity();
    {
        const float aspect = static_cast<float>(cfg.window.width) / static_cast<float>(cfg.window.height);
        Camera      cam    = Camera::Perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        cam.view           = glm::lookAt(glm::vec3(2.8f, 2.0f, 3.0f),
                                         glm::vec3(0.0f, 0.0f, 0.0f),
                                         glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent(camEntity, cam);
    }

    Pipeline pipeline;
    auto     initResult = pipeline.Initialize(host->GetWindow(), assets);
    if (initResult.IsErr())
    {
        std::fprintf(stderr, "Pipeline::Initialize failed (code=%u)\n",
                     static_cast<unsigned>(initResult.Error()));
        return 1;
    }
    pipeline.SetMaterialSystem(&materials);

    host->PushLayer(std::make_unique<RenderLayer>(pipeline, world, cubeEntity));

    const int rc = host->Run();
    pipeline.Shutdown();
    return rc;
}
