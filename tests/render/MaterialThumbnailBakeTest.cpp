// 材质球缩略图烘焙验收（GAP-2026-05-22 G2 的 headless 核心）。
//
// OrangeEditor 的 ThumbnailService 把 .material 应用到内置 sphere mesh、用
// viewport 的 Pipeline 调 RenderToTexture 渲到一张 96×96 RT 供 ImGui 采样。
// 本测试**不**经 ImGui（CI 友好，无窗口 / 无 descriptor pool），只锁住缩略图
// 的渲染内核：把"sphere + dir-light + 亮色 PBR 材质"的 world 经
// Pipeline::RenderToTexture 渲到 96×96 BGRA8 外部 RT，readback 中心像素断言
// 非全黑——证明材质球真被渲进了缩略图 RT（而非空 RT）。
//
// 与 PipelineRenderToTextureTest 的区别：那条验 RenderToTexture 公共面（quad +
// 非法入参 + 不破坏 viewport）；本条专验"缩略图实际用的 sphere mesh + 96×96
// 尺寸 + PBR 材质"这条 ThumbnailService::BakeThumbnail 的渲染配方能出像素。
//
// sphere mesh 在测试里自建（复刻 BuiltinAssets.cpp MakeSphereMesh 的 lat/lon
// UV-sphere + ComputeSmoothNormalsFromTriangles）——OrangeEditor 的
// MakeSphereMesh 不在引擎公共面，且其 TU 会拖进 EditorHost / ImGui / Vulkan
// 依赖，不适合 headless 测试链接。
//
// Vulkan 不可用时 InitializeOffscreen / RenderDevice::Create 失败 → 打印跳过
// + return 0（与既有 pipeline_*_test 惯例一致，不阻塞无 GPU 的 CI）。

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

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cassert>
#include <cmath>
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

    // lat/lon UV-sphere —— 复刻 BuiltinAssets.cpp MakeSphereMesh 的共享顶点构造，
    // 配 ComputeSmoothNormalsFromTriangles 得到指向球外的平滑法线。与 sample
    // 13_pbr_direct / 14_pbr_ibl + ThumbnailService 的缩略图 sphere 同款。
    std::unique_ptr<MeshAsset>
    MakeSphereMesh(float radius, std::uint32_t lon, std::uint32_t lat)
    {
        std::vector<VertexPosition3> positions;
        std::vector<VertexUV2>       uvs;
        std::vector<std::uint32_t>   indices;
        const float                  kPi = 3.14159265358979323846f;
        for (std::uint32_t i = 0; i <= lat; ++i)
        {
            const float v     = static_cast<float>(i) / static_cast<float>(lat);
            const float theta = v * kPi;
            const float sinT  = std::sin(theta);
            const float cosT  = std::cos(theta);
            for (std::uint32_t j = 0; j <= lon; ++j)
            {
                const float u    = static_cast<float>(j) / static_cast<float>(lon);
                const float phi  = u * 2.0f * kPi;
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
                const std::uint32_t a = i * (lon + 1) + j;
                const std::uint32_t b = (i + 1) * (lon + 1) + j;
                const std::uint32_t c = (i + 1) * (lon + 1) + (j + 1);
                const std::uint32_t d = i * (lon + 1) + (j + 1);
                indices.push_back(a);
                indices.push_back(c);
                indices.push_back(b);
                indices.push_back(a);
                indices.push_back(d);
                indices.push_back(c);
            }
        }
        auto pMesh = std::make_unique<MeshAsset>(std::move(positions),
                                                 std::move(uvs),
                                                 std::move(indices));
        pMesh->ComputeSmoothNormalsFromTriangles();
        return pMesh;
    }

    // 构造 ThumbnailService::BakeThumbnail 的等价 world：相机正对球心 + 侧向方向
    // 光 + 亮色 PBR sphere。侧光方向与 ThumbnailService 一致（左上前方）。
    void BuildThumbnailWorld(World& world, AssetHandle<MeshAsset> mesh,
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
        lt.rotation =
            MakeDirectionalLightRotationFromDir(glm::vec3(-0.4f, -0.5f, -1.0f));
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
    // ShaderReadOnly（RenderToTexture 成功后的状态）。仿 PipelineRenderToTextureTest
    // 的 ReadbackCenter（用 device 的 RHI 直接录一条 transient cmd）。
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
    std::fprintf(stdout, "[MaterialThumbnailBakeTest] running\n");

    // ThumbnailService 的实际缩略图尺寸（96×96）。viewport 用 256（与
    // PipelineRenderToTextureTest 同款给 Pipeline 一个有意义的初始 offscreen）。
    constexpr std::uint32_t kViewportW = 256;
    constexpr std::uint32_t kViewportH = 256;
    constexpr std::uint32_t kThumbW    = 96;
    constexpr std::uint32_t kThumbH    = 96;

    Orange::Renderer::RenderDeviceDesc deviceDesc{};
    deviceDesc.mBackend          = Orange::Renderer::BackendType::Default;
    deviceDesc.mEnableValidation = true;
    auto pDevice                 = Orange::Renderer::RenderDevice::Create(deviceDesc);
    if (!pDevice)
    {
        std::fprintf(stderr,
                     "[MaterialThumbnailBakeTest] RenderDevice::Create 失败 —— 本机可能无 "
                     "Vulkan，跳过\n");
        return 0;
    }

    AssetRegistry assets;
    {
        auto reg = assets.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
        assert(reg.IsOk());
    }
    // 32×16 sphere（与 BuiltinAssets InitializeEditorAssets 的 sphere.mesh 同档）。
    auto meshRes = assets.Insert<MeshAsset>("test/thumb_sphere",
                                            MakeSphereMesh(0.5f, 32u, 16u));
    assert(meshRes.IsOk());
    AssetHandle<MeshAsset> mesh = meshRes.Value();

    MaterialSystem matSys(assets);
    {
        auto rb = matSys.RegisterBuiltins();
        assert(rb.IsOk());
    }
    // 亮色 metallic=0 PBR：保证中心像素被打亮（非黑）。与 ThumbnailService 走
    // EnsureMaterialInstance 拿到的真实 .material 实例同款 template（"pbr"）。
    auto pbrInst = matSys.CreateInstance("pbr");
    assert(pbrInst);
    pbrInst->SetUniform("uBaseColor", glm::vec4(0.85f, 0.55f, 0.25f, 1.0f));
    pbrInst->SetUniform("uMRA", glm::vec4(0.0f, 0.5f, 1.0f, 0.0f)); // metallic=0 rough=0.5 ao=1

    Pipeline pipeline;
    {
        auto init = pipeline.InitializeOffscreen(*pDevice, assets, kViewportW, kViewportH);
        if (init.IsErr())
        {
            std::fprintf(stderr,
                         "[MaterialThumbnailBakeTest] InitializeOffscreen failed (code=%u). "
                         "可能 SPV 文件不在 CWD，跳过后续验证。\n",
                         static_cast<unsigned>(init.Error()));
            return 0;
        }
    }
    assert(pipeline.IsInitialized());

    // dummy IBL ambient：与编辑器 viewport Pipeline 一致地给基础环境光，让
    // sphere 背光面也亮（ThumbnailService 不自设 IBL，复用 viewport 的这份）。
    pipeline.SetDummyIblAmbient(0.5f, 0.5f, 0.5f);

    // 96×96 BGRA8 target —— 缩略图 RT 等价（usage 含 TransferSrc 仅为 readback；
    // ThumbnailService 真实路径不加 TransferSrc，直接 ImGui 采样）。
    Orange::Rhi::TextureDesc td{};
    td.mWidth   = kThumbW;
    td.mHeight  = kThumbH;
    td.mFormat  = Orange::Rhi::TextureFormat::BGRA8Unorm;
    td.mUsage   = Orange::Rhi::TextureUsage::RenderTarget | Orange::Rhi::TextureUsage::Sampled | Orange::Rhi::TextureUsage::TransferSrc;
    auto target = pDevice->GetRhiDevice().CreateTexture(td);
    assert(target);

    // ---- 核心：RenderToTexture(材质球 world) → Ok + 中心像素非黑 ----------
    {
        World world;
        BuildThumbnailWorld(world, mesh, pbrInst.get());
        auto r = pipeline.RenderToTexture(world, target.get(), kThumbW, kThumbH);
        assert(r.IsOk());
        std::fprintf(stdout, "  [PASS] RenderToTexture(sphere+pbr → 96×96) 返回 Ok\n");

        float       px[4]  = {0, 0, 0, 0};
        const bool  readOk = ReadbackCenter(*pDevice, *target, kThumbW, kThumbH, px);
        const float lum    = px[0] + px[1] + px[2];
        std::fprintf(stderr,
                     "  [readback] 缩略图中心 RGBA=(%.3f,%.3f,%.3f,%.3f) lum=%.3f ok=%d\n",
                     px[0], px[1], px[2], px[3], lum, readOk ? 1 : 0);
        assert(readOk && "缩略图 target readback 失败");
        // 中心像素是球心（正对相机最亮处），非全黑 = 材质球真被渲进缩略图 RT。
        assert(lum > 0.05f && "缩略图中心像素全黑 —— 材质球未真正渲染到缩略图 target");
        std::fprintf(stdout,
                     "  [PASS] 缩略图中心像素非黑（材质球渲染验证 lum=%.3f）\n", lum);
    }

    // ---- 第二次烘焙（scratch 复用路径）仍 Ok + 非黑 ---------------------
    // ThumbnailService 复用同一 scratch world 只换 material；这里用第二个
    // 实例（不同 baseColor）验"复用 RT + 重渲不同材质"也出非空像素。
    {
        auto pbrInst2 = matSys.CreateInstance("pbr");
        assert(pbrInst2);
        pbrInst2->SetUniform("uBaseColor", glm::vec4(0.2f, 0.7f, 0.9f, 1.0f));
        pbrInst2->SetUniform("uMRA", glm::vec4(0.0f, 0.4f, 1.0f, 0.0f));

        World world;
        BuildThumbnailWorld(world, mesh, pbrInst2.get());
        auto r = pipeline.RenderToTexture(world, target.get(), kThumbW, kThumbH);
        assert(r.IsOk());

        float       px[4]  = {0, 0, 0, 0};
        const bool  readOk = ReadbackCenter(*pDevice, *target, kThumbW, kThumbH, px);
        const float lum    = px[0] + px[1] + px[2];
        assert(readOk && lum > 0.05f);
        std::fprintf(stdout,
                     "  [PASS] 第二次烘焙（RT 复用 + 换材质）仍非黑 lum=%.3f\n", lum);
    }

    pipeline.Shutdown();
    assert(!pipeline.IsInitialized());
    target.reset();
    if (Orange::Failed(pDevice->WaitIdle()))
    {
        std::fprintf(stderr, "[MaterialThumbnailBakeTest] Shutdown 后 WaitIdle 失败\n");
        return 1;
    }

    std::fprintf(stdout, "[MaterialThumbnailBakeTest] all tests passed.\n");
    return 0;
}
