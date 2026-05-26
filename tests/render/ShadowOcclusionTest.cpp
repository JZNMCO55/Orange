// 多 shadow caster 阴影遮挡的 headless 正确性测试（GAP-2026-05-26 G2/G3）。
//
// 思路：差分遮挡 —— 同一个被点光照亮的地面中心像素，分别在「有/无遮挡物」
// 两种场景下渲染 + readback，断言有遮挡时中心明显变暗。这直接验证：
//   * point 全向 cubemap 阴影（G3）的 cube face 选择 + 距离重建 + 采样正确
//     —— 若 cube 约定错（命中错 face / uv 翻转），中心阴影不会落在预期位置，
//     有遮挡的中心仍亮 → 断言失败。遮挡点选在离轴位置以抓 face/uv 翻转。
//   * spot 透视阴影（G2）同款差分。
//
// 关键布置：地面 quad 法线 +Z，中性 fallback 光（无 DirectionalLight 时
// Pipeline 喂的斜下白光）对 +Z 面 NoL ≤ 0 → 地面几乎只被点光/聚光照亮，
// 故遮挡后中心近黑，差分信号极强。
//
// 无 Vulkan（CI 无 GPU）时跳过（return 0），与 PbrSceneBlackReproTest 同款。

#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/render/ShadowConfig.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <orange/renderer/RenderDevice.h>

#include <glm/vec3.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
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
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::PointLight;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Render::SpotLight;
using Orange::Engine::Scene::TransformComponent;

namespace
{

// XY 平面单位 quad（法线 +Z，边长 1.2，对齐 Pipeline FrontFace::CCW）。
std::unique_ptr<MeshAsset> MakeQuadMesh()
{
    std::vector<VertexPosition3> pos = {
        {-0.6f, -0.6f, 0.0f}, {0.6f, -0.6f, 0.0f},
        {0.6f, 0.6f, 0.0f},   {-0.6f, 0.6f, 0.0f},
    };
    std::vector<VertexUV2> uv = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
    };
    std::vector<VertexNormal3> nrm = {
        {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f},
    };
    std::vector<std::uint32_t> idx = {0, 1, 2, 0, 2, 3};
    return std::make_unique<MeshAsset>(std::move(pos), std::move(uv),
                                       std::move(nrm), std::move(idx));
}

TransformComponent MakeTransform(glm::vec3 pos, glm::vec3 scale)
{
    TransformComponent tc{};
    tc.position = pos;
    tc.scale    = scale;
    return tc;
}

// 渲染一帧（地面 + 可选遮挡物 + 一盏光）并 readback 指定像素 luminance。
// lightSetup 回调往 world 里挂光（point 或 spot），withOccluder 决定是否挂
// 遮挡物（位于光到地面中心的连线中点，离轴 x=0.4）。
template <typename LightSetup>
float RenderCenterLum(Pipeline& pipeline, AssetHandle<MeshAsset> quad,
                      bool withOccluder, const char* label, LightSetup&& lightSetup)
{
    World world;

    Entity camE = world.CreateEntity();
    Camera cam  = Camera::Perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
    cam.view    = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f),
                              glm::vec3(0.0f, 0.0f, 0.0f),
                              glm::vec3(0.0f, 1.0f, 0.0f));
    world.AddComponent(camE, cam);

    lightSetup(world);

    // 地面：大 quad（法线 +Z 朝相机 + 光），scale 3 填满视野。
    Entity floorE = world.CreateEntity();
    world.AddComponent(floorE, MakeTransform({0.0f, 0.0f, 0.0f}, {3.0f, 3.0f, 1.0f}));
    {
        RenderableComponent rc;
        rc.mesh             = quad;
        rc.materialInstance = nullptr;  // builtin PBR 默认材质
        rc.castsShadow      = true;
        world.AddComponent(floorE, rc);
    }

    // 遮挡物：小 quad，位于光→地面中心连线中点（离轴），挡住到中心的光。
    if (withOccluder)
    {
        Entity occE = world.CreateEntity();
        world.AddComponent(occE, MakeTransform({0.4f, 0.0f, 0.75f}, {0.25f, 0.25f, 1.0f}));
        RenderableComponent rc;
        rc.mesh             = quad;
        rc.materialInstance = nullptr;
        rc.castsShadow      = true;
        world.AddComponent(occE, rc);
    }

    pipeline.Render(world);

    float px[4] = {0, 0, 0, 0};
    const bool ok = pipeline.DebugReadbackPixel(128, 128, px);
    const float lum = px[0] + px[1] + px[2];
    std::fprintf(stderr, "  [%s] 中心 RGB=(%.3f,%.3f,%.3f) lum=%.3f ok=%d\n",
                 label, px[0], px[1], px[2], lum, ok ? 1 : 0);
    assert(ok && "DebugReadbackPixel 失败");
    return lum;
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[ShadowOcclusionTest] running\n");

    Orange::Renderer::RenderDeviceDesc deviceDesc{};
    deviceDesc.mBackend          = Orange::Renderer::BackendType::Default;
    deviceDesc.mEnableValidation = true;  // 阴影 pass validation-clean，且开 validation 仍快（4 帧 0.24s）
    auto pDevice = Orange::Renderer::RenderDevice::Create(deviceDesc);
    if (!pDevice)
    {
        std::fprintf(stderr, "[ShadowOcclusionTest] 无 Vulkan，跳过\n");
        return 0;
    }

    AssetRegistry assets;
    {
        auto reg = assets.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
        assert(reg.IsOk());
    }
    auto meshRes = assets.Insert<MeshAsset>("test/quad", MakeQuadMesh());
    assert(meshRes.IsOk());
    AssetHandle<MeshAsset> quad = meshRes.Value();

    Pipeline pipeline;
    {
        auto init = pipeline.InitializeOffscreen(*pDevice, assets, 256, 256);
        if (init.IsErr())
        {
            std::fprintf(stderr, "[ShadowOcclusionTest] InitializeOffscreen 失败，跳过\n");
            return 0;
        }
    }
    // 关闭 dummy IBL ambient，让地面只被显式光源照亮 → 阴影差分更干净。
    pipeline.SetDummyIblAmbient(0.0f, 0.0f, 0.0f);
    // 低分辨率 shadow map —— headless 测试只验正确性，256 足够且快。
    {
        Orange::Engine::Render::ShadowConfig sc{};
        sc.mapResolution = 256;
        pipeline.SetShadowConfig(sc);
    }

    // ---- G3：point 全向 cubemap 阴影 ------------------------------------
    auto setupPoint = [](World& w)
    {
        Entity le = w.CreateEntity();
        TransformComponent t{};
        t.position = {0.8f, 0.0f, 1.5f};  // +Z 侧，到地面中心连线中点 = (0.4,0,0.75)
        w.AddComponent(le, t);
        PointLight pl{};
        pl.color       = {1.0f, 1.0f, 1.0f};
        pl.intensity   = 25.0f;
        pl.range       = 10.0f;
        pl.castsShadow = true;
        w.AddComponent(le, pl);
    };
    int failures = 0;
    const float ptLit    = RenderCenterLum(pipeline, quad, false, "point-noOccluder", setupPoint);
    const float ptShadow = RenderCenterLum(pipeline, quad, true,  "point-occluded",   setupPoint);
    std::fprintf(stderr, "  => point: lit=%.3f shadow=%.3f ratio=%.3f\n",
                 ptLit, ptShadow, (ptLit > 0.0f) ? ptShadow / ptLit : -1.0f);
    if (!(ptLit > 0.1f))            { std::fprintf(stderr, "  [FAIL] point 未照亮中心\n"); ++failures; }
    if (!(ptShadow < ptLit * 0.6f)) { std::fprintf(stderr, "  [FAIL] point cubemap 阴影未遮挡中心\n"); ++failures; }

    // ---- G2：spot 透视阴影 ----------------------------------------------
    auto setupSpot = [](World& w)
    {
        Entity le = w.CreateEntity();
        TransformComponent t{};
        t.position = {0.8f, 0.0f, 1.5f};
        // 锥光朝地面中心 (0,0,0)：dir = normalize(center - pos)。用 from-to
        // 旋转把 -Y（kSpotLightLocalForward）转到该方向。
        const glm::vec3 dir = glm::normalize(glm::vec3(0.0f) - t.position);
        t.rotation = Orange::Engine::Render::MakeDirectionalLightRotationFromDir(dir);
        w.AddComponent(le, t);
        SpotLight sl{};
        sl.color          = {1.0f, 1.0f, 1.0f};
        sl.intensity      = 25.0f;
        sl.range          = 10.0f;
        sl.innerConeAngle = 0.5f;
        sl.outerConeAngle = 0.7f;  // 宽锥确保覆盖中心 + 遮挡点
        sl.castsShadow    = true;
        w.AddComponent(le, sl);
    };
    const float spLit    = RenderCenterLum(pipeline, quad, false, "spot-noOccluder", setupSpot);
    const float spShadow = RenderCenterLum(pipeline, quad, true,  "spot-occluded",   setupSpot);
    std::fprintf(stderr, "  => spot: lit=%.3f shadow=%.3f ratio=%.3f\n",
                 spLit, spShadow, (spLit > 0.0f) ? spShadow / spLit : -1.0f);
    if (!(spLit > 0.1f))            { std::fprintf(stderr, "  [FAIL] spot 未照亮中心\n"); ++failures; }
    if (!(spShadow < spLit * 0.6f)) { std::fprintf(stderr, "  [FAIL] spot 透视阴影未遮挡中心\n"); ++failures; }

    pipeline.Shutdown();
    if (Orange::Failed(pDevice->WaitIdle())) { return 1; }

    if (failures > 0)
    {
        std::fprintf(stderr, "[ShadowOcclusionTest] %d check(s) FAILED.\n", failures);
        return 1;
    }
    std::fprintf(stdout, "[ShadowOcclusionTest] all checks passed.\n");
    return 0;
}
