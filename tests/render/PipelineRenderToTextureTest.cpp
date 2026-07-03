// Pipeline::RenderToTexture 验收（GAP-2026-05-24-pipeline-cannot-render-to-
// arbitrary-rt 的 G1）。
//
// 锁住"用当前 Pipeline 已有资源把任意 world 渲染到调用方提供的外部 RT"这条
// 公共面，覆盖：
//   1. 非法入参防御：未 Initialize / window 模式假设 / target==nullptr /
//      width|height==0 → 返回 Err（这里只能测 offscreen + nullptr + 0 尺寸；
//      未 Initialize 路径用一个独立 Pipeline 实例覆盖）。
//   2. 离屏 Pipeline + 外部 BGRA8 target（64×64，usage 含 TransferSrc 以便
//      readback）→ RenderToTexture(world) 返回 Ok。
//   3. 像素验证（核心）：把外部 target 中心像素读回，断言非全黑——证明真把
//      场景渲进了外部 RT，而非空 RT。
//   4. 不破坏 viewport：RenderToTexture 之后再 Render(viewportWorld) +
//      DebugReadbackPixel，viewportColor 仍非空、尺寸仍是 InitializeOffscreen
//      的尺寸、像素仍正常（证明 scratch 与 primary viewport 互不干扰）。
//
// Vulkan 不可用时 InitializeOffscreen 失败 → 打印跳过 + return 0（与既有
// pipeline_*_test 惯例一致，不阻塞无 GPU 的 CI）。

#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <orange/renderer/RenderDevice.h>
#include <orange/rhi/RHIBuffer.h>
#include <orange/rhi/RHICommandList.h>
#include <orange/rhi/RHIDevice.h>
#include <orange/rhi/RHITexture.h>
#include <orange/rhi/RHITypes.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

using Orange::Engine::Entity;
using Orange::Engine::ResultCode;
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
using Orange::Engine::Render::MakeDirectionalLightRotationFromDir;
using Orange::Engine::Render::MaterialInstance;
using Orange::Engine::Render::MaterialSystem;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Scene::TransformComponent;

namespace
{

    // 正面朝相机（z+）的全屏 quad；CCW 正面（对齐 Pipeline FrontFace::CCW），
    // 带法线供 PBR 着色。
    std::unique_ptr<MeshAsset> MakeQuadMesh()
    {
        std::vector<VertexPosition3> pos = {
            {-0.9f, -0.9f, 0.0f},
            {0.9f, -0.9f, 0.0f},
            {0.9f, 0.9f, 0.0f},
            {-0.9f, 0.9f, 0.0f},
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
        std::vector<std::uint32_t> idx = {0, 1, 2, 0, 2, 3};
        return std::make_unique<MeshAsset>(std::move(pos), std::move(uv),
                                           std::move(nrm), std::move(idx));
    }

    // 构造一个"相机 + 方向光 + 亮色 PBR quad"的 world，保证中心像素被打亮。
    void BuildLitQuadWorld(World& world, AssetHandle<MeshAsset> mesh,
                           MaterialInstance* inst)
    {
        Entity camE = world.CreateEntity();
        Camera cam  = Camera::Perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
        cam.view    = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f),
                                  glm::vec3(0.0f, 0.0f, 0.0f),
                                  glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent(camE, cam);

        Entity             lightE = world.CreateEntity();
        TransformComponent lt{};
        lt.rotation = MakeDirectionalLightRotationFromDir(glm::vec3(0.0f, 0.0f, -1.0f));
        world.AddComponent(lightE, lt);
        world.AddComponent(lightE, DirectionalLight{});

        Entity e = world.CreateEntity();
        world.AddComponent(e, TransformComponent{});
        RenderableComponent rc;
        rc.mesh             = mesh;
        rc.materialInstance = inst;
        world.AddComponent(e, rc);
    }

    // 把外部 BGRA8 target 中心像素读回成归一化 RGBA（[0,1]）。target 当前应处于
    // ShaderReadOnly（RenderToTexture 成功后的状态）。仿 Pipeline::DebugReadbackPixel，
    // 但作用于调用方持有的外部 RT（用 device 的 RHI 直接录一条 transient cmd）。
    bool ReadbackCenter(Orange::Renderer::RenderDevice& device,
                        Orange::Rhi::RHITexture&        target,
                        std::uint32_t width, std::uint32_t height,
                        float outRGBA[4])
    {
        auto&               rhi = device.GetRhiDevice();
        const std::uint64_t bytes =
            static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4u;

        Orange::Rhi::BufferDesc bd{};
        bd.mSize        = bytes;
        bd.mUsage       = Orange::Rhi::BufferUsage::Transfer;
        bd.mMemoryUsage = Orange::Rhi::MemoryUsage::GpuToCpu;
        auto readback   = rhi.CreateBuffer(bd);
        if (!readback)
        {
            return false;
        }

        auto cmd = rhi.CreateCommandList(Orange::Rhi::CommandQueueType::Graphics);
        if (!cmd || cmd->Begin() != Orange::ResultCode::Success)
        {
            return false;
        }
        cmd->TransitionTexture(target, Orange::Rhi::TextureLayout::ShaderReadOnly,
                               Orange::Rhi::TextureLayout::TransferSrc);
        {
            Orange::Rhi::BufferTextureCopyRegion r{};
            r.mBufferOffset = 0;
            r.mMipLevel     = 0;
            r.mArrayLayer   = 0;
            r.mWidth        = width;
            r.mHeight       = height;
            r.mDepth        = 1;
            cmd->CopyTextureToBuffer(target, *readback, r);
        }
        cmd->TransitionTexture(target, Orange::Rhi::TextureLayout::TransferSrc,
                               Orange::Rhi::TextureLayout::ShaderReadOnly);
        if (cmd->End() != Orange::ResultCode::Success || rhi.SubmitCommandList(*cmd) != Orange::ResultCode::Success)
        {
            return false;
        }
        device.WaitIdle();

        const void* mapped = readback->Map();
        if (mapped == nullptr)
        {
            return false;
        }
        const auto*         p   = static_cast<const std::uint8_t*>(mapped);
        const std::uint64_t cx  = width / 2u;
        const std::uint64_t cy  = height / 2u;
        const std::uint64_t idx = (cy * width + cx) * 4u;
        // BGRA8Unorm → 字节序 B, G, R, A。
        outRGBA[2] = p[idx + 0] / 255.0f; // B
        outRGBA[1] = p[idx + 1] / 255.0f; // G
        outRGBA[0] = p[idx + 2] / 255.0f; // R
        outRGBA[3] = p[idx + 3] / 255.0f; // A
        readback->Unmap();
        return true;
    }

} // namespace

int main()
{
    std::fprintf(stdout, "[PipelineRenderToTextureTest] running\n");

    constexpr std::uint32_t kViewportW = 256;
    constexpr std::uint32_t kViewportH = 256;
    constexpr std::uint32_t kRttW      = 64;
    constexpr std::uint32_t kRttH      = 64;

    // ---- 1a. 未 Initialize 的 Pipeline → RenderToTexture 返回 NotInitialized
    {
        Pipeline pipeline;
        World    world;
        auto     r = pipeline.RenderToTexture(world, nullptr, kRttW, kRttH);
        assert(r.IsErr());
        assert(r.Error() == ResultCode::NotInitialized);
        std::fprintf(stdout, "  [PASS] 未 Initialize → NotInitialized\n");
    }

    Orange::Renderer::RenderDeviceDesc deviceDesc{};
    deviceDesc.mBackend          = Orange::Renderer::BackendType::Default;
    deviceDesc.mEnableValidation = true;
    auto pDevice                 = Orange::Renderer::RenderDevice::Create(deviceDesc);
    if (!pDevice)
    {
        std::fprintf(stderr,
                     "[PipelineRenderToTextureTest] RenderDevice::Create 失败 —— 本机可能无 "
                     "Vulkan，跳过\n");
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
        auto rb = matSys.RegisterBuiltins();
        assert(rb.IsOk());
    }
    // 亮色 metallic=0 PBR：保证中心像素被打亮（非黑）。
    auto pbrInst = matSys.CreateInstance("pbr");
    assert(pbrInst);
    pbrInst->SetUniform("uBaseColor", glm::vec4(0.9f, 0.4f, 0.2f, 1.0f));
    pbrInst->SetUniform("uMRA", glm::vec4(0.0f, 0.5f, 1.0f, 0.0f)); // metallic=0 rough=0.5 ao=1

    Pipeline pipeline;
    {
        auto init = pipeline.InitializeOffscreen(*pDevice, assets, kViewportW, kViewportH);
        if (init.IsErr())
        {
            std::fprintf(stderr,
                         "[PipelineRenderToTextureTest] InitializeOffscreen failed (code=%u). "
                         "可能 SPV 文件不在 CWD，跳过后续验证。\n",
                         static_cast<unsigned>(init.Error()));
            return 0;
        }
    }
    assert(pipeline.IsInitialized());

    // dummy IBL ambient：与编辑器缩略图路径一致地给基础环境光，保证背光面也亮。
    pipeline.SetDummyIblAmbient(0.5f, 0.5f, 0.5f);

    // ---- 1b. 非法入参防御（已 Initialize 的离屏 Pipeline）----------------
    {
        World world;
        BuildLitQuadWorld(world, mesh, pbrInst.get());
        // target==nullptr
        {
            auto r = pipeline.RenderToTexture(world, nullptr, kRttW, kRttH);
            assert(r.IsErr() && r.Error() == ResultCode::InvalidArgument);
        }
        std::fprintf(stdout, "  [PASS] target==nullptr → InvalidArgument\n");
    }

    // ---- 2. 外部 BGRA8 target（usage 含 TransferSrc 以便 readback）-------
    Orange::Rhi::TextureDesc td{};
    td.mWidth   = kRttW;
    td.mHeight  = kRttH;
    td.mFormat  = Orange::Rhi::TextureFormat::BGRA8Unorm; // == kSwapchainColorFormat
    td.mUsage   = Orange::Rhi::TextureUsage::RenderTarget | Orange::Rhi::TextureUsage::Sampled | Orange::Rhi::TextureUsage::TransferSrc;
    auto target = pDevice->GetRhiDevice().CreateTexture(td);
    assert(target);

    // 0 尺寸入参防御。
    {
        World world;
        BuildLitQuadWorld(world, mesh, pbrInst.get());
        auto r0 = pipeline.RenderToTexture(world, target.get(), 0, kRttH);
        assert(r0.IsErr() && r0.Error() == ResultCode::InvalidArgument);
        std::fprintf(stdout, "  [PASS] width==0 → InvalidArgument\n");
    }

    // ---- 3. RenderToTexture(亮色 PBR quad) → Ok + 中心像素非黑 ----------
    {
        World world;
        BuildLitQuadWorld(world, mesh, pbrInst.get());
        auto r = pipeline.RenderToTexture(world, target.get(), kRttW, kRttH);
        assert(r.IsOk());
        std::fprintf(stdout, "  [PASS] RenderToTexture 返回 Ok\n");

        float       px[4]  = {0, 0, 0, 0};
        const bool  readOk = ReadbackCenter(*pDevice, *target, kRttW, kRttH, px);
        const float lum    = px[0] + px[1] + px[2];
        std::fprintf(stderr,
                     "  [readback] 外部 target 中心 RGBA=(%.3f,%.3f,%.3f,%.3f) lum=%.3f ok=%d\n",
                     px[0], px[1], px[2], px[3], lum, readOk ? 1 : 0);
        assert(readOk && "外部 target readback 失败");
        // 中心像素非全黑：证明 world 真被渲进外部 RT（空 RT 会是黑/Undefined）。
        assert(lum > 0.05f && "RenderToTexture 中心像素全黑 —— 未真正渲染到外部 target");
        std::fprintf(stdout, "  [PASS] 外部 target 中心像素非黑（真渲染验证 lum=%.3f）\n", lum);
    }

    // ---- 4. 不破坏 viewport：再 Render(viewportWorld) 仍正常 ------------
    {
        World world;
        BuildLitQuadWorld(world, mesh, pbrInst.get());
        pipeline.Render(world);

        const auto* vp = pipeline.GetOffscreenColor();
        assert(vp != nullptr);

        std::uint32_t hdrW = 0;
        std::uint32_t hdrH = 0;
        pipeline.GetHdrTargetSize(hdrW, hdrH);
        assert(hdrW == kViewportW && hdrH == kViewportH); // viewport 尺寸未被 scratch 顶掉

        float       px[4] = {0, 0, 0, 0};
        const bool  ok    = pipeline.DebugReadbackPixel(kViewportW / 2u, kViewportH / 2u, px);
        const float lum   = px[0] + px[1] + px[2];
        std::fprintf(stderr, "  [viewport] 中心 RGBA=(%.3f,%.3f,%.3f,%.3f) lum=%.3f ok=%d\n",
                     px[0], px[1], px[2], px[3], lum, ok ? 1 : 0);
        assert(ok && "viewport DebugReadbackPixel 失败");
        assert(lum > 0.05f && "RenderToTexture 之后 viewport 渲染异常（被 scratch 污染）");
        std::fprintf(stdout,
                     "  [PASS] RenderToTexture 后 viewport 仍正常（尺寸=%ux%u lum=%.3f）\n",
                     hdrW, hdrH, lum);
    }

    // ---- 5. 第二次 RenderToTexture（缓存命中路径）仍 Ok + 非黑 ----------
    {
        World world;
        BuildLitQuadWorld(world, mesh, pbrInst.get());
        auto r = pipeline.RenderToTexture(world, target.get(), kRttW, kRttH);
        assert(r.IsOk());

        float       px[4]  = {0, 0, 0, 0};
        const bool  readOk = ReadbackCenter(*pDevice, *target, kRttW, kRttH, px);
        const float lum    = px[0] + px[1] + px[2];
        assert(readOk && lum > 0.05f);
        std::fprintf(stdout, "  [PASS] 第二次 RenderToTexture（scratch 复用）仍非黑 lum=%.3f\n", lum);
    }

    pipeline.Shutdown();
    assert(!pipeline.IsInitialized());
    target.reset();
    if (Orange::Failed(pDevice->WaitIdle()))
    {
        std::fprintf(stderr, "[PipelineRenderToTextureTest] Shutdown 后 WaitIdle 失败\n");
        return 1;
    }

    std::fprintf(stdout, "[PipelineRenderToTextureTest] all tests passed.\n");
    return 0;
}
