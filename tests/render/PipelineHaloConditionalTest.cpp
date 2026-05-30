// Pipeline halo template 条件编测试（GAP-2026-05-30 方案 b 验收）。
//
// 锁住 halo template 的 lazy 条件编逻辑（`Pipeline.cpp` halo pass）：
//   * 有 PointLight 但 haloEnabled=false → halo template 不编；
//   * 无 PointLight → halo template 不编；
//   * 有 haloEnabled PointLight → halo template lazy 编（count += 1），且
//     Render 不崩（覆盖此前零 headless 覆盖的"有 PointLight halo"路径）。
//
// 背景：halo G3（cb8041c）此前在 PointLight 遍历前**无条件**预编 halo
// template，令无点光场景也常驻一个永不用的 pipeline，并让 pipeline_*_test 的
// TemplatePipelineCount 期望错位（assert 弹窗 hang 到 timeout）。方案 b 把编译
// 推迟到首个 haloEnabled PointLight 命中。本测试同时锁两个方向，防回归。
//
// 三个 case 共用同一 Pipeline 实例，templatePipelines cache 累积——只有 case 3
// 命中 haloEnabled 才把 count 从 1 推到 2。

#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/platform/Window.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <cassert>
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
using Orange::Engine::Asset::VertexPosition3;
using Orange::Engine::Asset::VertexUV2;
using Orange::Engine::Platform::Window;
using Orange::Engine::Platform::WindowDesc;
using Orange::Engine::Render::Camera;
using Orange::Engine::Render::MaterialSystem;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::PointLight;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Scene::TransformComponent;

namespace
{

std::unique_ptr<MeshAsset> MakeQuadMesh()
{
    std::vector<VertexPosition3> positions = {
        {-0.5f, -0.5f, 0.0f}, {0.5f, -0.5f, 0.0f},
        {0.5f, 0.5f, 0.0f},   {-0.5f, 0.5f, 0.0f},
    };
    std::vector<VertexUV2> uvs = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
    };
    std::vector<std::uint32_t> indices = {0, 2, 1, 0, 3, 2};
    return std::make_unique<MeshAsset>(std::move(positions), std::move(uvs),
                                       std::move(indices));
}

Entity AddCamera(World& world)
{
    Entity camE = world.CreateEntity();
    world.AddComponent(camE, Camera::Orthographic(-1, 1, -1, 1, 0, 1));
    return camE;
}

void AddTexturedDrawable(World& world, AssetHandle<MeshAsset> mesh,
                         Orange::Engine::Render::MaterialInstance* mat)
{
    Entity e = world.CreateEntity();
    world.AddComponent(e, TransformComponent{});
    RenderableComponent rc;
    rc.mesh             = mesh;
    rc.materialInstance = mat;
    world.AddComponent(e, rc);
}

void AddPointLight(World& world, bool haloEnabled)
{
    Entity lightE = world.CreateEntity();
    world.AddComponent(lightE, TransformComponent{});
    PointLight pl{};
    pl.haloEnabled   = haloEnabled;
    pl.haloRadius    = 0.25f;
    pl.haloIntensity = 0.5f;
    world.AddComponent(lightE, pl);
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[PipelineHaloConditionalTest] running\n");

    WindowDesc winDesc{};
    winDesc.title   = "PipelineHaloConditionalTest";
    winDesc.width   = 320;
    winDesc.height  = 240;
    winDesc.visible = false;
    auto winResult = Window::Create(winDesc);
    if (winResult.IsErr())
    {
        std::fprintf(stderr, "[PipelineHaloConditionalTest] Window::Create failed (code=%u)\n",
                     static_cast<unsigned>(winResult.Error()));
        return 1;
    }
    auto window = std::move(winResult).Value();

    AssetRegistry assets;
    {
        auto reg = assets.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
        assert(reg.IsOk());
    }
    auto meshHandleResult = assets.Insert<MeshAsset>("test/quad", MakeQuadMesh());
    assert(meshHandleResult.IsOk());
    AssetHandle<MeshAsset> meshHandle = meshHandleResult.Value();

    MaterialSystem matSys(assets);
    {
        auto rb = matSys.RegisterBuiltins();
        assert(rb.IsOk());
    }
    auto texInst = matSys.CreateInstance("textured");
    assert(texInst);

    Pipeline pipeline;
    {
        auto init = pipeline.Initialize(*window, assets);
        if (init.IsErr())
        {
            std::fprintf(stderr,
                         "[PipelineHaloConditionalTest] Initialize failed (code=%u). "
                         "本机可能无 Vulkan，跳过。\n",
                         static_cast<unsigned>(init.Error()));
            return 0;
        }
    }
    assert(pipeline.IsInitialized());

    // ---- case 1: PointLight 但 haloEnabled=false → halo 不编 ----------
    {
        World world;
        AddCamera(world);
        AddTexturedDrawable(world, meshHandle, texInst.get());
        AddPointLight(world, /*haloEnabled=*/false);

        pipeline.Render(world);
        assert(pipeline.TemplatePipelineCount() == 1);  // 仅 textured，halo 不编
        std::fprintf(stdout, "  [PASS] PointLight 但 haloEnabled=false：count=1（halo 不编）\n");
    }

    // ---- case 2: 无 PointLight → halo 不编 ----------------------------
    {
        World world;
        AddCamera(world);
        AddTexturedDrawable(world, meshHandle, texInst.get());

        pipeline.Render(world);
        assert(pipeline.TemplatePipelineCount() == 1);  // cache 命中，仍 1
        std::fprintf(stdout, "  [PASS] 无 PointLight：count=1（halo 不编）\n");
    }

    // ---- case 3: haloEnabled PointLight → halo lazy 编 + Render 不崩 ---
    {
        World world;
        AddCamera(world);
        AddTexturedDrawable(world, meshHandle, texInst.get());
        AddPointLight(world, /*haloEnabled=*/true);

        pipeline.Render(world);
        // textured（cache 命中）+ halo（首个 haloEnabled PointLight lazy 编）。
        assert(pipeline.TemplatePipelineCount() == 2);
        std::fprintf(stdout, "  [PASS] haloEnabled PointLight：count=2（halo lazy 编 + Render 不崩）\n");
    }

    // ---- debug-view Normals mode：用 debug normals material 渲染所有 drawable
    //      （world-normal-as-RGB），验证不崩 + debug pipeline 编出 -------------
    {
        World world;
        AddCamera(world);
        AddTexturedDrawable(world, meshHandle, texInst.get());
        const std::size_t before = pipeline.TemplatePipelineCount();
        pipeline.SetDebugViewMode(Orange::Engine::Render::DebugViewMode::Normals);
        pipeline.Render(world);                          // drawable 改用 debug normals material
        // debug normals pipeline **真编出**（count +1），而非 shader 缺失跳过
        // （后者会让 count 不变）——这是 normals debug view 真正生效的关键断言。
        assert(pipeline.TemplatePipelineCount() == before + 1);
        pipeline.SetDebugViewMode(Orange::Engine::Render::DebugViewMode::Lit);  // 复位
        std::fprintf(stdout, "  [PASS] DebugViewMode::Normals Render 不崩 + debug pipeline 编出\n");
    }

    // ---- debug-view Unlit mode：drawable 用 debug unlit material 渲染（直出
    //      base color），验证 debug pipeline 真编出 ----------------------------
    {
        World world;
        AddCamera(world);
        AddTexturedDrawable(world, meshHandle, texInst.get());
        const std::size_t before = pipeline.TemplatePipelineCount();
        pipeline.SetDebugViewMode(Orange::Engine::Render::DebugViewMode::Unlit);
        pipeline.Render(world);
        assert(pipeline.TemplatePipelineCount() == before + 1);  // debug unlit pipeline 编出
        pipeline.SetDebugViewMode(Orange::Engine::Render::DebugViewMode::Lit);
        std::fprintf(stdout, "  [PASS] DebugViewMode::Unlit Render 不崩 + debug pipeline 编出\n");
    }

    std::fprintf(stdout, "[PipelineHaloConditionalTest] all passed\n");
    return 0;
}
