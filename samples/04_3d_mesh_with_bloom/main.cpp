// samples/04_3d_mesh_with_bloom —— HDR + Bloom 收尾里程碑。
//
// 与 04_3d_mesh 同骨架（旋转 textured 立方体 + 透视相机），多一条
// PostProcessChain：`BuiltinPostProcessChain::CreateDefault()` 串
// HDR → Bloom → Tonemap → LUT(no-op) 4 个 pass。Pipeline 在双段 frame
// 流程里：
//   * Stage A 离屏画 cube 到 RGBA16F HDR target；
//   * Stage A 末尾追加 6-down + 5-up 的 bloom mip-chain（mip[0] 走
//     Karis 平均 + bright-pass，按 BloomPass.threshold 卡白点）；
//   * Stage B 由 TonemapPass 路径完成 HDR + bloom → ACES Narkowicz →
//     BGRA8Unorm 写到 swap-chain。LutPass 当前 lut handle 无效 → no-op。
//
// 调参选择：
//   * checker 颜色保持原 textured shader 的 [0.1, 1.15] 区间；
//   * BloomPass.threshold 调到 0.5——让亮 cell 触发 bloom；
//   * Tonemap.exposure = 1.0；
//   * BloomPass.intensity 默认 0.5。

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
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/PostProcessChain.h>
#include <orange/engine/render/PostProcessPasses.h>
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
using Orange::Engine::Render::BloomPass;
using Orange::Engine::Render::BuiltinPostProcessChain::CreateDefault;
using Orange::Engine::Render::Camera;
using Orange::Engine::Render::MaterialInstance;
using Orange::Engine::Render::MaterialSystem;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::PostProcessChain;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Scene::TransformComponent;

namespace
{

// 立方体 6 个面 × 每面 4 顶点 + UV [0,1]²。从外侧看 outward CCW（BL → BR
// → TR → TL）；与 04_3d_mesh 同布局。
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

class RenderLayer : public Layer
{
public:
    RenderLayer(Pipeline& pipeline, World& world, Entity cube)
        : Layer("RenderLayer"), mPipeline(pipeline), mWorld(world), mCube(cube)
    {
    }

    void OnUpdate(const FrameContext& frame) override
    {
        if (auto* xf = mWorld.GetComponent<TransformComponent>(mCube))
        {
            const float     angle = frame.time.totalSeconds * 0.7f;
            const glm::vec3 axis  = glm::normalize(glm::vec3(0.4f, 1.0f, 0.2f));
            xf->rotation = glm::angleAxis(angle, axis);
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

}  // namespace

int main()
{
    AppConfig cfg{};
    cfg.window.title  = "OrangeEngine - 04 3d_mesh_with_bloom";
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
    auto cubeInstance = materials.CreateInstance("textured");
    if (!cubeInstance)
    {
        std::fprintf(stderr, "MaterialSystem::CreateInstance(\"textured\") returned null\n");
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

    Entity camEntity = world.CreateEntity();
    {
        const float aspect = static_cast<float>(cfg.window.width)
                           / static_cast<float>(cfg.window.height);
        Camera cam = Camera::Perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
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

    // 默认 4-pass 链：HDR + Bloom + Tonemap + LUT(no-op)。
    // textured shader 输出 [0.1, 1.15] 区间，把 BloomPass.threshold 调
    // 到 0.5 让亮 cell 显著触发 bright-pass、bloom 在屏幕上肉眼可见。
    PostProcessChain chain = CreateDefault();
    if (auto* bp = dynamic_cast<BloomPass*>(chain.FindByName("bloom")))
    {
        bp->threshold = 0.5f;
        bp->intensity = 0.6f;
    }
    pipeline.SetPostProcessChain(&chain);
    pipeline.SetMaterialSystem(&materials);

    host->PushLayer(std::make_unique<RenderLayer>(pipeline, world, cubeEntity));

    const int rc = host->Run();
    pipeline.Shutdown();   // 必须早于 host 析构（Window 还活着时释放渲染资源）
    return rc;
}
