// Pipeline 的 per-template `RHIPipeline` 缓存验证 —— Task 06.02 验收。
//
// 覆盖两条核心路径：
//   1. 同 Material 多 instance 只编一次 pipeline：用 MaterialSystem 创
//      建两条 textured instance 喂给两条 drawable，Render 一帧后 cache
//      count 恰好 == 1（只有 textured 一条 Material 进表）；
//   2. 不同 Material 编出独立 pipeline：再加一条 toon instance 的 drawable，
//      Render 一帧后 cache count == 2（textured + toon 各一条）。
//
// 本测试需要真实的 RHI + window：通过 `Platform::Window::Create({.visible=false})`
// 拉一个隐藏窗口（GLFW 在 Windows 上仍创建底层 HWND，足够 Renderer 建
// swap-chain），跑一遍最小化的 Pipeline.Initialize → Render → Shutdown
// 流程。失败可能源于：Vulkan SDK 未装、显卡驱动不支持当前 Vulkan 版本、
// validation layer 找不到。这些场景应当在 CI 镜像维度提前规避。

#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/platform/Window.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <glm/glm.hpp>

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
using Orange::Engine::Render::MaterialInstance;
using Orange::Engine::Render::MaterialSystem;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Scene::TransformComponent;

namespace
{

// 程序式造一个两三角形 quad，复用为所有 entity 的 mesh 资源——
// per-template 缓存的 dedup 关键由 Material 决定，与 mesh 无关。
std::unique_ptr<MeshAsset> MakeQuadMesh()
{
    std::vector<VertexPosition3> positions = {
        {-0.5f, -0.5f, 0.0f},
        { 0.5f, -0.5f, 0.0f},
        { 0.5f,  0.5f, 0.0f},
        {-0.5f,  0.5f, 0.0f},
    };
    std::vector<VertexUV2> uvs = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
    };
    std::vector<std::uint32_t> indices = {0, 2, 1, 0, 3, 2};
    return std::make_unique<MeshAsset>(std::move(positions),
                                       std::move(uvs),
                                       std::move(indices));
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[PipelineTemplateCacheTest] running\n");

    // 1. 隐藏窗口 -------------------------------------------------------
    WindowDesc winDesc{};
    winDesc.title   = "PipelineTemplateCacheTest";
    winDesc.width   = 320;
    winDesc.height  = 240;
    winDesc.visible = false;
    auto winResult = Window::Create(winDesc);
    if (winResult.IsErr())
    {
        std::fprintf(stderr,
                     "[PipelineTemplateCacheTest] Window::Create failed (code=%u)\n",
                     static_cast<unsigned>(winResult.Error()));
        return 1;
    }
    auto window = std::move(winResult).Value();

    // 2. AssetRegistry + ShaderLoader + mesh 注册 ----------------------
    AssetRegistry assets;
    {
        auto reg = assets.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
        if (reg.IsErr())
        {
            std::fprintf(stderr,
                         "[PipelineTemplateCacheTest] RegisterLoader<ShaderAsset> failed (code=%u)\n",
                         static_cast<unsigned>(reg.Error()));
            return 1;
        }
    }
    auto meshHandleResult = assets.Insert<MeshAsset>("test/quad", MakeQuadMesh());
    if (meshHandleResult.IsErr())
    {
        std::fprintf(stderr,
                     "[PipelineTemplateCacheTest] Insert<MeshAsset> failed (code=%u)\n",
                     static_cast<unsigned>(meshHandleResult.Error()));
        return 1;
    }
    AssetHandle<MeshAsset> meshHandle = meshHandleResult.Value();

    // 3. MaterialSystem + 两个 textured instance + 一个 toon instance --
    MaterialSystem matSys(assets);
    {
        auto rb = matSys.RegisterBuiltins();
        if (rb.IsErr())
        {
            std::fprintf(stderr,
                         "[PipelineTemplateCacheTest] RegisterBuiltins failed (code=%u)\n",
                         static_cast<unsigned>(rb.Error()));
            return 1;
        }
    }
    auto texturedA = matSys.CreateInstance("textured");
    auto texturedB = matSys.CreateInstance("textured");
    auto toonInst  = matSys.CreateInstance("toon");
    assert(texturedA && texturedB && toonInst);
    // 同模板 -> 同 const Material*；异模板 -> 不同 const Material*
    assert(texturedA->GetMaterial() == texturedB->GetMaterial());
    assert(texturedA->GetMaterial() != toonInst->GetMaterial());

    // 4. Pipeline 初始化 ------------------------------------------------
    Pipeline pipeline;
    {
        auto init = pipeline.Initialize(*window, assets);
        if (init.IsErr())
        {
            std::fprintf(stderr,
                         "[PipelineTemplateCacheTest] Pipeline::Initialize failed (code=%u). "
                         "本机可能没装 Vulkan / 显卡驱动不支持 Vulkan 1.3，跳过 cache 验证。\n",
                         static_cast<unsigned>(init.Error()));
            // Initialize 失败时仍要检查 TemplatePipelineCount 默认 0、
            // 然后正常退出 —— ctest 不算失败（与"GPU 不可用"环境兼容）。
            assert(pipeline.TemplatePipelineCount() == 0);
            return 0;
        }
    }
    assert(pipeline.IsInitialized());
    assert(pipeline.TemplatePipelineCount() == 0);  // 还没 Render，cache 应空

    // 5. 第一遍 Render：两条 textured drawable -------------------------
    {
        World world;

        Entity camE = world.CreateEntity();
        world.AddComponent(camE, Camera::Orthographic(-1, 1, -1, 1, 0, 1));

        auto addEntity = [&](MaterialInstance* inst) {
            Entity e = world.CreateEntity();
            world.AddComponent(e, TransformComponent{});
            RenderableComponent rc;
            rc.mesh             = meshHandle;
            rc.materialInstance = inst;
            world.AddComponent(e, rc);
        };
        addEntity(texturedA.get());
        addEntity(texturedB.get());

        pipeline.Render(world);
        // 同一 Material 两个 instance 只编一次 pipeline。
        assert(pipeline.TemplatePipelineCount() == 1);
        std::fprintf(stdout, "  [PASS] 同 Material 多 instance 只编一次 pipeline\n");
    }

    // 6. 第二遍 Render：再加一个 toon drawable -------------------------
    {
        World world;

        Entity camE = world.CreateEntity();
        world.AddComponent(camE, Camera::Orthographic(-1, 1, -1, 1, 0, 1));

        auto addEntity = [&](MaterialInstance* inst) {
            Entity e = world.CreateEntity();
            world.AddComponent(e, TransformComponent{});
            RenderableComponent rc;
            rc.mesh             = meshHandle;
            rc.materialInstance = inst;
            world.AddComponent(e, rc);
        };
        addEntity(texturedA.get());
        addEntity(toonInst.get());

        pipeline.Render(world);
        // 现在 textured + toon 各一条，cache 应为 2。
        assert(pipeline.TemplatePipelineCount() == 2);
        std::fprintf(stdout, "  [PASS] 不同 Material 编出独立 pipeline\n");
    }

    // 7. 第三遍 Render：仍是同两条 Material —— 缓存命中、数量不变 -----
    {
        World world;

        Entity camE = world.CreateEntity();
        world.AddComponent(camE, Camera::Orthographic(-1, 1, -1, 1, 0, 1));

        Entity e = world.CreateEntity();
        world.AddComponent(e, TransformComponent{});
        RenderableComponent rc;
        rc.mesh             = meshHandle;
        rc.materialInstance = toonInst.get();
        world.AddComponent(e, rc);

        pipeline.Render(world);
        assert(pipeline.TemplatePipelineCount() == 2);  // 没新增
        std::fprintf(stdout, "  [PASS] 重复 Material 不重复编 pipeline\n");
    }

    // 8. fallback：drawable.materialInstance == nullptr 时 Pipeline lazy-load
    //    的 builtin textured Material 走另一个 const Material* 地址 → 缓
    //    存增长一条。
    {
        World world;

        Entity camE = world.CreateEntity();
        world.AddComponent(camE, Camera::Orthographic(-1, 1, -1, 1, 0, 1));

        Entity e = world.CreateEntity();
        world.AddComponent(e, TransformComponent{});
        RenderableComponent rc;
        rc.mesh             = meshHandle;
        rc.materialInstance = nullptr;  // 显式 fallback
        world.AddComponent(e, rc);

        pipeline.Render(world);
        // builtin textured Material 是 Pipeline 自己 lazy-load 的实例，
        // 与 MaterialSystem 内的 textured 是不同的 const Material* 地址。
        // 所以缓存增长到 3。
        assert(pipeline.TemplatePipelineCount() == 3);
        std::fprintf(stdout, "  [PASS] materialInstance == nullptr 走 builtin fallback、独立 cache slot\n");
    }

    pipeline.Shutdown();
    assert(!pipeline.IsInitialized());
    assert(pipeline.TemplatePipelineCount() == 0);

    std::fprintf(stdout, "[PipelineTemplateCacheTest] all tests passed.\n");
    return 0;
}
