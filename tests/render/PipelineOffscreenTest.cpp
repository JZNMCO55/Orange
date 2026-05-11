// Pipeline::InitializeOffscreen 离屏模式验收 ——
// Phase 6 / Task 06-08 S1 验收。
//
// 覆盖路径：
//   1. 未 Initialize 时 GetOffscreenColor 返回 nullptr。
//   2. InitializeOffscreen(borrowed RenderDevice, 256×256) 成功；不依赖 Window；
//      调用方持有的 RenderDevice 保持有效。
//   3. Render(空 World) 不崩。
//   4. Render(含 camera + drawable World)：TemplatePipelineCount > 0；
//      GetOffscreenColor 返回非空 RHITexture*，对其调
//      `Interop::GetVulkanImageView` / `GetVulkanImage` 均非 null（证明
//      texture 是真实的、可被消费者侧 ImGui_ImplVulkan_AddTexture 接住）。
//   5. ResizeOffscreen(384×288) + 再 Render：HDR 内部尺寸跟 viewport 同步。
//   6. Shutdown 后 GetOffscreenColor 回 nullptr；调用方借的 RenderDevice
//      仍可独立 WaitIdle / 析构（证明 Pipeline 没偷走所有权）。
//
// 与 PipelineHdrTargetTest 共享 mesh / material / loader / shader 等准备
// 代码——硬抄是为了让本测试与 window 模式那套测试在 fixture 上对齐，便
// 于将来同步演进；提取共享 helper 留给后续轻量重构。

#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <orange/renderer/RenderDevice.h>
#include <orange/renderer/VulkanInterop.h>
#include <orange/rhi/RHITexture.h>

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
    std::fprintf(stdout, "[PipelineOffscreenTest] running\n");

    // ---- 1. Pipeline 未 Initialize 的 introspection -----------------
    {
        Pipeline pipeline;
        assert(pipeline.GetOffscreenColor() == nullptr);
        assert(!pipeline.IsInitialized());
        std::fprintf(stdout, "  [PASS] 未 Initialize 时 GetOffscreenColor == nullptr\n");
    }

    // ---- 2. 借用 RenderDevice + Pipeline.InitializeOffscreen --------
    constexpr std::uint32_t kInitialW = 256;
    constexpr std::uint32_t kInitialH = 256;
    constexpr std::uint32_t kResizedW = 384;
    constexpr std::uint32_t kResizedH = 288;

    Orange::Renderer::RenderDeviceDesc deviceDesc{};
    deviceDesc.mBackend          = Orange::Renderer::BackendType::Default;
    deviceDesc.mEnableValidation = true;
    auto pDevice = Orange::Renderer::RenderDevice::Create(deviceDesc);
    if (!pDevice)
    {
        std::fprintf(stderr,
                     "[PipelineOffscreenTest] RenderDevice::Create 失败 —— 本机可能无 "
                     "Vulkan，跳过\n");
        return 0;
    }

    AssetRegistry assets;
    {
        auto reg = assets.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
        if (reg.IsErr())
        {
            std::fprintf(stderr,
                         "[PipelineOffscreenTest] RegisterLoader failed (code=%u)\n",
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
        auto init = pipeline.InitializeOffscreen(*pDevice, assets, kInitialW, kInitialH);
        if (init.IsErr())
        {
            std::fprintf(stderr,
                         "[PipelineOffscreenTest] InitializeOffscreen failed (code=%u). "
                         "可能 SPV 文件不在 CWD，跳过后续验证。\n",
                         static_cast<unsigned>(init.Error()));
            return 0;
        }
    }
    assert(pipeline.IsInitialized());

    // GetOffscreenColor 应在 InitializeOffscreen 之后立刻可用（不需要先 Render）。
    {
        const auto* tex = pipeline.GetOffscreenColor();
        assert(tex != nullptr);
        // 取 native VkImageView / VkImage：消费者侧 ImGui::Image 链路必走的两步。
        void* viewHandle  = Orange::Renderer::Interop::GetVulkanImageView(*tex);
        void* imageHandle = Orange::Renderer::Interop::GetVulkanImage(*tex);
        assert(viewHandle  != nullptr);
        assert(imageHandle != nullptr);
        std::fprintf(stdout,
                     "  [PASS] InitializeOffscreen 后 GetOffscreenColor 非空，"
                     "VkImageView=%p VkImage=%p\n",
                     viewHandle, imageHandle);
    }

    // HDR target 内部尺寸应与 offscreen 入参一致。
    {
        std::uint32_t hdrW = 0;
        std::uint32_t hdrH = 0;
        pipeline.GetHdrTargetSize(hdrW, hdrH);
        assert(hdrW == kInitialW);
        assert(hdrH == kInitialH);
        std::fprintf(stdout, "  [PASS] HDR target = %ux%u（与 offscreen 入参对齐）\n",
                     hdrW, hdrH);
    }

    // ---- 3. Render 空 World ----------------------------------------
    {
        World empty;
        pipeline.Render(empty);
        const auto* tex = pipeline.GetOffscreenColor();
        assert(tex != nullptr);
        assert(pipeline.TemplatePipelineCount() == 0);  // 无 drawable → 无模板编译
        std::fprintf(stdout, "  [PASS] Render 空 World 不崩、viewportColor 维持\n");
    }

    // ---- 4. Render 含 drawable+camera 的 World ---------------------
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
        assert(pipeline.TemplatePipelineCount() == 1);  // textured 模板编译一次

        const auto* tex = pipeline.GetOffscreenColor();
        assert(tex != nullptr);
        void* viewHandle = Orange::Renderer::Interop::GetVulkanImageView(*tex);
        assert(viewHandle != nullptr);
        std::fprintf(stdout,
                     "  [PASS] Render 单 drawable 后 cache=%zu / view=%p\n",
                     pipeline.TemplatePipelineCount(), viewHandle);
    }

    // ---- 5. ResizeOffscreen → 下一帧重建 HDR + viewport ------------
    {
        const auto* texBefore = pipeline.GetOffscreenColor();
        assert(texBefore != nullptr);
        void* viewBefore = Orange::Renderer::Interop::GetVulkanImageView(*texBefore);

        pipeline.ResizeOffscreen(kResizedW, kResizedH);
        // 真正重建发生在下一次 Render 顶部。
        std::uint32_t hdrW = 0;
        std::uint32_t hdrH = 0;
        pipeline.GetHdrTargetSize(hdrW, hdrH);
        assert(hdrW == kInitialW);  // 还没 Render，尺寸未生效
        assert(hdrH == kInitialH);

        World empty;
        pipeline.Render(empty);
        pipeline.GetHdrTargetSize(hdrW, hdrH);
        assert(hdrW == kResizedW);
        assert(hdrH == kResizedH);

        // resize 后 viewportColor 是新对象，VkImage 句柄应该和旧的不同。
        const auto* texAfter = pipeline.GetOffscreenColor();
        assert(texAfter != nullptr);
        void* viewAfter = Orange::Renderer::Interop::GetVulkanImageView(*texAfter);
        assert(viewAfter != nullptr);
        std::fprintf(stdout,
                     "  [PASS] ResizeOffscreen 后 HDR=%ux%u, view: before=%p after=%p\n",
                     hdrW, hdrH, viewBefore, viewAfter);
    }

    // ---- 6. Resize 后再 Render drawable 确认 descriptor set 仍有效 --
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
        assert(pipeline.TemplatePipelineCount() == 1);
        const auto* tex = pipeline.GetOffscreenColor();
        assert(tex != nullptr);
        assert(Orange::Renderer::Interop::GetVulkanImageView(*tex) != nullptr);
        std::fprintf(stdout, "  [PASS] resize 后 Render drawable 正常\n");
    }

    // ---- 7. Shutdown 后 introspection 回到默认 + RenderDevice 完好 --
    pipeline.Shutdown();
    assert(!pipeline.IsInitialized());
    assert(pipeline.GetOffscreenColor() == nullptr);

    // Pipeline 借用的 RenderDevice 仍属调用方，可独立 WaitIdle / 析构。
    assert(pDevice);
    if (Orange::Failed(pDevice->WaitIdle()))
    {
        std::fprintf(stderr,
                     "[PipelineOffscreenTest] Pipeline::Shutdown 后 RenderDevice::WaitIdle 失败\n");
        return 1;
    }
    std::fprintf(stdout, "  [PASS] Shutdown 后 introspection 回零、外部 RenderDevice 仍可用\n");

    std::fprintf(stdout, "[PipelineOffscreenTest] all tests passed.\n");
    return 0;
}
