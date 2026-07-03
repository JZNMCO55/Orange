// 视觉 golden 回归（自动化测试基建 ② 像素 golden）。
//
// 锁住"一个程序化构造的确定 PBR 场景（相机 + 方向光 + 亮色 quad），经
// Pipeline::RenderToTexture 渲到外部 RT，逐像素与提交的参考图比对"。这是把
// 引擎渲染输出纳入回归网的第一道视觉断言，配合 capture_viewport 路径补足
// "ctest 全绿但视觉全黑/错色"的盲区。
//
// 关键确定性选择：
//   * 走 RenderToTexture（PBR 直出，postProcessChain=nullptr）——不经 TAA /
//     bloom / dither 等任何 temporal / 随机后处理，逐像素天然可复现；
//   * 场景全程序化构造（零磁盘资产、零时间/随机输入），固定 256×256；
//   * SetDummyIblAmbient 给常量环境光（非真 IBL prefilter bake）。
// 跨 GPU / 驱动仍可能超容差（PPM 像素与本机 GPU 绑定）——参考图按本机/同 GPU
// 视觉回归网定位，重生成命令见 tests/golden/README.md。
//
// Vulkan 不可用时 RenderDevice::Create / InitializeOffscreen 失败 → 打印跳过 +
// return 0（与既有 pipeline_*_test 惯例一致，不阻塞无 GPU 的 CI；此时也生不出
// golden，符合"只有有 Vulkan 的机器能生成/校验参考图"）。
//
// 用法：visual_golden_pbr_test <goldenDir> [--update-golden] [--tolerance N] ...
//   goldenDir 由 ctest 注册时作为 argv[1] 传入（= tests/golden/data）。

#include "golden/GoldenTestHarness.h"

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

#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

    // 构造一个"相机 + 方向光 + 亮色 PBR quad"的确定 world。
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

    // 把外部 BGRA8 target 整张读回成紧凑 RGBA8（交换 B/R 通道）。target 当前应处于
    // ShaderReadOnly（RenderToTexture 成功后的状态）。仿 PipelineRenderToTextureTest
    // 的 ReadbackCenter，但读整张而非中心像素，并做 BGRA→RGBA 以喂 golden harness
    // 的 MakeRgbFromRgba（它取前 3 字节当 RGB）。
    bool ReadbackFullRgba(Orange::Renderer::RenderDevice& device,
                          Orange::Rhi::RHITexture&        target,
                          std::uint32_t width, std::uint32_t height,
                          std::vector<std::uint8_t>& outRgba)
    {
        auto&               rhi = device.GetRhiDevice();
        const std::uint64_t pixelCount =
            static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
        const std::uint64_t bytes = pixelCount * 4u;

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
        const auto* p = static_cast<const std::uint8_t*>(mapped);
        outRgba.resize(static_cast<std::size_t>(bytes));
        for (std::uint64_t i = 0; i < pixelCount; ++i)
        {
            // 源 BGRA8 字节序 B,G,R,A → 目标紧凑 RGBA8。
            outRgba[i * 4 + 0] = p[i * 4 + 2]; // R
            outRgba[i * 4 + 1] = p[i * 4 + 1]; // G
            outRgba[i * 4 + 2] = p[i * 4 + 0]; // B
            outRgba[i * 4 + 3] = p[i * 4 + 3]; // A
        }
        readback->Unmap();
        return true;
    }

} // namespace

int main(int argc, char** argv)
{
    std::fprintf(stdout, "[VisualGoldenPbrTest] running\n");

    if (argc < 2)
    {
        std::fprintf(stderr,
                     "[VisualGoldenPbrTest] 用法: %s <goldenDir> [--update-golden] ...\n",
                     argc > 0 ? argv[0] : "visual_golden_pbr_test");
        return 2;
    }
    const std::string goldenDir = argv[1];

    constexpr std::uint32_t kW = 256;
    constexpr std::uint32_t kH = 256;

    Orange::Renderer::RenderDeviceDesc deviceDesc{};
    deviceDesc.mBackend          = Orange::Renderer::BackendType::Default;
    deviceDesc.mEnableValidation = true;
    auto pDevice                 = Orange::Renderer::RenderDevice::Create(deviceDesc);
    if (!pDevice)
    {
        std::fprintf(stderr,
                     "[VisualGoldenPbrTest] RenderDevice::Create 失败 —— 本机可能无 "
                     "Vulkan，跳过（golden 无法生成/校验）\n");
        return 0;
    }

    AssetRegistry assets;
    {
        auto reg = assets.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
        if (reg.IsErr())
        {
            std::fprintf(stderr, "[VisualGoldenPbrTest] 注册 ShaderLoader 失败\n");
            return 1;
        }
    }
    auto meshRes = assets.Insert<MeshAsset>("test/golden_quad", MakeQuadMesh());
    if (meshRes.IsErr())
    {
        std::fprintf(stderr, "[VisualGoldenPbrTest] 插入 quad mesh 失败\n");
        return 1;
    }
    AssetHandle<MeshAsset> mesh = meshRes.Value();

    MaterialSystem matSys(assets);
    {
        auto rb = matSys.RegisterBuiltins();
        if (rb.IsErr())
        {
            std::fprintf(stderr, "[VisualGoldenPbrTest] RegisterBuiltins 失败\n");
            return 1;
        }
    }
    // 亮色 metallic=0 PBR：确定的基础色（金属度 0 / 粗糙 0.5 / ao 1）。
    auto pbrInst = matSys.CreateInstance("pbr");
    if (!pbrInst)
    {
        std::fprintf(stderr, "[VisualGoldenPbrTest] CreateInstance(pbr) 失败\n");
        return 1;
    }
    pbrInst->SetUniform("uBaseColor", glm::vec4(0.9f, 0.4f, 0.2f, 1.0f));
    pbrInst->SetUniform("uMRA", glm::vec4(0.0f, 0.5f, 1.0f, 0.0f));

    Pipeline pipeline;
    {
        auto init = pipeline.InitializeOffscreen(*pDevice, assets, kW, kH);
        if (init.IsErr())
        {
            std::fprintf(stderr,
                         "[VisualGoldenPbrTest] InitializeOffscreen 失败 (code=%u)，跳过\n",
                         static_cast<unsigned>(init.Error()));
            return 0;
        }
    }
    pipeline.SetDummyIblAmbient(0.5f, 0.5f, 0.5f);

    // 外部 BGRA8 target（usage 含 TransferSrc 以便 readback）。
    Orange::Rhi::TextureDesc td{};
    td.mWidth   = kW;
    td.mHeight  = kH;
    td.mFormat  = Orange::Rhi::TextureFormat::BGRA8Unorm;
    td.mUsage   = Orange::Rhi::TextureUsage::RenderTarget | Orange::Rhi::TextureUsage::Sampled | Orange::Rhi::TextureUsage::TransferSrc;
    auto target = pDevice->GetRhiDevice().CreateTexture(td);
    if (!target)
    {
        std::fprintf(stderr, "[VisualGoldenPbrTest] CreateTexture 失败\n");
        return 1;
    }

    World world;
    BuildLitQuadWorld(world, mesh, pbrInst.get());
    {
        auto r = pipeline.RenderToTexture(world, target.get(), kW, kH);
        if (r.IsErr())
        {
            std::fprintf(stderr,
                         "[VisualGoldenPbrTest] RenderToTexture 失败 (code=%u)\n",
                         static_cast<unsigned>(r.Error()));
            return 1;
        }
    }

    std::vector<std::uint8_t> rgba;
    if (!ReadbackFullRgba(*pDevice, *target, kW, kH, rgba))
    {
        std::fprintf(stderr, "[VisualGoldenPbrTest] readback 失败\n");
        return 1;
    }

    const int result = Orange::Golden::RunGoldenComparison(
        rgba.data(), kW, kH, "pbr_quad", goldenDir, argc, argv);

    pipeline.Shutdown();
    target.reset();
    pDevice->WaitIdle();
    return result;
}
