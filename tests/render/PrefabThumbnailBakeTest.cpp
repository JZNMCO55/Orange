// prefab 缩略图烘焙验收（prefab scene snapshot thumbnail 的 headless 核心）。
//
// OrangeEditor 的 ThumbnailService::BakePrefabThumbnail 把 .prefab.json 实例化
// 到一个 scratch world、遍历其 (Transform, Renderable) 算合并 world-space AABB、
// 据此摆相机（3/4 视角 + dist 框满 + padding），再用 viewport 的 Pipeline 调
// RenderToTexture 渲到 96×96 RT 供 ImGui 采样。本测试**不**经 ImGui / PrefabAsset
// / EditorHost（CI 友好，无窗口 / 无 descriptor pool / 不拖 OrangeEditor TU），
// 只锁住 prefab 缩略图的渲染内核：直接建等价的"多个带 PBR 材质的 Renderable
// 实体"world（绕开 PrefabAsset 实例化），复刻 ThumbnailService 里那段 AABB 框
// 相机数学，经 Pipeline::RenderToTexture 渲到 96×96 BGRA8 外部 RT，readback 中心
// 像素断言非全黑——证明 prefab 真被框定 + 渲进了缩略图 RT（而非空 RT / 框偏导致
// 几何全在视锥外）。
//
// 两个 case：
//   1) prefab 居中（多球分布在原点附近）→ AABB 框相机 → 中心非黑。
//   2) prefab 偏离原点（中心 (5,0,0) ± 2）→ AABB 框相机必须跟随平移，否则相机
//      仍看原点、几何全在画面外 → 中心黑。断言非黑 = 框定真的跟随了 AABB。
//
// sphere mesh 在测试里自建（复刻 BuiltinAssets.cpp MakeSphereMesh 的 lat/lon
// UV-sphere + ComputeSmoothNormalsFromTriangles）——与 MaterialThumbnailBakeTest
// 同款，OrangeEditor 的 MakeSphereMesh 不在引擎公共面。
//
// Vulkan 不可用时 InitializeOffscreen / RenderDevice::Create 失败 → 打印跳过
// + return 0（与既有 pipeline_*_test / material_thumbnail_bake_test 惯例一致，
// 不阻塞无 GPU 的 CI）。

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
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
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

// lat/lon UV-sphere —— 复刻 BuiltinAssets.cpp MakeSphereMesh（与
// MaterialThumbnailBakeTest 同款）。
std::unique_ptr<MeshAsset>
MakeSphereMesh(float radius, std::uint32_t lon, std::uint32_t lat)
{
    std::vector<VertexPosition3> positions;
    std::vector<VertexUV2>       uvs;
    std::vector<std::uint32_t>   indices;
    const float kPi = 3.14159265358979323846f;
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
            const std::uint32_t a = i       * (lon + 1) + j;
            const std::uint32_t b = (i + 1) * (lon + 1) + j;
            const std::uint32_t c = (i + 1) * (lon + 1) + (j + 1);
            const std::uint32_t d = i       * (lon + 1) + (j + 1);
            indices.push_back(a); indices.push_back(c); indices.push_back(b);
            indices.push_back(a); indices.push_back(d); indices.push_back(c);
        }
    }
    auto pMesh = std::make_unique<MeshAsset>(std::move(positions),
                                             std::move(uvs),
                                             std::move(indices));
    pMesh->ComputeSmoothNormalsFromTriangles();
    return pMesh;
}

// ---- AABB 框相机数学（复刻 ThumbnailService.cpp 的匿名 namespace helper）-----
struct LocalAABB
{
    glm::vec3 min;
    glm::vec3 max;
};

glm::mat4 ComposeWorldMatrix(const TransformComponent& xform) noexcept
{
    glm::mat4 m = glm::translate(glm::mat4(1.0f), xform.position);
    m *= glm::mat4_cast(xform.rotation);
    m  = glm::scale(m, xform.scale);
    return m;
}

LocalAABB ComputeMeshLocalAABB(const MeshAsset& mesh) noexcept
{
    const auto& positions = mesh.Positions();
    if (positions.empty())
    {
        return LocalAABB{glm::vec3(0.0f), glm::vec3(0.0f)};
    }
    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(std::numeric_limits<float>::lowest());
    for (const auto& p : positions)
    {
        mn.x = std::min(mn.x, p.x);  mx.x = std::max(mx.x, p.x);
        mn.y = std::min(mn.y, p.y);  mx.y = std::max(mx.y, p.y);
        mn.z = std::min(mn.z, p.z);  mx.z = std::max(mx.z, p.z);
    }
    return LocalAABB{mn, mx};
}

LocalAABB TransformAABB(const LocalAABB& local, const glm::mat4& worldMat) noexcept
{
    const glm::vec3 corners[8] = {
        {local.min.x, local.min.y, local.min.z},
        {local.max.x, local.min.y, local.min.z},
        {local.min.x, local.max.y, local.min.z},
        {local.max.x, local.max.y, local.min.z},
        {local.min.x, local.min.y, local.max.z},
        {local.max.x, local.min.y, local.max.z},
        {local.min.x, local.max.y, local.max.z},
        {local.max.x, local.max.y, local.max.z},
    };
    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(std::numeric_limits<float>::lowest());
    for (const auto& c : corners)
    {
        const glm::vec3 w = glm::vec3(worldMat * glm::vec4(c, 1.0f));
        mn = glm::min(mn, w);
        mx = glm::max(mx, w);
    }
    return LocalAABB{mn, mx};
}

// 据 prefab 实例的 (Transform, Renderable) view 合并 world AABB，复刻
// ThumbnailService::BakePrefabThumbnail 的摆相机逻辑，写进 world 的相机 entity。
// 与 BakePrefabThumbnail 的步骤 4 / 5 等价。
void FrameCameraToScene(World& world, AssetRegistry& assets, Entity cameraEntity)
{
    glm::vec3 aabbMin(std::numeric_limits<float>::max());
    glm::vec3 aabbMax(std::numeric_limits<float>::lowest());
    bool      hasGeometry = false;

    auto& reg  = world.Registry();
    auto  view = reg.view<TransformComponent, RenderableComponent>();
    for (auto e : view)
    {
        const auto& xform = view.get<TransformComponent>(e);
        const auto& rc    = view.get<RenderableComponent>(e);
        if (!rc.visible) { continue; }
        const auto* pMesh = assets.Get(rc.mesh);
        if (pMesh == nullptr || pMesh->Empty()) { continue; }

        const LocalAABB localAABB = ComputeMeshLocalAABB(*pMesh);
        const glm::mat4 worldMat  = ComposeWorldMatrix(xform);
        const LocalAABB worldAABB = TransformAABB(localAABB, worldMat);
        aabbMin     = glm::min(aabbMin, worldAABB.min);
        aabbMax     = glm::max(aabbMax, worldAABB.max);
        hasGeometry = true;
    }

    const glm::vec3 dir = glm::normalize(glm::vec3(1.0f, 0.8f, 1.0f));
    constexpr float kFovY = glm::radians(45.0f);

    glm::vec3 center(0.0f);
    glm::vec3 eye(0.0f, 0.0f, 3.0f);
    float     near = 0.1f;
    float     far  = 100.0f;

    const glm::vec3 extent = aabbMax - aabbMin;
    const float     radius = hasGeometry ? glm::length(extent) * 0.5f : 0.0f;
    if (hasGeometry && radius > 1e-4f)
    {
        center = (aabbMin + aabbMax) * 0.5f;
        const float dist = (radius / std::sin(kFovY * 0.5f)) * 1.1f;
        eye  = center + dir * dist;
        near = std::max(0.01f, dist - radius * 2.0f);
        far  = dist + radius * 2.0f;
    }

    auto* cam = world.GetComponent<Camera>(cameraEntity);
    assert(cam != nullptr);
    *cam = Camera::Perspective(kFovY, 1.0f, near, far);
    cam->view = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));
}

// 构造一个"等价 prefab 实例"的 world：相机 entity（姿态由 FrameCameraToScene
// 据 AABB 设）+ 侧向方向光 + 若干带 PBR 材质的 sphere renderable。centerOffset
// 把整组球平移到该位置（验"框定跟随 AABB"用）。
Entity BuildPrefabLikeWorld(World& world, AssetHandle<MeshAsset> mesh,
                            const std::vector<MaterialInstance*>& mats,
                            const std::vector<glm::vec3>&         localOffsets,
                            const glm::vec3&                      centerOffset)
{
    // 相机 entity（占位 Camera，FrameCameraToScene 覆写）。
    Entity camE = world.CreateEntity();
    world.AddComponent(camE, Camera::Perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f));

    // 侧向方向光（与 ThumbnailService 一致）。
    Entity lightE = world.CreateEntity();
    TransformComponent lt{};
    lt.rotation =
        MakeDirectionalLightRotationFromDir(glm::vec3(-0.4f, -0.5f, -1.0f));
    world.AddComponent(lightE, lt);
    world.AddComponent(lightE, DirectionalLight{});

    // 多个 sphere renderable —— 模拟 prefab 模板里的多个子实体几何。
    for (std::size_t i = 0; i < localOffsets.size(); ++i)
    {
        Entity e = world.CreateEntity();
        TransformComponent t{};
        t.position = centerOffset + localOffsets[i];
        world.AddComponent(e, t);
        RenderableComponent rc;
        rc.mesh             = mesh;
        rc.materialInstance = mats[i % mats.size()];
        world.AddComponent(e, rc);
    }
    return camE;
}

bool ReadbackCenter(Orange::Renderer::RenderDevice& device,
                    Orange::Rhi::RHITexture& target,
                    std::uint32_t width, std::uint32_t height,
                    float outRGBA[4])
{
    auto& rhi = device.GetRhiDevice();
    const std::uint64_t bytes =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4u;

    Orange::Rhi::BufferDesc bd{};
    bd.mSize        = bytes;
    bd.mUsage       = Orange::Rhi::BufferUsage::Transfer;
    bd.mMemoryUsage = Orange::Rhi::MemoryUsage::GpuToCpu;
    auto readback = rhi.CreateBuffer(bd);
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
    if (cmd->End() != Orange::ResultCode::Success
        || rhi.SubmitCommandList(*cmd) != Orange::ResultCode::Success)
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
    const std::uint64_t cx  = width / 2u;
    const std::uint64_t cy  = height / 2u;
    const std::uint64_t idx = (cy * width + cx) * 4u;
    // BGRA8Unorm → 字节序 B, G, R, A。
    outRGBA[2] = p[idx + 0] / 255.0f;  // B
    outRGBA[1] = p[idx + 1] / 255.0f;  // G
    outRGBA[0] = p[idx + 2] / 255.0f;  // R
    outRGBA[3] = p[idx + 3] / 255.0f;  // A
    readback->Unmap();
    return true;
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[PrefabThumbnailBakeTest] running\n");

    constexpr std::uint32_t kViewportW = 256;
    constexpr std::uint32_t kViewportH = 256;
    constexpr std::uint32_t kThumbW    = 96;
    constexpr std::uint32_t kThumbH    = 96;

    Orange::Renderer::RenderDeviceDesc deviceDesc{};
    deviceDesc.mBackend          = Orange::Renderer::BackendType::Default;
    deviceDesc.mEnableValidation = true;
    auto pDevice = Orange::Renderer::RenderDevice::Create(deviceDesc);
    if (!pDevice)
    {
        std::fprintf(stderr,
                     "[PrefabThumbnailBakeTest] RenderDevice::Create 失败 —— 本机可能无 "
                     "Vulkan，跳过\n");
        return 0;
    }

    AssetRegistry assets;
    {
        auto reg = assets.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
        assert(reg.IsOk());
    }
    auto meshRes = assets.Insert<MeshAsset>("test/prefab_thumb_sphere",
                                            MakeSphereMesh(0.5f, 32u, 16u));
    assert(meshRes.IsOk());
    AssetHandle<MeshAsset> mesh = meshRes.Value();

    MaterialSystem matSys(assets);
    {
        auto rb = matSys.RegisterBuiltins();
        assert(rb.IsOk());
    }
    // 三个亮色 PBR 实例（不同 baseColor）—— 保证中心像素被打亮。地址稳定供
    // RenderableComponent::materialInstance 持有（持到测试结束）。
    auto pbrA = matSys.CreateInstance("pbr");
    auto pbrB = matSys.CreateInstance("pbr");
    auto pbrC = matSys.CreateInstance("pbr");
    assert(pbrA && pbrB && pbrC);
    pbrA->SetUniform("uBaseColor", glm::vec4(0.85f, 0.55f, 0.25f, 1.0f));
    pbrA->SetUniform("uMRA", glm::vec4(0.0f, 0.5f, 1.0f, 0.0f));
    pbrB->SetUniform("uBaseColor", glm::vec4(0.25f, 0.75f, 0.85f, 1.0f));
    pbrB->SetUniform("uMRA", glm::vec4(0.0f, 0.4f, 1.0f, 0.0f));
    pbrC->SetUniform("uBaseColor", glm::vec4(0.65f, 0.30f, 0.80f, 1.0f));
    pbrC->SetUniform("uMRA", glm::vec4(0.0f, 0.6f, 1.0f, 0.0f));
    const std::vector<MaterialInstance*> mats{pbrA.get(), pbrB.get(), pbrC.get()};

    // 三个球分布在原点附近 ±1 范围（模拟一个 prefab 模板的多子实体）。
    const std::vector<glm::vec3> offsets{
        {-1.0f, 0.0f, 0.0f},
        { 1.0f, 0.0f, 0.0f},
        { 0.0f, 1.0f, 0.0f},
    };

    Pipeline pipeline;
    {
        auto init = pipeline.InitializeOffscreen(*pDevice, assets, kViewportW, kViewportH);
        if (init.IsErr())
        {
            std::fprintf(stderr,
                         "[PrefabThumbnailBakeTest] InitializeOffscreen failed (code=%u). "
                         "可能 SPV 文件不在 CWD，跳过后续验证。\n",
                         static_cast<unsigned>(init.Error()));
            return 0;
        }
    }
    assert(pipeline.IsInitialized());
    pipeline.SetDummyIblAmbient(0.5f, 0.5f, 0.5f);

    Orange::Rhi::TextureDesc td{};
    td.mWidth  = kThumbW;
    td.mHeight = kThumbH;
    td.mFormat = Orange::Rhi::TextureFormat::BGRA8Unorm;
    td.mUsage  = Orange::Rhi::TextureUsage::RenderTarget
               | Orange::Rhi::TextureUsage::Sampled
               | Orange::Rhi::TextureUsage::TransferSrc;
    auto target = pDevice->GetRhiDevice().CreateTexture(td);
    assert(target);

    // ---- case 1：居中 prefab → AABB 框相机 → 中心非黑 -----------------------
    {
        World world;
        Entity camE = BuildPrefabLikeWorld(world, mesh, mats, offsets,
                                           glm::vec3(0.0f));
        FrameCameraToScene(world, assets, camE);
        auto r = pipeline.RenderToTexture(world, target.get(), kThumbW, kThumbH);
        assert(r.IsOk());
        std::fprintf(stdout,
                     "  [PASS] RenderToTexture(居中 prefab → 96×96) 返回 Ok\n");

        float px[4] = {0, 0, 0, 0};
        const bool readOk = ReadbackCenter(*pDevice, *target, kThumbW, kThumbH, px);
        const float lum   = px[0] + px[1] + px[2];
        std::fprintf(stderr,
                     "  [readback case1] 中心 RGBA=(%.3f,%.3f,%.3f,%.3f) lum=%.3f ok=%d\n",
                     px[0], px[1], px[2], px[3], lum, readOk ? 1 : 0);
        assert(readOk && "prefab 缩略图 target readback 失败");
        assert(lum > 0.05f
               && "居中 prefab 缩略图中心像素全黑 —— prefab 未被框定 / 渲染到 target");
        std::fprintf(stdout,
                     "  [PASS] 居中 prefab 中心像素非黑（AABB 框定 + 渲染验证 lum=%.3f）\n",
                     lum);
    }

    // ---- case 2：偏离原点 prefab（中心 (5,0,0)）→ 框定必须跟随 AABB --------
    // 若相机仍固定看原点，偏移 5 + 范围 ±2 的几何全落在视锥外 → 中心黑。
    // 断言非黑 = AABB 框定真的把相机平移跟随了。
    {
        World world;
        const glm::vec3 farCenter(5.0f, 0.0f, 0.0f);
        Entity camE = BuildPrefabLikeWorld(world, mesh, mats, offsets, farCenter);
        FrameCameraToScene(world, assets, camE);
        auto r = pipeline.RenderToTexture(world, target.get(), kThumbW, kThumbH);
        assert(r.IsOk());

        float px[4] = {0, 0, 0, 0};
        const bool readOk = ReadbackCenter(*pDevice, *target, kThumbW, kThumbH, px);
        const float lum   = px[0] + px[1] + px[2];
        std::fprintf(stderr,
                     "  [readback case2] 中心 RGBA=(%.3f,%.3f,%.3f,%.3f) lum=%.3f ok=%d\n",
                     px[0], px[1], px[2], px[3], lum, readOk ? 1 : 0);
        assert(readOk && lum > 0.05f
               && "偏离原点 prefab 缩略图中心全黑 —— AABB 框定未跟随实例位置");
        std::fprintf(stdout,
                     "  [PASS] 偏离原点 prefab 中心非黑（框定跟随 AABB 验证 lum=%.3f）\n",
                     lum);
    }

    pipeline.Shutdown();
    assert(!pipeline.IsInitialized());
    target.reset();
    if (Orange::Failed(pDevice->WaitIdle()))
    {
        std::fprintf(stderr, "[PrefabThumbnailBakeTest] Shutdown 后 WaitIdle 失败\n");
        return 1;
    }

    std::fprintf(stdout, "[PrefabThumbnailBakeTest] all tests passed.\n");
    return 0;
}
