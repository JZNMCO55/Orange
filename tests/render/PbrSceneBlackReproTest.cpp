// pbr_showcase 场景"全黑"回归的 headless 像素复现（GAP-2026-05-25 A2）。
//
// 用户报告:keystone（set 1 材质贴图渲染）上线后 PBR scene 所有球全黑。
// 我之前的 quad + 方向光测试 readback 显示被打亮（非黑）—— 复现不出。
// 本测试逐个加上场景的关键差异(暖色 metallic=0 材质 / EnvironmentComponent /
// 方向光)对照,精确定位哪个条件触发全黑。
//
// readback 中心像素:lum < 0.05 视为"黑"。

#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/MeshLoader.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/BuiltinPostProcessChain.h>
#include <orange/engine/render/EnvironmentComponent.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/PostProcessChain.h>
#include <orange/engine/render/Material.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <orange/renderer/RenderDevice.h>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

using Orange::Engine::Entity;
using Orange::Engine::World;
using Orange::Engine::Asset::AssetHandle;
using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Asset::MeshAsset;
using Orange::Engine::Asset::ShaderAsset;
using Orange::Engine::Asset::ShaderLoader;
using Orange::Engine::Asset::VertexNormal3;
using Orange::Engine::Asset::VertexPosition3;
using Orange::Engine::Asset::VertexUV2;
using Orange::Engine::Render::Camera;
using Orange::Engine::Render::DirectionalLight;
using Orange::Engine::Render::EnvironmentComponent;
using Orange::Engine::Render::MakeDirectionalLightRotationFromDir;
using Orange::Engine::Render::Material;
using Orange::Engine::Render::MaterialInstance;
using Orange::Engine::Render::MaterialSystem;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Scene::TransformComponent;

namespace
{

    std::unique_ptr<MeshAsset> MakeQuadMesh()
    {
        std::vector<VertexPosition3> pos = {
            {-0.6f, -0.6f, 0.0f},
            {0.6f, -0.6f, 0.0f},
            {0.6f, 0.6f, 0.0f},
            {-0.6f, 0.6f, 0.0f},
        };
        std::vector<VertexUV2> uv = {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
            {0.0f, 1.0f},
        };
        std::vector<VertexNormal3> nrm = {
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
        };
        std::vector<std::uint32_t> idx = {0, 1, 2, 0, 2, 3}; // CCW 正面朝向(对齐 Pipeline FrontFace::CCW)
        return std::make_unique<MeshAsset>(std::move(pos), std::move(uv),
                                           std::move(nrm), std::move(idx));
    }

    // 渲染一帧并返回中心像素 luminance。withEnv: 是否挂 EnvironmentComponent(空 cubemap)。
    float RenderAndReadCenter(Pipeline& pipeline, AssetHandle<MeshAsset> mesh,
                              MaterialInstance* inst, bool withEnv, const char* label,
                              bool withLight = true)
    {
        World  world;
        Entity camE = world.CreateEntity();
        // 透视相机 + 明确 lookAt:eye(0,0,3) 看向原点 → quad/sphere 都正面框入,
        // 消除 ortho 近/远裁剪 + 取景歧义。
        Camera cam = Camera::Perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
        cam.view   = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f),
                                 glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent(camE, cam);

        if (withLight)
        {
            Entity             lightE = world.CreateEntity();
            TransformComponent lt{};
            lt.rotation = MakeDirectionalLightRotationFromDir(glm::vec3(0.0f, 0.0f, -1.0f));
            world.AddComponent(lightE, lt);
            world.AddComponent(lightE, DirectionalLight{});
        }

        if (withEnv)
        {
            Entity               envE = world.CreateEntity();
            EnvironmentComponent env{}; // cubemap 空（与 pbr_showcase 场景一致）
            env.intensity = 1.2f;
            world.AddComponent(envE, env);
        }

        Entity e = world.CreateEntity();
        world.AddComponent(e, TransformComponent{});
        RenderableComponent rc;
        rc.mesh             = mesh;
        rc.materialInstance = inst;
        world.AddComponent(e, rc);

        pipeline.Render(world);

        float       px[4] = {0, 0, 0, 0};
        const bool  ok    = pipeline.DebugReadbackPixel(128, 128, px);
        const float lum   = px[0] + px[1] + px[2];
        std::fprintf(stderr, "  [%s] 中心 RGBA=(%.3f,%.3f,%.3f,%.3f) lum=%.3f ok=%d\n",
                     label, px[0], px[1], px[2], px[3], lum, ok ? 1 : 0);
        // 回归门:编辑器模板路径(RegisterTemplatesFromDirectory)下 PBR 必须被画
        // 出来且被打亮。若 pbr 模板缺 set1/tangent 声明(GAP-2026-05-25 回归),
        // shader 采样未绑 descriptor → 管线非法 → 中心读到背景/黑 → lum 极低。
        assert(ok && "DebugReadbackPixel 失败");
        assert(lum > 0.05f && "PBR(编辑器模板路径)渲染全黑 —— set1/tangent 未绑回归");
        return lum;
    }

} // namespace

int main()
{
    std::fprintf(stdout, "[PbrSceneBlackReproTest] running\n");

    Orange::Renderer::RenderDeviceDesc deviceDesc{};
    deviceDesc.mBackend          = Orange::Renderer::BackendType::Default;
    deviceDesc.mEnableValidation = true;
    auto pDevice                 = Orange::Renderer::RenderDevice::Create(deviceDesc);
    if (!pDevice)
    {
        std::fprintf(stderr, "[PbrSceneBlackReproTest] 无 Vulkan,跳过\n");
        return 0;
    }

    AssetRegistry assets;
    {
        auto reg = assets.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
        assert(reg.IsOk());
    }
    auto meshRes = assets.Insert<MeshAsset>("test/quad", MakeQuadMesh());
    assert(meshRes.IsOk());
    AssetHandle<MeshAsset> mesh = meshRes.Value();

    MaterialSystem matSys(assets);
    {
        // ★ 用编辑器的真实路径:从 .template.json 目录注册(不是 RegisterBuiltins!)。
        // 编辑器的 pbr 模板来自 pbr.template.json,其 textureSlots 为空 → 复现 set 1 未绑。
        const std::filesystem::path tdir =
            std::string(ORANGE_ENGINE_REPO_ASSETS_DIR) + "/shaders/templates";
        auto rb = matSys.RegisterTemplatesFromDirectory(tdir);
        if (rb.IsErr())
        {
            std::fprintf(stderr, "[PbrSceneBlackReproTest] RegisterTemplatesFromDirectory 失败 "
                                 "(code=%u) dir=%s\n",
                         static_cast<unsigned>(rb.Error()),
                         tdir.string().c_str());
            return 1;
        }
        const Material* pbrT = matSys.FindTemplate("pbr");
        std::fprintf(stderr, "  [template] pbr textureSlots=%zu (空=会复现黑)\n",
                     pbrT ? pbrT->textureSlots.size() : 99);
    }

    // 暖色 metallic=0 材质,匹配 pbr_showcase 的 warm_m0r0。
    auto warm = matSys.CreateInstance("pbr");
    assert(warm);
    warm->SetUniform("uBaseColor", glm::vec4(1.0f, 0.78f, 0.34f, 1.0f));
    warm->SetUniform("uMRA", glm::vec4(0.0f, 0.1f, 1.0f, 0.0f)); // metallic=0 roughness=0.1 ao=1

    Pipeline pipeline;
    {
        auto init = pipeline.InitializeOffscreen(*pDevice, assets, 256, 256);
        if (init.IsErr())
        {
            std::fprintf(stderr, "[PbrSceneBlackReproTest] InitializeOffscreen 失败,跳过\n");
            return 0;
        }
    }

    // ★ 编辑器实际设置:Bloom + Tonemap + LUT 后处理链(我之前用 passthrough,
    // 没覆盖这条!)。这是 quad 测试唯一漏掉的差异。
    auto chain = std::make_unique<Orange::Engine::Render::PostProcessChain>(
        Orange::Engine::Render::BuiltinPostProcessChain::CreateDefault());
    pipeline.SetPostProcessChain(chain.get());

    // 对照:无 Environment vs 有 Environment(空 cubemap)。
    const float lumNoEnv   = RenderAndReadCenter(pipeline, mesh, warm.get(), false, "warm-noEnv");
    const float lumWithEnv = RenderAndReadCenter(pipeline, mesh, warm.get(), true, "warm-withEnv");

    std::fprintf(stderr, "  => lumNoEnv=%.3f lumWithEnv=%.3f\n", lumNoEnv, lumWithEnv);

    // 关键:加载真实 sphere.mesh(经 MeshLoader v4 → ComputeTangentsFromTriangles
    // 算真实切线),复现用户场景的网格。注册 MeshLoader 后 Load。
    {
        using ::Orange::Engine::Asset::MeshLoader;
        auto mreg = assets.RegisterLoader<MeshAsset>(std::make_unique<MeshLoader>());
        // 已注册过会返回 err,忽略。
        (void)mreg;
        const std::string spherePath =
            std::string(ORANGE_ENGINE_REPO_ASSETS_DIR) + "/meshes/sphere.mesh";
        auto sres = assets.Load<MeshAsset>(spherePath);
        if (sres.IsErr())
        {
            std::fprintf(stderr, "  [sphere] Load 失败 (code=%u) path=%s\n",
                         static_cast<unsigned>(sres.Error()), spherePath.c_str());
        }
        else
        {
            const auto* sm = assets.Get<MeshAsset>(sres.Value());
            std::fprintf(stderr, "  [sphere] loaded vtx=%zu idx=%zu hasTangents=%d\n",
                         sm ? sm->VertexCount() : 0, sm ? sm->IndexCount() : 0,
                         (sm && sm->HasTangents()) ? 1 : 0);
            const float lumSphere =
                RenderAndReadCenter(pipeline, sres.Value(), warm.get(), false, "warm-sphere");
            std::fprintf(stderr, "  => lumSphere=%.3f\n", lumSphere);

            // 编辑器可见性机制:0.5 dummy IBL ambient 补光。测"仅 ambient、无
            // 直接光"是否被 keystone 破坏(场景里背光面 / 全场景靠它可见)。
            pipeline.SetDummyIblAmbient(0.5f, 0.5f, 0.5f);
            const float lumAmbDirect =
                RenderAndReadCenter(pipeline, sres.Value(), warm.get(), true, "sphere-amb+light", true);
            const float lumAmbOnly =
                RenderAndReadCenter(pipeline, sres.Value(), warm.get(), true, "sphere-ambONLY", false);
            std::fprintf(stderr, "  => lumAmbDirect=%.3f lumAmbOnly=%.3f\n",
                         lumAmbDirect, lumAmbOnly);
        }
    }

    pipeline.Shutdown();
    if (Orange::Failed(pDevice->WaitIdle()))
    {
        return 1;
    }

    std::fprintf(stdout, "[PbrSceneBlackReproTest] done\n");
    return 0;
}
