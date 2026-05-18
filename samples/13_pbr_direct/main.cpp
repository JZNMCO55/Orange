// samples/13_pbr_direct —— PBR direct lighting 3×3 球阵
//
// 3×3 球阵：
//   * 行（Y 轴自下而上） metallic ∈ {0.0, 0.5, 1.0}
//   * 列（X 轴自左向右） roughness ∈ {0.1, 0.5, 0.9}
//   * baseColor 全部 (1.0, 0.78, 0.34) 暖橙——让 metallic=1 行的金属高光带
//     可识别的色调（金属在 Schlick fresnel 下高光自带 baseColor 着色）
//
// 1 个 DirectionalLight 从前上方右侧斜下打入；IBL 三槽位在 Pipeline 启动时
// 绑 dummy 1×1 黑纹理，自然退化为 direct-only。本 sample 用于 validate
// Cook-Torrance + GGX NDF + Smith correlated G + Schlick fresnel + Lambert
// diffuse 数学正确性。
//
// 视觉预期（与 wiki concepts/rendering/microfacet-theory.md 参考图对照）：
//   * 列方向 roughness 0.1 → 0.9：高光从 "sharp 小点" 过渡到 "wide 模糊"
//   * 行方向 metallic 0 → 1：从 "塑料漫反射 + 白色高光" 过渡到 "金属几乎
//     无 diffuse + baseColor-tinted 高光"（kD = (1-F)(1-metallic) 能量项）
//
// 后处理链复用 BuiltinPostProcessChain::CreateDefault（bloom + tonemap），
// 与编辑器默认观感保持一致。

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

// 球体 mesh 工厂：lat/lon UV-sphere，复用 12_layer_partition_demo 同构造。
// 共享顶点路径下 ComputeSmoothNormalsFromTriangles 自然得到近似
// normalize(position) 的平滑法线（球面平凡）。
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
            const float sinP = std::sin(phi);
            const float cosP = std::cos(phi);
            positions.push_back({radius * sinT * cosP,
                                 radius * cosT,
                                 radius * sinT * sinP});
            uvs.push_back({u, 1.0f - v});
        }
    }
    for (std::uint32_t i = 0; i < lat; ++i)
    {
        for (std::uint32_t j = 0; j < lon; ++j)
        {
            const std::uint32_t a = i       * (lon + 1) + j;
            const std::uint32_t b = (i + 1) * (lon + 1) + j;
            const std::uint32_t c = (i + 1) * (lon + 1) + (j + 1);
            const std::uint32_t d = i       * (lon + 1) + (j + 1);
            indices.push_back(a); indices.push_back(c); indices.push_back(b);
            indices.push_back(a); indices.push_back(d); indices.push_back(c);
        }
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

constexpr std::array<float, 3> kMetallicSteps  = {0.0f, 0.5f, 1.0f};
constexpr std::array<float, 3> kRoughnessSteps = {0.1f, 0.5f, 0.9f};
// 暖橙：在 metallic=1 行金属高光自带 baseColor 着色，肉眼可识别"非白"高光。
constexpr glm::vec4            kBaseColor{1.0f, 0.78f, 0.34f, 1.0f};
constexpr float                kSphereSpacing  = 1.4f;
constexpr float                kSphereRadius   = 0.5f;

}  // namespace

int main(int /*argc*/, char** /*argv*/)
{
    AppConfig cfg{};
    cfg.window.title  = "OrangeEngine - 13 pbr_direct";
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

    auto sphereRes = assets.Insert<MeshAsset>("builtin/sphere",
                                              MakeSphereMesh(kSphereRadius, 32, 16));
    if (sphereRes.IsErr())
    {
        std::fprintf(stderr, "Insert<MeshAsset> failed\n");
        return 1;
    }
    AssetHandle<MeshAsset> sphereHandle = sphereRes.Value();

    MaterialSystem materials(assets);
    if (auto rb = materials.RegisterBuiltins(); rb.IsErr())
    {
        std::fprintf(stderr, "RegisterBuiltins failed\n");
        return 1;
    }

    World world;

    // 9 个 PBR MaterialInstance：每球独立一份，row × col 索引。
    // 位置阵列居中——球阵中心位于世界原点。
    std::vector<std::unique_ptr<MaterialInstance>> instances;
    instances.reserve(kMetallicSteps.size() * kRoughnessSteps.size());

    const float xOffset = -kSphereSpacing
                        * static_cast<float>(kRoughnessSteps.size() - 1) * 0.5f;
    const float yOffset = -kSphereSpacing
                        * static_cast<float>(kMetallicSteps.size() - 1) * 0.5f;

    for (std::size_t row = 0; row < kMetallicSteps.size(); ++row)
    {
        for (std::size_t col = 0; col < kRoughnessSteps.size(); ++col)
        {
            auto inst = materials.CreateInstance("pbr");
            if (!inst)
            {
                std::fprintf(stderr, "CreateInstance(\"pbr\") failed\n");
                return 1;
            }
            inst->SetUniform("uBaseColor", kBaseColor);
            // uMRA: .x = metallic / .y = roughness / .z = ao / .w 预留
            inst->SetUniform("uMRA",
                             glm::vec4(kMetallicSteps[row],
                                       kRoughnessSteps[col],
                                       1.0f,
                                       0.0f));
            instances.push_back(std::move(inst));

            Entity entity = world.CreateEntity();
            TransformComponent xf{};
            xf.position = {
                xOffset + static_cast<float>(col) * kSphereSpacing,
                yOffset + static_cast<float>(row) * kSphereSpacing,
                0.0f,
            };
            world.AddComponent(entity, xf);

            RenderableComponent r;
            r.mesh             = sphereHandle;
            r.materialInstance = instances.back().get();
            r.castsShadow      = false;  // 球阵无地面，shadow 路径不需要
            world.AddComponent(entity, r);
        }
    }

    // DirectionalLight：右上前斜下打——每球面上有清晰的 primary highlight；
    // roughness ↓ 高光聚拢，metallic ↑ diffuse 衰减、高光带 baseColor 着色。
    Entity lightEntity = world.CreateEntity();
    {
        TransformComponent lightXf{};
        lightXf.rotation = Orange::Engine::Render::MakeDirectionalLightRotationFromDir(
            glm::vec3(0.5f, -1.0f, -0.6f));
        world.AddComponent(lightEntity, lightXf);

        DirectionalLight dl{};
        dl.color       = glm::vec3(1.0f, 1.0f, 1.0f);
        dl.intensity   = 2.5f;
        dl.castsShadow = false;
        world.AddComponent(lightEntity, dl);
    }

    // Camera：正前方略上斜俯视球阵，让 3×3 全部入画。
    Entity camEntity = world.CreateEntity();
    {
        const float aspect = static_cast<float>(cfg.window.width)
                           / static_cast<float>(cfg.window.height);
        Camera cam = Camera::Perspective(glm::radians(40.0f), aspect, 0.1f, 100.0f);
        cam.view = glm::lookAt(glm::vec3(0.0f, 0.3f, 6.5f),
                               glm::vec3(0.0f, 0.0f, 0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent(camEntity, cam);
    }

    Pipeline pipeline;
    if (auto r = pipeline.Initialize(host->GetWindow(), assets); r.IsErr())
    {
        std::fprintf(stderr, "Pipeline::Initialize failed (code=%u)\n",
                     static_cast<unsigned>(r.Error()));
        return 1;
    }
    PostProcessChain chain = CreateDefault();
    pipeline.SetPostProcessChain(&chain);
    pipeline.SetMaterialSystem(&materials);
    pipeline.SetShadowConfig(ShadowConfig{});

    host->PushLayer(std::make_unique<RenderLayer>(pipeline, world));

    const int rc = host->Run();
    pipeline.Shutdown();
    return rc;
}
