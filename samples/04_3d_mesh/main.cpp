// samples/04_3d_mesh —— 3D 视觉验证。
//
// 把 perspective camera + 立方体 mesh + 每帧 TransformComponent 旋转
// 串到位，确认引擎已具备 "3D 实体在 3D 投影下旋转上屏" 的全链路能力。
//
// 相对 03_textured_quad 的差量只在 sample 层：
//   * 程序式构造 24 顶点立方体（每面独立 4 顶点 + UV [0,1]²，textured_mesh
//     fragment shader 复用即可，每面看着是干净的 8x8 棋盘）；
//   * Camera 用透视投影，view 用 lookAt 看向原点；
//   * RenderLayer 在每帧 OnUpdate 里改 cube 的 TransformComponent::rotation
//     —— RenderScene::Collect 已经按 T*R*S 合成 worldMatrix，所以旋转
//     自动随 quat 走。
//
// 没 depth buffer：OrangeRender 的 BeginFrame/SubmitItem/EndFrame 路径
// 当前只挂 swapchain color attachment，VkRenderingInfo 没 depth。所以
// 立方体的 "看着 3D" 只靠背面剔除——任意旋转下相机看到的只有 1–3 个外
// 向面，互不重叠。深度路径等 OrangeRender 暴露 depth 接口或
// 自定义 RenderTarget 路径打通后再补，本 sample 不在引擎层重铺这条线。

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
using Orange::Engine::Render::MaterialInstance;
using Orange::Engine::Render::MaterialSystem;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Scene::TransformComponent;

namespace
{

// 每个面 4 顶点，按 "从外侧看 outward CCW" 的顺序 BL → BR → TR → TL
// 列出。配合下方 (0,2,1,0,3,2) 的索引模板，正好得到 "world-CW per
// triangle"——经 Camera 的 Y-flip projection 翻转一次后在 framebuffer
// 空间中是 CCW，与 Pipeline 的 FrontFace::CCW + CullMode::Back 对齐。
struct CubeFace
{
    std::array<VertexPosition3, 4> positions;
};

constexpr std::array<CubeFace, 6> kCubeFaces = {{
    // +X 面（右）：从 +X 外侧看，BL 在 (-Z) 侧低位
    {{{
        { 0.5f, -0.5f,  0.5f},
        { 0.5f, -0.5f, -0.5f},
        { 0.5f,  0.5f, -0.5f},
        { 0.5f,  0.5f,  0.5f},
    }}},
    // -X 面（左）
    {{{
        {-0.5f, -0.5f, -0.5f},
        {-0.5f, -0.5f,  0.5f},
        {-0.5f,  0.5f,  0.5f},
        {-0.5f,  0.5f, -0.5f},
    }}},
    // +Y 面（顶）
    {{{
        {-0.5f,  0.5f,  0.5f},
        { 0.5f,  0.5f,  0.5f},
        { 0.5f,  0.5f, -0.5f},
        {-0.5f,  0.5f, -0.5f},
    }}},
    // -Y 面（底）
    {{{
        {-0.5f, -0.5f, -0.5f},
        { 0.5f, -0.5f, -0.5f},
        { 0.5f, -0.5f,  0.5f},
        {-0.5f, -0.5f,  0.5f},
    }}},
    // +Z 面（前，相机方向）
    {{{
        {-0.5f, -0.5f,  0.5f},
        { 0.5f, -0.5f,  0.5f},
        { 0.5f,  0.5f,  0.5f},
        {-0.5f,  0.5f,  0.5f},
    }}},
    // -Z 面（后）
    {{{
        { 0.5f, -0.5f, -0.5f},
        {-0.5f, -0.5f, -0.5f},
        {-0.5f,  0.5f, -0.5f},
        { 0.5f,  0.5f, -0.5f},
    }}},
}};

constexpr std::array<VertexUV2, 4> kFaceUVs = {{
    {0.0f, 0.0f},  // BL
    {1.0f, 0.0f},  // BR
    {1.0f, 1.0f},  // TR
    {0.0f, 1.0f},  // TL
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
        // (0, 2, 1, 0, 3, 2) per face：world-CW per triangle，经 Y-flip
        // projection 后变 NDC-CCW = Vulkan 默认 front-facing。
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        indices.push_back(base + 0);
        indices.push_back(base + 3);
        indices.push_back(base + 2);
    }

    return std::make_unique<MeshAsset>(std::move(positions), std::move(uvs), std::move(indices));
}

class RenderLayer : public Layer
{
public:
    RenderLayer(Pipeline& pipeline, World& world, Entity cube)
        : Layer("RenderLayer"), mPipeline(pipeline), mWorld(world), mCube(cube)
    {
    }

    void OnUpdate(const FrameContext& frame) override
    {
        // 绕一条非主轴匀速旋转——让三个面交替露出来，比单纯绕 Y 轴
        // 更能直观验证 "3D 实体在转"。
        if (auto* xf = mWorld.GetComponent<TransformComponent>(mCube))
        {
            const float          angle = frame.time.totalSeconds * 0.7f;
            const glm::vec3      axis  = glm::normalize(glm::vec3(0.4f, 1.0f, 0.2f));
            xf->rotation = glm::angleAxis(angle, axis);
        }
        mPipeline.Render(mWorld);
    }

    // 接住 framebuffer-resize：让 Pipeline 喂给 OrangeRender 触发
    // swap-chain 重建，避免拖拽窗口后下一帧死锁。返回 false 让事件继
    // 续向后传给其它 layer / overlay。
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

}  // namespace

int main()
{
    AppConfig cfg{};
    cfg.window.title  = "OrangeEngine - 04 3d_mesh";
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

    // MaterialSystem 注册内置模板（textured + toon + rim_light），拿一
    // 个 textured instance 喂给 RenderableComponent.materialInstance。
    // Pipeline 当前阶段仍走 hardcoded textured pipeline，instance 只起
    // schema 贯通作用，下一子任务起切到 per-template Pipeline 缓存。
    MaterialSystem materials(assets);
    if (auto rb = materials.RegisterBuiltins(); rb.IsErr())
    {
        std::fprintf(stderr,
                     "MaterialSystem::RegisterBuiltins failed (code=%u)\n",
                     static_cast<unsigned>(rb.Error()));
        return 1;
    }
    auto cubeInstance = materials.CreateInstance("textured");
    if (!cubeInstance)
    {
        std::fprintf(stderr, "MaterialSystem::CreateInstance(\"textured\") returned null\n");
        return 1;
    }

    World world;

    Entity cubeEntity = world.CreateEntity();
    world.AddComponent(cubeEntity, TransformComponent{});  // 默认放原点；rotation 在 OnUpdate 里更新
    {
        RenderableComponent r;
        r.mesh             = meshHandle;
        r.materialInstance = cubeInstance.get();
        world.AddComponent(cubeEntity, r);
    }

    Entity camEntity = world.CreateEntity();
    {
        const float aspect = static_cast<float>(cfg.window.width)
                           / static_cast<float>(cfg.window.height);
        Camera cam = Camera::Perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        // 看向原点，eye 偏离三个轴让正方体一开始就同时露出 +X / +Y / +Z 三面
        cam.view = glm::lookAt(glm::vec3(2.5f, 1.7f, 3.0f),
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

    host->PushLayer(std::make_unique<RenderLayer>(pipeline, world, cubeEntity));

    const int rc = host->Run();
    pipeline.Shutdown();   // 必须早于 host 析构（Window 还活着时释放渲染资源）
    return rc;
}
