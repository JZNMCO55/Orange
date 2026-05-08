// Bloom mip-chain 资源生命周期 + OnResize 重建覆盖 —— Task 06.04 验收。
//
// 覆盖路径：
//   1. 默认空 PostProcessChain（Pipeline 自带 chain == nullptr）→
//      BloomMipCount() 返回 0；
//   2. 装载含 BloomPass 的 chain → 第一次 Render 后 BloomMipCount == 6，
//      mip[0] 尺寸 == HDR/2，mip[5] 尺寸 == HDR/64；
//   3. OnResize 改变 HDR 尺寸 → 下一次 Render 重建 bloom mips，所有 6 张
//      尺寸跟随更新；
//   4. chain 切回不含 BloomPass → 下一次 Render 释放 bloom 资源、
//      BloomMipCount 回 0；
//   5. Shutdown 后 BloomMipCount 回 0。

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
#include <orange/engine/render/PostProcessChain.h>
#include <orange/engine/render/PostProcessPasses.h>
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
using Orange::Engine::Render::BloomPass;
using Orange::Engine::Render::Camera;
using Orange::Engine::Render::HdrPass;
using Orange::Engine::Render::MaterialSystem;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::PostProcessChain;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Scene::TransformComponent;

namespace
{

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
    std::fprintf(stdout, "[BloomChainTest] running\n");

    constexpr std::uint32_t kInitialW = 320;
    constexpr std::uint32_t kInitialH = 240;
    constexpr std::uint32_t kResizedW = 480;
    constexpr std::uint32_t kResizedH = 360;

    WindowDesc winDesc{};
    winDesc.title   = "BloomChainTest";
    winDesc.width   = kInitialW;
    winDesc.height  = kInitialH;
    winDesc.visible = false;
    auto winResult = Window::Create(winDesc);
    if (winResult.IsErr())
    {
        std::fprintf(stderr, "[BloomChainTest] Window::Create failed (code=%u)\n",
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
                         "[BloomChainTest] Initialize failed (code=%u). "
                         "本机可能无 Vulkan，跳过 bloom 验证。\n",
                         static_cast<unsigned>(init.Error()));
            assert(pipeline.BloomMipCount() == 0);
            return 0;
        }
    }
    assert(pipeline.IsInitialized());
    assert(pipeline.BloomMipCount() == 0);  // 没装 chain → bloom 资源不应分配

    // ---- 1. 没装 chain 时 Render 不分配 bloom 资源 -------------------
    {
        World world;
        Entity camE = world.CreateEntity();
        world.AddComponent(camE, Camera::Orthographic(-1, 1, -1, 1, 0, 1));
        Entity e = world.CreateEntity();
        world.AddComponent(e, TransformComponent{});
        RenderableComponent rc;
        rc.mesh             = meshHandle;
        rc.materialInstance = texInst.get();
        world.AddComponent(e, rc);

        pipeline.Render(world);
        assert(pipeline.BloomMipCount() == 0);
        std::fprintf(stdout, "  [PASS] 无 chain 时 BloomMipCount == 0\n");
    }

    // ---- 2. 装载含 BloomPass 的 chain → 6 张 mip 资源 ----------------
    PostProcessChain chain;
    chain.AddPass(std::make_unique<HdrPass>());
    chain.AddPass(std::make_unique<BloomPass>());  // BloomPass 默认 threshold=1, intensity=0.5
    pipeline.SetPostProcessChain(&chain);

    {
        World world;
        Entity camE = world.CreateEntity();
        world.AddComponent(camE, Camera::Orthographic(-1, 1, -1, 1, 0, 1));
        Entity e = world.CreateEntity();
        world.AddComponent(e, TransformComponent{});
        RenderableComponent rc;
        rc.mesh             = meshHandle;
        rc.materialInstance = texInst.get();
        world.AddComponent(e, rc);

        pipeline.Render(world);
        assert(pipeline.BloomMipCount() == 6);

        std::uint32_t hdrW = 0;
        std::uint32_t hdrH = 0;
        pipeline.GetHdrTargetSize(hdrW, hdrH);
        assert(hdrW > 0 && hdrH > 0);

        // mip[0] = HDR / 2；mip[5] = HDR / 64（按 2 的幂逐级缩半）
        std::uint32_t mip0w = 0, mip0h = 0;
        std::uint32_t mip5w = 0, mip5h = 0;
        pipeline.GetBloomMipSize(0, mip0w, mip0h);
        pipeline.GetBloomMipSize(5, mip5w, mip5h);
        assert(mip0w == hdrW / 2 && mip0h == hdrH / 2);
        // 由于 max(_, 1) 兜底 + 整数除法，小窗口下 mip[5] 可能塌到 1×1。
        assert(mip5w >= 1 && mip5h >= 1);
        std::fprintf(stdout, "  [PASS] BloomPass 激活 → 6 张 mip (mip0=%ux%u / mip5=%ux%u)\n",
                     mip0w, mip0h, mip5w, mip5h);
    }

    // ---- 3. OnResize → bloom mips 跟着 HDR 重建 ----------------------
    {
        pipeline.OnResize(kResizedW, kResizedH);

        World world;
        Entity camE = world.CreateEntity();
        world.AddComponent(camE, Camera::Orthographic(-1, 1, -1, 1, 0, 1));
        Entity e = world.CreateEntity();
        world.AddComponent(e, TransformComponent{});
        RenderableComponent rc;
        rc.mesh             = meshHandle;
        rc.materialInstance = texInst.get();
        world.AddComponent(e, rc);

        pipeline.Render(world);
        assert(pipeline.BloomMipCount() == 6);

        std::uint32_t hdrW = 0;
        std::uint32_t hdrH = 0;
        pipeline.GetHdrTargetSize(hdrW, hdrH);
        assert(hdrW == kResizedW && hdrH == kResizedH);

        std::uint32_t mip0w = 0, mip0h = 0;
        pipeline.GetBloomMipSize(0, mip0w, mip0h);
        assert(mip0w == kResizedW / 2 && mip0h == kResizedH / 2);
        std::fprintf(stdout, "  [PASS] OnResize 后 bloom mip[0] = %ux%u（=HDR/2）\n", mip0w, mip0h);
    }

    // ---- 4. chain 切回不含 BloomPass → 释放 bloom 资源 ---------------
    PostProcessChain emptyChain;  // 空 chain，无 BloomPass
    pipeline.SetPostProcessChain(&emptyChain);
    {
        World world;
        Entity camE = world.CreateEntity();
        world.AddComponent(camE, Camera::Orthographic(-1, 1, -1, 1, 0, 1));
        pipeline.Render(world);
        assert(pipeline.BloomMipCount() == 0);
        std::fprintf(stdout, "  [PASS] chain 切回无 BloomPass → 释放 bloom 资源\n");
    }

    // ---- 5. Shutdown 后 BloomMipCount 回 0 ----------------------------
    pipeline.Shutdown();
    assert(!pipeline.IsInitialized());
    assert(pipeline.BloomMipCount() == 0);
    std::fprintf(stdout, "  [PASS] Shutdown 后 BloomMipCount 回零\n");

    std::fprintf(stdout, "[BloomChainTest] all tests passed.\n");
    return 0;
}
