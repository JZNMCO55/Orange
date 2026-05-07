// samples/03_textured_quad —— Phase 2 视觉验证节点。
//
// 走通 ECS → Pipeline → OrangeRender 的完整数据流：
//   1. 程序式构造一个 quad MeshAsset（4 顶点、6 索引、pos+uv）；
//   2. 通过 AssetRegistry::Insert 注册到资源表，拿到 handle；
//   3. ECS 里建一个 entity 挂 TransformComponent + RenderableComponent
//      引用该 mesh handle；再建一个挂 Camera 的 entity 作为主相机；
//   4. AppHost 主循环里，自定义 Layer 在 OnUpdate 时调 Pipeline.Render
//      把这一帧画上屏。
//
// 当前 OrangeRender 的 RHI 还没把 sampler / descriptor-set 上线，
// 所以 fragment shader 从 uv 程序式合成 checker 当 "贴图" 用——视
// 觉效果是 8x8 棋盘 + 一点 uv 渐变。等 OrangeRender 暴露 sampler 路
// 径后再切到真实 TextureAsset 采样。

#include <orange/engine/app/AppConfig.h>
#include <orange/engine/app/AppHost.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/app/Layer.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/platform/WindowEvent.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <cstdio>
#include <memory>
#include <utility>

using namespace Orange::Engine;
using Orange::Engine::Asset::AssetHandle;
using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Asset::MeshAsset;
using Orange::Engine::Asset::VertexPosition3;
using Orange::Engine::Asset::VertexUV2;
using Orange::Engine::Render::Camera;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Scene::TransformComponent;

namespace
{

// 程序式构造一个 1x1 的正方形 mesh，覆盖 ortho [-1, 1]² 中的中央
// 区域。两个三角形 (0,1,2) + (0,2,3)；UV 与 quad 角点一一对应。
std::unique_ptr<MeshAsset> MakeQuadMesh()
{
    std::vector<VertexPosition3> positions = {
        {-0.5f, -0.5f, 0.0f},
        { 0.5f, -0.5f, 0.0f},
        { 0.5f,  0.5f, 0.0f},
        {-0.5f,  0.5f, 0.0f},
    };
    std::vector<VertexUV2> uvs = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f},
    };
    // 索引按 "world-CW = NDC-CCW after projection Y-flip" 约定编排，与
    // Pipeline 的 FrontFace::CCW + CullMode::Back 默认状态对齐。详见
    // src/render/Pipeline.cpp 中 rasterizer state 注释。
    std::vector<std::uint32_t> indices = {
        0, 2, 1,
        0, 3, 2,
    };
    return std::make_unique<MeshAsset>(std::move(positions), std::move(uvs), std::move(indices));
}

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

int main()
{
    AppConfig cfg{};
    cfg.window.title  = "OrangeEngine - 03 textured_quad";
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
    auto meshHandleResult = assets.Insert<MeshAsset>("builtin/quad", MakeQuadMesh());
    if (meshHandleResult.IsErr())
    {
        std::fprintf(stderr,
                     "AssetRegistry::Insert<MeshAsset> failed (code=%u)\n",
                     static_cast<unsigned>(meshHandleResult.Error()));
        return 1;
    }
    AssetHandle<MeshAsset> meshHandle = meshHandleResult.Value();

    World world;

    auto quadEntity = world.CreateEntity();
    world.AddComponent(quadEntity, TransformComponent{});  // 默认放原点
    {
        RenderableComponent r;
        r.mesh = meshHandle;
        world.AddComponent(quadEntity, r);
    }

    auto camEntity = world.CreateEntity();
    // ortho 单位立方：[-1,1]² × [0,1]——quad 顶点恰好充满 1x1 中心
    world.AddComponent(camEntity, Camera::Orthographic(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f));

    Pipeline pipeline;
    auto initResult = pipeline.Initialize(host->GetWindow(), assets);
    if (initResult.IsErr())
    {
        std::fprintf(stderr,
                     "Pipeline::Initialize failed (code=%u)\n",
                     static_cast<unsigned>(initResult.Error()));
        return 1;
    }

    host->PushLayer(std::make_unique<RenderLayer>(pipeline, world));

    const int rc = host->Run();
    pipeline.Shutdown();   // 必须早于 host 析构（Window 还活着时释放渲染资源）
    return rc;
}
