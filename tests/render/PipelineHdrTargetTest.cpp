// Pipeline 的 HDR off-screen target 生命周期 + OnResize 重建覆盖 ——
// Task 06.03 验收。
//
// 覆盖路径：
//   1. 未 Initialize 时 GetHdrTargetSize 返回 (0, 0)，TemplatePipelineCount
//      返回 0；
//   2. Initialize（隐藏 Window 的 framebuffer extent 给定）后 HDR 尺寸
//      跟 Window 的 framebuffer size 一致；尚未 Render 过 → 缓存为空；
//   3. Render 一帧（无 drawable / 无 camera）— Renderer.BeginFrame /
//      EndFrame 走通、不崩；HDR target 在 Render 中维持原尺寸；
//   4. Render 一帧（有 drawable + camera）— TemplatePipelineCount 增长，
//      HDR target 已经被画了一次；
//   5. OnResize 改变尺寸 → 下一次 Render 重建 HDR target、尺寸更新；
//   6. Shutdown 后 GetHdrTargetSize 回到 (0, 0)、TemplatePipelineCount 回 0。
//
// "validation layer 静默"无法在 ctest 内显式断言（validation 输出走到
// stderr），但只要 Render() 全程不崩 + Vulkan 不返回失败码，就基本说明
// 没有重大 layout / format / lifetime 错误——这是当前 0.x 阶段可接受
// 的近似验收。

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
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Scene::TransformComponent;

namespace
{

    std::unique_ptr<MeshAsset> MakeQuadMesh()
    {
        std::vector<VertexPosition3> positions = {
            {-0.5f, -0.5f, 0.0f},
            {0.5f, -0.5f, 0.0f},
            {0.5f, 0.5f, 0.0f},
            {-0.5f, 0.5f, 0.0f},
        };
        std::vector<VertexUV2> uvs = {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
            {0.0f, 1.0f},
        };
        std::vector<std::uint32_t> indices = {0, 2, 1, 0, 3, 2};
        return std::make_unique<MeshAsset>(std::move(positions),
                                           std::move(uvs),
                                           std::move(indices));
    }

} // namespace

int main()
{
    std::fprintf(stdout, "[PipelineHdrTargetTest] running\n");

    // ---- 1. Pipeline 未 Initialize 的 introspection -----------------
    {
        Pipeline      pipeline;
        std::uint32_t w = 9999;
        std::uint32_t h = 9999;
        pipeline.GetHdrTargetSize(w, h);
        assert(w == 0 && h == 0);
        assert(pipeline.TemplatePipelineCount() == 0);
        assert(!pipeline.IsInitialized());
        std::fprintf(stdout, "  [PASS] 未 Initialize 时 introspection 默认值\n");
    }

    // ---- 2. 隐藏 Window + Pipeline.Initialize -----------------------
    constexpr std::uint32_t kInitialW = 320;
    constexpr std::uint32_t kInitialH = 240;
    constexpr std::uint32_t kResizedW = 480;
    constexpr std::uint32_t kResizedH = 360;

    WindowDesc winDesc{};
    winDesc.title   = "PipelineHdrTargetTest";
    winDesc.width   = kInitialW;
    winDesc.height  = kInitialH;
    winDesc.visible = false;
    auto winResult  = Window::Create(winDesc);
    if (winResult.IsErr())
    {
        std::fprintf(stderr,
                     "[PipelineHdrTargetTest] Window::Create failed (code=%u)\n",
                     static_cast<unsigned>(winResult.Error()));
        return 1;
    }
    auto window = std::move(winResult).Value();

    AssetRegistry assets;
    {
        auto reg = assets.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
        if (reg.IsErr())
        {
            std::fprintf(stderr,
                         "[PipelineHdrTargetTest] RegisterLoader failed (code=%u)\n",
                         static_cast<unsigned>(reg.Error()));
            return 1;
        }
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
                         "[PipelineHdrTargetTest] Initialize failed (code=%u). "
                         "本机可能无 Vulkan，跳过 HDR target 验证。\n",
                         static_cast<unsigned>(init.Error()));
            std::uint32_t w = 9999;
            std::uint32_t h = 9999;
            pipeline.GetHdrTargetSize(w, h);
            assert(w == 0 && h == 0);
            return 0;
        }
    }
    assert(pipeline.IsInitialized());

    // Initialize 应该按 window framebuffer size 建好 HDR target。
    {
        std::uint32_t fbW = 0;
        std::uint32_t fbH = 0;
        window->GetFramebufferSize(fbW, fbH);
        std::uint32_t hdrW = 0;
        std::uint32_t hdrH = 0;
        pipeline.GetHdrTargetSize(hdrW, hdrH);
        // 隐藏窗口的 framebuffer size 取决于平台；只要非零并与 Pipeline
        // 内部记录一致即视为正确。
        assert(hdrW == fbW);
        assert(hdrH == fbH);
        assert(hdrW > 0);
        assert(hdrH > 0);
        std::fprintf(stdout,
                     "  [PASS] Initialize 后 HDR target = %ux%u（与 framebuffer 对齐）\n",
                     hdrW, hdrH);
    }
    assert(pipeline.TemplatePipelineCount() == 0); // 还没 Render

    // debug-view mode 公共 API 往返（默认 Lit + Set/Get 一致，零回归基线）。
    {
        using Orange::Engine::Render::DebugViewMode;
        assert(pipeline.GetDebugViewMode() == DebugViewMode::Lit); // 默认
        pipeline.SetDebugViewMode(DebugViewMode::Wireframe);
        assert(pipeline.GetDebugViewMode() == DebugViewMode::Wireframe);
        pipeline.SetDebugViewMode(DebugViewMode::Normals);
        assert(pipeline.GetDebugViewMode() == DebugViewMode::Normals);
        pipeline.SetDebugViewMode(DebugViewMode::Lit); // 复位，避免影响后续
        assert(pipeline.GetDebugViewMode() == DebugViewMode::Lit);
        std::fprintf(stdout, "  [PASS] debug-view mode API 往返（默认 Lit + Set/Get）\n");
    }

    // ---- 3. Render 空 World（无 camera / 无 drawable）---------------
    {
        World empty;
        pipeline.Render(empty);
        // 没 camera + 没 drawable 也不该崩；HDR target 不变；TemplatePipeline
        // cache 不增长。
        std::uint32_t hdrW = 0;
        std::uint32_t hdrH = 0;
        pipeline.GetHdrTargetSize(hdrW, hdrH);
        assert(hdrW > 0 && hdrH > 0);
        assert(pipeline.TemplatePipelineCount() == 0);
        std::fprintf(stdout, "  [PASS] Render 空 World 不崩、HDR target 维持\n");
    }

    // ---- 4. Render 含 drawable+camera 的 World ----------------------
    {
        World  world;
        Entity camE = world.CreateEntity();
        world.AddComponent(camE, Camera::Orthographic(-1, 1, -1, 1, 0, 1));

        Entity e = world.CreateEntity();
        world.AddComponent(e, TransformComponent{});
        RenderableComponent rc;
        rc.mesh             = meshHandle;
        rc.materialInstance = texInst.get();
        world.AddComponent(e, rc);

        pipeline.Render(world);
        assert(pipeline.TemplatePipelineCount() == 1); // textured 模板编一次

        std::uint32_t hdrW = 0;
        std::uint32_t hdrH = 0;
        pipeline.GetHdrTargetSize(hdrW, hdrH);
        assert(hdrW > 0 && hdrH > 0);
        std::fprintf(stdout, "  [PASS] Render 单 drawable 后 cache=%zu / HDR=%ux%u\n",
                     pipeline.TemplatePipelineCount(), hdrW, hdrH);
    }

    // ---- 5. OnResize → 下一帧重建 HDR target ------------------------
    {
        std::uint32_t prevW = 0;
        std::uint32_t prevH = 0;
        pipeline.GetHdrTargetSize(prevW, prevH);

        pipeline.OnResize(kResizedW, kResizedH);
        // OnResize 之后 Pipeline 把 dirty 标志记下来；尺寸要等下一次 Render
        // 时才会同步过去。
        std::uint32_t pendingW = 0;
        std::uint32_t pendingH = 0;
        pipeline.GetHdrTargetSize(pendingW, pendingH);
        // 仍是旧尺寸——因为 EnsureHdrTarget 还没被 Render 触发。
        assert(pendingW == prevW);
        assert(pendingH == prevH);

        World empty;
        pipeline.Render(empty);

        std::uint32_t newW = 0;
        std::uint32_t newH = 0;
        pipeline.GetHdrTargetSize(newW, newH);
        assert(newW == kResizedW);
        assert(newH == kResizedH);
        std::fprintf(stdout, "  [PASS] OnResize 后 Render 重建 HDR target (%ux%u → %ux%u)\n",
                     prevW, prevH, newW, newH);
    }

    // ---- 6. Resize 后再 Render drawable —— 验证 descriptor set 已重新指向新 view
    {
        World  world;
        Entity camE = world.CreateEntity();
        world.AddComponent(camE, Camera::Orthographic(-1, 1, -1, 1, 0, 1));

        Entity e = world.CreateEntity();
        world.AddComponent(e, TransformComponent{});
        RenderableComponent rc;
        rc.mesh             = meshHandle;
        rc.materialInstance = texInst.get();
        world.AddComponent(e, rc);

        pipeline.Render(world);
        // 同 Material 仍旧只有一条 pipeline cache；HDR target 仍是 resize 后尺寸。
        assert(pipeline.TemplatePipelineCount() == 1);
        std::uint32_t newW = 0;
        std::uint32_t newH = 0;
        pipeline.GetHdrTargetSize(newW, newH);
        assert(newW == kResizedW);
        assert(newH == kResizedH);
        std::fprintf(stdout, "  [PASS] resize 后 Render drawable 正常工作\n");
    }

    // ---- 7. Shutdown 后 introspection 回到默认 ----------------------
    pipeline.Shutdown();
    assert(!pipeline.IsInitialized());
    {
        std::uint32_t w = 9999;
        std::uint32_t h = 9999;
        pipeline.GetHdrTargetSize(w, h);
        assert(w == 0 && h == 0);
        assert(pipeline.TemplatePipelineCount() == 0);
        std::fprintf(stdout, "  [PASS] Shutdown 后 introspection 回零\n");
    }

    std::fprintf(stdout, "[PipelineHdrTargetTest] all tests passed.\n");
    return 0;
}
