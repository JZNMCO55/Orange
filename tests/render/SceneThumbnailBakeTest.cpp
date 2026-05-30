// scene snapshot 缩略图烘焙验收（.scene.json 场景快照 thumbnail 的 headless 核心）。
//
// OrangeEditor 的 ThumbnailService::BakeSceneThumbnail 把一份 .scene.json 文件
// 内容 LoadFromString 追加到一个 scene scratch world（常驻 camera + dir-light）、
// 遍历其 (Transform, Renderable) 算合并 world-space AABB、据此摆相机（3/4 视角 +
// dist 框满 + padding），用 viewport 的 Pipeline 调 RenderToTexture 渲到 96×96 RT
// 供 ImGui 采样，渲完后遍历本次 LoadFromString 的 created 列表逐个 DestroySubtree
// 清干净（scene 多根，不像 prefab 单根）。
//
// 本测试**不**经 ImGui / EditorHost / ThumbnailService TU（CI 友好，无窗口 / 无
// descriptor pool / 不拖 OrangeEditor），只锁住 scene 缩略图的渲染内核 + 多根清理：
//   1) 程序式造一个含多个带 PBR 材质的 Renderable 实体的 world；
//   2) SaveSubtreeToString 得 scene blob（与 BakeSceneThumbnail 吃磁盘 .scene.json
//      文件内容等价——都是 scene/world schema 的 JSON 文本）；
//   3) LoadFromString 把 blob 追加进预建好 camera + light 的 scratch world，回填
//      created 列表；
//   4) 复刻 BakeSceneThumbnail 的 AABB 框相机数学摆相机，经 Pipeline::RenderToTexture
//      渲到 96×96 BGRA8 外部 RT，readback 中心像素断言非全黑（证明场景真被框定 +
//      渲进缩略图 RT）；
//   5) 复刻 BakeSceneThumbnail 的 RAII 多根清理：遍历 created 逐个 DestroySubtree，
//      断言清理后 scratch world 只剩常驻 camera + light（created 全销毁，验证多根
//      清理无残留——这是 scene 缩略图相对 prefab 的关键差异，prefab 单根
//      DestroySubtree，scene 多根需遍历）。
//
// 两个 case：
//   case1) scene 居中（多球分布在原点附近）→ AABB 框相机 → 中心非黑。
//   case2) scene 偏离原点（中心 (5,0,0) ± 1）→ AABB 框相机必须跟随平移，否则相机
//          仍看原点、几何全在画面外 → 中心黑。断言非黑 = 框定真的跟随了 AABB。
// 两 case 跑完后单独做一次多根清理无残留断言。
//
// 多根清理用本测试自带的 DestroySubtreeLocal（复刻 EditorHierarchy::DestroySubtree
// 的语义：收 child 再递归，最后摘父链销自己）——EditorHierarchy 不在引擎公共面，
// 与 sphere mesh 自建同理。
//
// Vulkan 不可用时 InitializeOffscreen / RenderDevice::Create 失败 → 打印跳过
// + return 0（与既有 *_thumbnail_bake_test 惯例一致，不阻塞无 GPU 的 CI）。

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
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
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
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
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
using Orange::Engine::Scene::HierarchyComponent;
using Orange::Engine::Scene::TransformComponent;
namespace SceneSerialization = Orange::Engine::Scene;

namespace
{

// lat/lon UV-sphere —— 复刻 BuiltinAssets.cpp MakeSphereMesh（与
// Prefab/MeshThumbnailBakeTest 同款）。
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

// 据 scratch world 的 (Transform, Renderable) view 合并 world AABB 摆相机，复刻
// ThumbnailService::BakeSceneThumbnail 的步骤 4 / 5（与 prefab / mesh 同一段数学）。
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

// 复刻 EditorHierarchy::DestroySubtree 语义（EditorHierarchy 不在引擎公共面）：
// 先收 child 列表（不能边遍历兄弟链边 destroy——destroy 会抽走组件让 sibling
// 字段失效），再依次递归销毁，最后摘父链销自己。本测试的实体都是平铺根（无
// HierarchyComponent），实际只销自己；保留递归形态与 BakeSceneThumbnail 调的
// DestroySubtree 等价。
void DestroySubtreeLocal(World& world, Entity e)
{
    if (!world.IsValid(e)) { return; }
    std::vector<Entity> children;
    if (const auto* h = world.GetComponent<HierarchyComponent>(e); h != nullptr)
    {
        Entity c = h->firstChild;
        while (c.IsValid())
        {
            children.push_back(c);
            const auto* ch = world.GetComponent<HierarchyComponent>(c);
            c = (ch != nullptr) ? ch->nextSibling : Entity::Invalid();
        }
    }
    for (const Entity c : children) { DestroySubtreeLocal(world, c); }
    world.DestroyEntity(e);
}

// 构造一份"scene 源 world"：若干带 PBR 材质的 sphere renderable，整组平移到
// centerOffset（验"框定跟随 AABB"用）。不含 camera / light——scene 缩略图烘焙
// 用 scratch 自己的 camera + dir-light 兜底，框定覆盖 scene 自带视角。
void BuildSceneSourceWorld(World& world, AssetHandle<MeshAsset> mesh,
                           const std::vector<MaterialInstance*>& mats,
                           const std::vector<glm::vec3>&         localOffsets,
                           const glm::vec3&                      centerOffset)
{
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
    std::fprintf(stdout, "[SceneThumbnailBakeTest] running\n");

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
                     "[SceneThumbnailBakeTest] RenderDevice::Create 失败 —— 本机可能无 "
                     "Vulkan，跳过\n");
        return 0;
    }

    AssetRegistry assets;
    {
        auto reg = assets.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
        assert(reg.IsOk());
    }
    // sphere mesh 用 Insert 挂进 registry（带 path），让 scene 序列化的
    // RenderableComponent.mesh 能 PathOf 反查 + LoadFromString 反向 dedup-hit。
    auto meshRes = assets.Insert<MeshAsset>("test/scene_thumb_sphere",
                                            MakeSphereMesh(0.5f, 32u, 16u));
    assert(meshRes.IsOk());
    AssetHandle<MeshAsset> mesh = meshRes.Value();

    MaterialSystem matSys(assets);
    {
        auto rb = matSys.RegisterBuiltins();
        assert(rb.IsOk());
    }
    // 三个亮色 PBR 实例（不同 baseColor）—— 保证中心像素被打亮。unique_ptr 持
    // 到测试结束，raw 指针进 namedMaterialInstances 供 scene 序列化按 id 反查 +
    // LoadFromString 正查回挂。
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

    // name → MaterialInstance* 表（Save 反查 id / Load 正查回挂；与编辑器
    // namedMaterialInstances 同款机制）。
    std::unordered_map<std::string, MaterialInstance*> named;
    named["mat/a"] = pbrA.get();
    named["mat/b"] = pbrB.get();
    named["mat/c"] = pbrC.get();

    // 三个球分布在原点附近 ±1 范围（模拟一张多实体场景）。
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
                         "[SceneThumbnailBakeTest] InitializeOffscreen failed (code=%u). "
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

    // 常驻 scene scratch world：camera + dir-light（兜底光），每次 LoadFromString
    // 把场景实体追加进来、渲完后逐个 DestroySubtree 清掉——复刻 BakeSceneThumbnail
    // 的 scratch 复用模式。两 case 共用同一 scratch（验"复用不残留"）。
    World scratch;
    Entity scratchCam = scratch.CreateEntity();
    scratch.AddComponent(scratchCam,
                         Camera::Perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f));
    Entity scratchLight = scratch.CreateEntity();
    {
        TransformComponent lt{};
        lt.rotation = MakeDirectionalLightRotationFromDir(glm::vec3(-0.4f, -0.5f, -1.0f));
        scratch.AddComponent(scratchLight, lt);
        scratch.AddComponent(scratchLight, DirectionalLight{});
    }
    // scratch 烘焙前的常驻实体数（camera + light = 2）——多根清理后必须回到这个数。
    const std::size_t kResidentCount = scratch.Size();
    assert(kResidentCount == 2);

    SceneSerialization::SaveOptions saveOpts;
    saveOpts.assetRegistry          = &assets;
    saveOpts.namedMaterialInstances = &named;

    SceneSerialization::LoadOptions loadOpts;
    loadOpts.assetRegistry          = &assets;
    loadOpts.namedMaterialInstances = &named;

    // 一次 scene 缩略图烘焙的完整流程（与 BakeSceneThumbnail 等价）：源 world →
    // SaveSubtreeToString 得 blob → LoadFromString 进 scratch（回填 created）→
    // 框相机 → RenderToTexture → readback 中心 → 多根清理 created。返回中心 lum，
    // 并把清理后 scratch 实体数写回 outResidentAfter 供断言。
    auto bakeOnce = [&](const glm::vec3& centerOffset, float& outLum,
                        std::size_t& outResidentAfter) -> bool
    {
        // 1) 源 world：多球 renderable（平移到 centerOffset）。
        World source;
        BuildSceneSourceWorld(source, mesh, mats, offsets, centerOffset);

        // 2) SaveSubtreeToString 得 scene blob（所有实体都是根，全传进去）。
        std::vector<Entity> roots;
        for (auto e : source.Registry().view<RenderableComponent>())
        {
            roots.push_back(World::FromEntt(e));
        }
        auto blobRes = SceneSerialization::SaveSubtreeToString(
            source, std::span<const Entity>{roots.data(), roots.size()}, saveOpts);
        if (blobRes.IsErr()) { return false; }
        const std::string blob = blobRes.Value();

        // 3) LoadFromString 把场景追加进 scratch，回填 created 列表（多根）。
        std::vector<Entity> created;
        auto loadRes =
            SceneSerialization::LoadFromString(blob, scratch, loadOpts, &created);
        if (loadRes.IsErr()) { return false; }
        assert(!created.empty() && "scene 加载未产出任何实体");

        // 4) 框相机 + 渲染。
        FrameCameraToScene(scratch, assets, scratchCam);
        auto r = pipeline.RenderToTexture(scratch, target.get(), kThumbW, kThumbH);
        const bool renderOk = r.IsOk();

        // 5) readback 中心像素。
        float px[4] = {0, 0, 0, 0};
        const bool readOk = renderOk
            && ReadbackCenter(*pDevice, *target, kThumbW, kThumbH, px);
        outLum = readOk ? (px[0] + px[1] + px[2]) : -1.0f;

        // 6) 多根 RAII 清理：遍历 created 逐个 DestroySubtree（跳已被连带销毁的 /
        //    死句柄）——复刻 BakeSceneThumbnail 的 SceneGuard。
        for (const Entity e : created)
        {
            if (e.IsValid() && scratch.IsValid(e))
            {
                DestroySubtreeLocal(scratch, e);
            }
        }
        outResidentAfter = scratch.Size();
        return renderOk && readOk;
    };

    // ---- case 1：居中 scene → AABB 框相机 → 中心非黑 + 清理无残留 -----------
    {
        float       lum = -1.0f;
        std::size_t residentAfter = 0;
        const bool  ok = bakeOnce(glm::vec3(0.0f), lum, residentAfter);
        std::fprintf(stderr,
                     "  [readback case1] 中心 lum=%.3f residentAfter=%zu ok=%d\n",
                     lum, residentAfter, ok ? 1 : 0);
        assert(ok && "居中 scene 缩略图烘焙失败（save/load/render/readback 任一环）");
        assert(lum > 0.05f
               && "居中 scene 缩略图中心像素全黑 —— 场景未被框定 / 渲染到 target");
        // 关键：多根清理后 scratch 必须只剩常驻 camera + light（created 全销毁）。
        assert(residentAfter == kResidentCount
               && "case1 多根清理后 scratch 有残留 —— created 未清干净");
        std::fprintf(stdout,
                     "  [PASS] 居中 scene 中心非黑 + 多根清理无残留（lum=%.3f）\n", lum);
    }

    // ---- case 2：偏离原点 scene（中心 (5,0,0)）→ 框定必须跟随 AABB -----------
    // 若相机仍固定看原点，偏移 5 + 范围 ±1 的几何全落画面外 → 中心黑。
    // 断言非黑 = AABB 框定真把相机平移跟随了。复用同一 scratch，再验一次清理无残留。
    {
        float       lum = -1.0f;
        std::size_t residentAfter = 0;
        const bool  ok = bakeOnce(glm::vec3(5.0f, 0.0f, 0.0f), lum, residentAfter);
        std::fprintf(stderr,
                     "  [readback case2] 中心 lum=%.3f residentAfter=%zu ok=%d\n",
                     lum, residentAfter, ok ? 1 : 0);
        assert(ok && lum > 0.05f
               && "偏离原点 scene 缩略图中心全黑 —— AABB 框定未跟随场景位置");
        // 第二次烘焙复用同一 scratch，清理后仍只剩常驻实体（验证复用不累积污染）。
        assert(residentAfter == kResidentCount
               && "case2 多根清理后 scratch 有残留 —— 复用 scratch 累积了污染");
        std::fprintf(stdout,
                     "  [PASS] 偏离原点 scene 中心非黑 + 复用 scratch 清理无残留（lum=%.3f）\n",
                     lum);
    }

    pipeline.Shutdown();
    assert(!pipeline.IsInitialized());
    target.reset();
    if (Orange::Failed(pDevice->WaitIdle()))
    {
        std::fprintf(stderr, "[SceneThumbnailBakeTest] Shutdown 后 WaitIdle 失败\n");
        return 1;
    }

    std::fprintf(stdout, "[SceneThumbnailBakeTest] all tests passed.\n");
    return 0;
}
