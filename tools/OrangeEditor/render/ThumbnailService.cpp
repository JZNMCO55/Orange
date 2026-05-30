#include "ThumbnailService.h"

#include "../BuiltinAssets.h"     // EnsureMaterialInstance（按 path 取 live instance）
#include "../EditorHierarchy.h"   // DestroySubtree（清 prefab 实例子树）
#include "../EditorHost.h"

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/PrefabAsset.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/MaterialTypes.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/PrefabInstantiation.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <orange/renderer/VulkanInterop.h>
#include <orange/rhi/RHIDevice.h>
#include <orange/rhi/RHITypes.h>

#include <backends/imgui_impl_vulkan.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>

namespace Orange::Editor::Render
{

namespace
{

// FNV-1a 64-bit —— 轻量、无依赖，content-hash 用。把任意字节序列累进种子。
constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime  = 1099511628211ull;

std::uint64_t HashBytes(std::uint64_t seed, const void* data, std::size_t size)
{
    const auto* p = static_cast<const unsigned char*>(data);
    std::uint64_t h = seed;
    for (std::size_t i = 0; i < size; ++i)
    {
        h ^= static_cast<std::uint64_t>(p[i]);
        h *= kFnvPrime;
    }
    return h;
}

std::uint64_t HashString(std::uint64_t seed, std::string_view s)
{
    return HashBytes(seed, s.data(), s.size());
}

// ---- AABB 框相机数学 -----------------------------------------------------
// 以下三个 helper 复刻自 EditorPicking.cpp 的匿名 namespace（ComposeWorldMatrix /
// ComputeMeshLocalAABB / TransformAABB），用于把 prefab 实例的全部 Renderable
// 几何合并出一个 world-space AABB，再据此框定缩略图相机。MVP 阶段刻意"复刻"而
// 非抽公共 util——EditorPicking 那份是 picking 专用、本份是缩略图专用，去重留
// 后续整骨（两处语义虽同但 churn 风险不值得现在合）。

// world matrix = T * R * S（hierarchy 不参与；prefab 实例各 Transform 已是
// 模板内相对坐标，对缩略图框定足够：整体 AABB 只需各实体 local mesh 经自身
// Transform 变换后合并）。
glm::mat4
ComposeWorldMatrix(const Orange::Engine::Scene::TransformComponent& xform) noexcept
{
    glm::mat4 m = glm::translate(glm::mat4(1.0f), xform.position);
    m *= glm::mat4_cast(xform.rotation);
    m  = glm::scale(m, xform.scale);
    return m;
}

struct LocalAABB
{
    glm::vec3 min;
    glm::vec3 max;
};

// mesh local AABB（扫 Positions min/max）。空 mesh 返回退化点 AABB。
LocalAABB
ComputeMeshLocalAABB(const Orange::Engine::Asset::MeshAsset& mesh) noexcept
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

// local AABB → world AABB（8 角点变换取新 min/max；粗 AABB，缩略图框定足够）。
LocalAABB
TransformAABB(const LocalAABB& local, const glm::mat4& worldMat) noexcept
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

// 据合并出的 world-space AABB（aabbMin / aabbMax）把缩略图相机摆到 3/4 视角并
// 框满几何，写进 world 的 cameraEntity 的 Camera 组件。prefab（多实体合并 AABB）
// 与 mesh（单 mesh local AABB）两条烘焙路径共用——抽出避免复制这段相机数学。
//
// 算法：3/4 视角方向 dir = normalize(1, 0.8, 1)，dist = r / sin(fov/2) 框满再
// *1.1 留 ~10% padding，near/far 据包围球半径夹出。
// 退化兜底：hasGeometry == false 或 AABB 退化（半径≈0）→ 固定相机 eye=(0,0,3)
// 看原点 + 默认 near/far（防除零 / NaN）。
// 返回 false 表示 cameraEntity 上没有 Camera 组件（调用方据此中止本次 bake）。
bool FrameCameraToAABB(Orange::Engine::World& world,
                       Orange::Engine::Entity cameraEntity,
                       const glm::vec3& aabbMin, const glm::vec3& aabbMax,
                       bool hasGeometry) noexcept
{
    using Orange::Engine::Render::Camera;

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
        // dist = r / sin(fov/2) 框满，再 *1.1 留 10% padding。
        const float dist = (radius / std::sin(kFovY * 0.5f)) * 1.1f;
        eye  = center + dir * dist;
        near = std::max(0.01f, dist - radius * 2.0f);
        far  = dist + radius * 2.0f;
    }
    // else：退化兜底——保持初始 eye=(0,0,3) 看原点 + 默认 near/far。

    auto* cam = world.GetComponent<Camera>(cameraEntity);
    if (cam == nullptr) { return false; }
    *cam = Camera::Perspective(kFovY, 1.0f, near, far);
    cam->view = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));
    return true;
}

}  // namespace

std::uint64_t ThumbnailService::ComputeContentHash(
    const Orange::Engine::Render::MaterialInstance& instance)
{
    using Orange::Engine::Render::MaterialUniformType;

    std::uint64_t h = kFnvOffset;

    // 1) template name —— 切 template 必须重烘（shader / uniform 布局变了）。
    if (const auto* mat = instance.GetMaterial(); mat != nullptr)
    {
        h = HashString(h, mat->name);
    }

    // 2) 全部 uniform override 值。GetUniformOverrideNames 顺序未定义
    //    （unordered_map），先排序保证同一内容 hash 稳定。
    std::vector<std::string> names = instance.GetUniformOverrideNames();
    std::sort(names.begin(), names.end());
    for (const auto& name : names)
    {
        h = HashString(h, name);
        const auto typeOpt = instance.GetUniformOverrideType(name);
        if (!typeOpt.has_value()) { continue; }
        // 把类型也混入：同 name 不同类型（schema 演化）算不同内容。
        const auto t = static_cast<std::uint32_t>(*typeOpt);
        h = HashBytes(h, &t, sizeof(t));
        switch (*typeOpt)
        {
            case MaterialUniformType::Float:
            {
                const float v = instance.GetUniformFloat(name).value_or(0.0f);
                h = HashBytes(h, &v, sizeof(v));
                break;
            }
            case MaterialUniformType::Int:
            {
                const std::int32_t v = instance.GetUniformInt(name).value_or(0);
                h = HashBytes(h, &v, sizeof(v));
                break;
            }
            case MaterialUniformType::Vec2:
            {
                const glm::vec2 v = instance.GetUniformVec2(name).value_or(glm::vec2(0.0f));
                h = HashBytes(h, &v, sizeof(v));
                break;
            }
            case MaterialUniformType::Vec3:
            {
                const glm::vec3 v = instance.GetUniformVec3(name).value_or(glm::vec3(0.0f));
                h = HashBytes(h, &v, sizeof(v));
                break;
            }
            case MaterialUniformType::Vec4:
            {
                const glm::vec4 v = instance.GetUniformVec4(name).value_or(glm::vec4(0.0f));
                h = HashBytes(h, &v, sizeof(v));
                break;
            }
            case MaterialUniformType::Mat4:
            {
                const glm::mat4 v = instance.GetUniformMat4(name).value_or(glm::mat4(1.0f));
                h = HashBytes(h, &v, sizeof(v));
                break;
            }
        }
    }

    // 3) 纹理 override binding 列表 —— 换贴图 binding 也算内容变化。
    //    （仅 hash binding 号；贴图内容本身的版本暂不追踪，编辑器换贴图走
    //     重新 SetTexture，binding 集合或 instance 指针变化已足够覆盖常见路径。）
    std::vector<std::uint32_t> bindings = instance.GetTextureOverrideBindings();
    std::sort(bindings.begin(), bindings.end());
    for (const auto b : bindings)
    {
        h = HashBytes(h, &b, sizeof(b));
    }

    return h;
}

ThumbnailService::ThumbnailService(EditorHost&                     host,
                                   Orange::Renderer::RenderDevice& device,
                                   VkDescriptorPool                imguiDescriptorPool)
    : mHost(host)
    , mDevice(device)
    , mImguiDescriptorPool(imguiDescriptorPool)
{
    CreateSampler();
}

ThumbnailService::~ThumbnailService()
{
    Shutdown();
}

void ThumbnailService::SetPipeline(Orange::Engine::Render::Pipeline* pipeline) noexcept
{
    mpPipeline = pipeline;
}

ImTextureID ThumbnailService::GetOrRequestThumbnail(const std::string& materialPath)
{
    return RequestThumbnail(materialPath, ThumbKind::Material);
}

ImTextureID ThumbnailService::GetOrRequestPrefabThumbnail(const std::string& prefabPath)
{
    return RequestThumbnail(prefabPath, ThumbKind::Prefab);
}

ImTextureID ThumbnailService::GetOrRequestMeshThumbnail(const std::string& meshPath)
{
    return RequestThumbnail(meshPath, ThumbKind::Mesh);
}

ImTextureID ThumbnailService::RequestThumbnail(const std::string& path, ThumbKind kind)
{
    if (path.empty()) { return 0; }

    // 按 kind 计算当前 live content hash（命中比对用）。返回 nullopt 表示
    // 取不到 hash 源（material instance / prefab asset 缺失）——这时跳过比对，
    // 命中直接返回旧缩略图（不主动重烘）。
    const auto computeCurHash = [&]() -> std::optional<std::uint64_t>
    {
        if (kind == ThumbKind::Material)
        {
            if (auto* inst = ::EnsureMaterialInstance(mHost, path); inst != nullptr)
            {
                return ComputeContentHash(*inst);
            }
        }
        else if (kind == ThumbKind::Prefab)
        {
            if (mHost.assets.pAssets != nullptr)
            {
                auto loaded =
                    mHost.assets.pAssets->Load<Orange::Engine::Asset::PrefabAsset>(path);
                if (loaded.IsOk())
                {
                    if (const auto* asset = mHost.assets.pAssets->Get(loaded.Value());
                        asset != nullptr)
                    {
                        return HashString(kFnvOffset, asset->TemplateBlob());
                    }
                }
            }
        }
        else  // Mesh
        {
            // mesh content hash = 路径 FNV（与 bake 时一致）。mesh 文件内容变化
            // 罕见，路径 hash 足够稳定，命中后必相等，不触发无谓重烘。
            return HashString(kFnvOffset, path);
        }
        return std::nullopt;
    };

    auto it = mCache.find(path);
    if (it != mCache.end() && it->second.descriptorSet != VK_NULL_HANDLE)
    {
        // 命中：标记本帧被引用（LRU），并比对当前 content-hash。一致直接返回旧
        // 缩略图；变了入 pending 留 FlushPending 帧外重烘（GetOrRequest 是帧内 UI
        // 路径，禁止 GPU 工作 / 改缓存元数据），本帧仍返回旧缩略图避免闪烁。
        mReferencedThisFrame.push_back(path);
        const auto curHash = computeCurHash();
        if (curHash.has_value() && *curHash != it->second.contentHash && !IsPending(path))
        {
            mPending.push_back(PendingItem{path, kind});
        }
        return reinterpret_cast<ImTextureID>(it->second.descriptorSet);
    }

    // 未命中（或缓存里有占位但 set 还没建好）→ 去重入 pending。
    if (!IsPending(path))
    {
        mPending.push_back(PendingItem{path, kind});
    }
    return 0;
}

bool ThumbnailService::IsPending(const std::string& path) const
{
    return std::find_if(mPending.begin(), mPending.end(),
                        [&](const PendingItem& p) { return p.path == path; })
           != mPending.end();
}

void ThumbnailService::FlushPending(std::uint64_t frameIndex, int maxPerFrame)
{
    // 先把本帧被 UI 引用过的缓存条目 lastUsedFrame 推到当前帧（LRU 记账）——
    // 即便 Pipeline 未就绪 / 无 pending 也要做，否则正在显示的缩略图会被误判
    // 为旧而淘汰。处理完即清空。
    for (const auto& path : mReferencedThisFrame)
    {
        auto it = mCache.find(path);
        if (it != mCache.end()) { it->second.lastUsedFrame = frameIndex; }
    }
    mReferencedThisFrame.clear();

    if (mpPipeline != nullptr && maxPerFrame > 0)  // Pipeline 未就绪：不烘
    {
        int baked = 0;
        while (!mPending.empty() && baked < maxPerFrame)
        {
            const PendingItem item = mPending.front();
            mPending.erase(mPending.begin());
            // BakeThumbnail 无论成功失败都把 item 出队（失败不无限重试，避免每帧
            // 反复烘一个坏资产拖慢编辑器；用户改好后下次 GetOrRequest 重入队）。
            BakeThumbnail(item.path, item.kind, frameIndex);
            ++baked;
        }
    }

    // 帧外做 LRU 淘汰（RemoveTexture 安全）。
    EvictIfNeeded(frameIndex);
}

void ThumbnailService::Invalidate(const std::string& materialPath)
{
    auto it = mCache.find(materialPath);
    if (it == mCache.end()) { return; }
    // 改 hash 为哨兵 0 让下次 GetOrRequest 命中比对必不相等 → 重入 pending。
    // 不在此直接 ReleaseEntry：Invalidate 可能在帧内（Material Save / Prefab
    // 创建）被调，此刻 descriptor set 可能仍被本帧 ImGui draw data 引用，帧内
    // RemoveTexture 不安全。
    it->second.contentHash = 0;
    // 显式入队（去重），让下一帧 FlushPending 立即重烘。kind 取自缓存条目
    // （material / prefab 由它决定重烘走哪条路径）。
    if (!IsPending(materialPath))
    {
        mPending.push_back(PendingItem{materialPath, it->second.kind});
    }
}

void ThumbnailService::Shutdown()
{
    for (auto& [path, entry] : mCache)
    {
        ReleaseEntry(entry);
    }
    mCache.clear();
    mPending.clear();
    mpScratchWorld.reset();
    mScratchBuilt = false;
    mpPrefabScratchWorld.reset();
    mPrefabScratchBuilt = false;
    mpMeshScratchWorld.reset();
    mMeshScratchBuilt = false;
    DestroySampler();
}

bool ThumbnailService::BakeThumbnail(const std::string& path, ThumbKind kind,
                                     std::uint64_t frameIndex)
{
    if (mpPipeline == nullptr) { return false; }
    if (mSampler == VK_NULL_HANDLE) { return false; }

    // 按 kind 分派到对应烘焙路径。各路径各自构造 scratch world 后收敛到 FinalizeBake。
    switch (kind)
    {
        case ThumbKind::Material: return BakeMaterialThumbnail(path, frameIndex);
        case ThumbKind::Prefab:   return BakePrefabThumbnail(path, frameIndex);
        case ThumbKind::Mesh:     return BakeMeshThumbnail(path, frameIndex);
    }
    return false;
}

bool ThumbnailService::BakeMaterialThumbnail(const std::string& materialPath,
                                             std::uint64_t      frameIndex)
{
    // 1) 取 / lazy-create 对应 .material 的 live MaterialInstance。
    Orange::Engine::Render::MaterialInstance* inst =
        ::EnsureMaterialInstance(mHost, materialPath);
    if (inst == nullptr) { return false; }

    // 2) 确保材质 scratch world 就位（首次 bake 建好 camera + dir-light + sphere
    //    entity；后续 bake 只换 entity 的 materialInstance 指针）。
    const auto sphereMesh = mHost.assets.sphereMeshHandle;
    if (!sphereMesh.IsValid()) { return false; }

    if (!mScratchBuilt)
    {
        using namespace Orange::Engine;
        using Orange::Engine::Render::Camera;
        using Orange::Engine::Render::DirectionalLight;
        using Orange::Engine::Render::MakeDirectionalLightRotationFromDir;
        using Orange::Engine::Render::RenderableComponent;
        using Orange::Engine::Scene::TransformComponent;

        mpScratchWorld = std::make_unique<World>();
        World& w = *mpScratchWorld;

        // 相机：正对球心，距离 3，45° fov，aspect=1（正方形缩略图）。
        Entity camE = w.CreateEntity();
        Camera cam  = Camera::Perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
        cam.view    = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f),
                                  glm::vec3(0.0f, 0.0f, 0.0f),
                                  glm::vec3(0.0f, 1.0f, 0.0f));
        w.AddComponent(camE, cam);

        // 方向光：左上前方侧光，给球面立体感（暗面靠 viewport Pipeline 已设
        // 的 dummy IBL ambient 抬亮，不在此 SetDummyIblAmbient）。
        Entity lightE = w.CreateEntity();
        TransformComponent lt{};
        lt.rotation =
            MakeDirectionalLightRotationFromDir(glm::vec3(-0.4f, -0.5f, -1.0f));
        w.AddComponent(lightE, lt);
        w.AddComponent(lightE, DirectionalLight{});

        // 球体 entity：Transform 单位 + Renderable{sphere, material}。
        mScratchRenderableEntity = w.CreateEntity();
        w.AddComponent(mScratchRenderableEntity, TransformComponent{});
        RenderableComponent rc;
        rc.mesh             = sphereMesh;
        rc.materialInstance = inst;
        w.AddComponent(mScratchRenderableEntity, rc);

        mScratchBuilt = true;
    }
    else
    {
        // 复用 scratch：仅更新 material 指针（mesh / camera / light 不变）。
        using Orange::Engine::Render::RenderableComponent;
        auto* rc = mpScratchWorld->GetComponent<RenderableComponent>(
            mScratchRenderableEntity);
        if (rc == nullptr) { return false; }
        rc->mesh             = sphereMesh;
        rc->materialInstance = inst;
    }

    // 3) 公共尾段：取 / 建 RT → RenderToTexture → AddTexture → 写元数据。
    ThumbEntry& entry = mCache[materialPath];
    return FinalizeBake(entry, materialPath, *mpScratchWorld, frameIndex,
                        ComputeContentHash(*inst), ThumbKind::Material);
}

bool ThumbnailService::FinalizeBake(ThumbEntry&            entry,
                                    const std::string&     path,
                                    Orange::Engine::World& scratchWorld,
                                    std::uint64_t          frameIndex,
                                    std::uint64_t          contentHash,
                                    ThumbKind              kind)
{
    // 1) 取 / 建 target RT（96×96 BGRA8，usage = RenderTarget | Sampled）。
    //    复用缓存里已有 entry 的 RT（resize 不变，重烘只是重渲内容）。
    if (entry.pRt == nullptr)
    {
        Orange::Rhi::TextureDesc td{};
        td.mWidth  = kThumbSize;
        td.mHeight = kThumbSize;
        td.mFormat = Orange::Rhi::TextureFormat::BGRA8Unorm;  // == kSwapchainColorFormat
        td.mUsage  = Orange::Rhi::TextureUsage::RenderTarget
                   | Orange::Rhi::TextureUsage::Sampled;
        entry.pRt = mDevice.GetRhiDevice().CreateTexture(td);
        if (entry.pRt == nullptr)
        {
            // 建 RT 失败：移除空 entry，避免缓存里留半成品。
            mCache.erase(path);
            return false;
        }
    }

    // 2) 用 viewport Pipeline 把 scratch world 渲到 target RT。
    auto rr = mpPipeline->RenderToTexture(scratchWorld, entry.pRt.get(),
                                          kThumbSize, kThumbSize);
    if (rr.IsErr())
    {
        // 渲染失败：保留 RT（下次重试可复用），但不更新 descriptor set / hash。
        // descriptorSet 仍为旧值（首次失败则 VK_NULL_HANDLE → GetOrRequest 返回 0）。
        return false;
    }

    // 3) 包 descriptor set 供 ImGui 采样。RenderToTexture 成功后 target 处于
    //    ShaderReadOnly。首次烘建 set；重烘时 RT view 句柄不变（复用同一 RT），
    //    descriptor set 可继续指向同一 view，无需 Remove+Add。
    if (entry.descriptorSet == VK_NULL_HANDLE)
    {
        auto* rawView = Orange::Renderer::Interop::GetVulkanImageView(*entry.pRt);
        if (rawView == nullptr)
        {
            return false;
        }
        entry.descriptorSet = ImGui_ImplVulkan_AddTexture(
            mSampler,
            static_cast<VkImageView>(rawView),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (entry.descriptorSet == VK_NULL_HANDLE)
        {
            return false;
        }
    }

    // 4) 写缓存元数据。
    entry.contentHash   = contentHash;
    entry.lastUsedFrame = frameIndex;
    entry.kind          = kind;
    return true;
}

bool ThumbnailService::BakePrefabThumbnail(const std::string& prefabPath,
                                           std::uint64_t      frameIndex)
{
    using namespace Orange::Engine;
    using Orange::Engine::Render::Camera;
    using Orange::Engine::Render::DirectionalLight;
    using Orange::Engine::Render::MakeDirectionalLightRotationFromDir;
    using Orange::Engine::Render::RenderableComponent;
    using Orange::Engine::Scene::InstantiateOptions;
    using Orange::Engine::Scene::LoadOptions;
    using Orange::Engine::Scene::TransformComponent;

    auto* pReg = mHost.assets.pAssets.get();
    if (pReg == nullptr) { return false; }

    // 1) Load<PrefabAsset> 拿 handle（失败 return false 出队）。
    auto loaded = pReg->Load<Orange::Engine::Asset::PrefabAsset>(prefabPath);
    if (loaded.IsErr()) { return false; }
    const auto* asset = pReg->Get(loaded.Value());
    if (asset == nullptr) { return false; }

    // 2) 确保 prefab scratch world 就位（首次建常驻 camera + dir-light entity）。
    if (!mPrefabScratchBuilt)
    {
        mpPrefabScratchWorld = std::make_unique<World>();
        World& w = *mpPrefabScratchWorld;

        // 相机：姿态每次 bake 按当前 prefab AABB 重设；此处先建好 entity + 占位
        // Camera（FinalizeBake 渲染前会覆写 view / projection）。
        mPrefabCameraEntity = w.CreateEntity();
        Camera cam = Camera::Perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
        cam.view   = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f),
                                 glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
        w.AddComponent(mPrefabCameraEntity, cam);

        // 方向光：与材质球同款左上前方侧光。
        Entity lightE = w.CreateEntity();
        TransformComponent lt{};
        lt.rotation =
            MakeDirectionalLightRotationFromDir(glm::vec3(-0.4f, -0.5f, -1.0f));
        w.AddComponent(lightE, lt);
        w.AddComponent(lightE, DirectionalLight{});

        mPrefabScratchBuilt = true;
    }

    World& world = *mpPrefabScratchWorld;

    // 3) 实例化 prefab 到 scratch world。LoadOptions 填全 4 字段（抄
    //    PrefabCommands.cpp：让模板 blob 里的 Renderable / Animator 等组件正确
    //    认领资源；漏填会让实例丢材质 / animator）。
    LoadOptions lo;
    lo.assetRegistry          = pReg;
    lo.animatorRegistry       = mHost.assets.pAnimators.get();
    lo.namedMaterialInstances = &mHost.assets.namedMaterialInstances;
    lo.extraSerializers       = mHost.extraSerializers;

    InstantiateOptions opt{};
    opt.parent      = Entity::Invalid();
    opt.loadOptions = &lo;

    auto inst = Scene::InstantiatePrefab(world, *pReg, loaded.Value(), opt);
    if (inst.IsErr()) { return false; }
    const Entity instanceRoot = inst.Value();

    // RAII 守卫：任何退出路径（render 失败 / early-return / 异常）都
    // DestroySubtree 清掉实例子树，避免实例残留累积污染下次 AABB。这是本函数
    // 最需小心的实现点——bake 流程中段任一 return 都会触发析构清理。
    struct InstanceGuard
    {
        World*  pWorld;
        Entity  root;
        ~InstanceGuard()
        {
            if (pWorld != nullptr && root.IsValid() && pWorld->IsValid(root))
            {
                EditorHierarchy::DestroySubtree(*pWorld, root);
            }
        }
    } guard{&world, instanceRoot};

    // 4) 算 AABB 框相机：遍历 scratch world 的 (Transform, Renderable) view
    //    （camera / light 无 Renderable，不干扰），合并各实体的 world-space AABB。
    glm::vec3 aabbMin(std::numeric_limits<float>::max());
    glm::vec3 aabbMax(std::numeric_limits<float>::lowest());
    bool      hasGeometry = false;
    {
        auto& reg  = world.Registry();
        auto  view = reg.view<Orange::Engine::Scene::TransformComponent,
                              Orange::Engine::Render::RenderableComponent>();
        for (auto e : view)
        {
            const auto& xform = view.get<Orange::Engine::Scene::TransformComponent>(e);
            const auto& rc    = view.get<Orange::Engine::Render::RenderableComponent>(e);
            if (!rc.visible) { continue; }
            const auto* pMesh = pReg->Get(rc.mesh);
            if (pMesh == nullptr || pMesh->Empty()) { continue; }

            const LocalAABB localAABB = ComputeMeshLocalAABB(*pMesh);
            const glm::mat4 worldMat  = ComposeWorldMatrix(xform);
            const LocalAABB worldAABB = TransformAABB(localAABB, worldMat);
            aabbMin     = glm::min(aabbMin, worldAABB.min);
            aabbMax     = glm::max(aabbMax, worldAABB.max);
            hasGeometry = true;
        }
    }

    // 5) 据 AABB 摆相机（共用 helper，prefab / mesh 同一段相机数学）。返回 false
    //    表示相机 entity 上没有 Camera 组件 —— 中止（守卫会清实例）。
    if (!FrameCameraToAABB(world, mPrefabCameraEntity, aabbMin, aabbMax, hasGeometry))
    {
        return false;
    }

    // 6) 公共尾段。content hash = prefab templateBlob 的 FNV。
    ThumbEntry& entry = mCache[prefabPath];
    const std::uint64_t hash = HashString(kFnvOffset, asset->TemplateBlob());
    const bool ok = FinalizeBake(entry, prefabPath, world, frameIndex, hash,
                                 ThumbKind::Prefab);

    // 7) 实例清理由 guard 析构统一执行（无论 ok 与否）。
    return ok;
}

bool ThumbnailService::BakeMeshThumbnail(const std::string& meshPath,
                                         std::uint64_t      frameIndex)
{
    using namespace Orange::Engine;
    using Orange::Engine::Render::Camera;
    using Orange::Engine::Render::DirectionalLight;
    using Orange::Engine::Render::MakeDirectionalLightRotationFromDir;
    using Orange::Engine::Render::RenderableComponent;
    using Orange::Engine::Scene::TransformComponent;

    auto* pReg = mHost.assets.pAssets.get();
    if (pReg == nullptr) { return false; }

    // 1) Load<MeshAsset> 拿 handle（失败 return false 出队，不无限重试）。
    auto loaded = pReg->Load<Orange::Engine::Asset::MeshAsset>(meshPath);
    if (loaded.IsErr()) { return false; }
    const auto meshHandle = loaded.Value();
    const auto* pMesh = pReg->Get(meshHandle);
    if (pMesh == nullptr) { return false; }

    // 2) 默认材质 —— mesh 自身无材质，固定用内置 PBR baseline 着色（pPbrMaterial，
    //    Cook-Torrance，最能体现 DCC 模型的形体）。缺失则回退默认 renderable 材质
    //    （textured）；两者都没有则中止（无法着色）。
    Orange::Engine::Render::MaterialInstance* defaultMat =
        mHost.assets.pPbrMaterial.get();
    if (defaultMat == nullptr)
    {
        defaultMat = mHost.assets.pDefaultRenderableMaterial.get();
    }
    if (defaultMat == nullptr) { return false; }

    // 3) 确保 mesh scratch world 就位（首次建常驻 camera + dir-light + 单个
    //    renderable entity；后续 bake 只换 rc.mesh 指针、按 AABB 重摆相机）。
    if (!mMeshScratchBuilt)
    {
        mpMeshScratchWorld = std::make_unique<World>();
        World& w = *mpMeshScratchWorld;

        // 相机：姿态每次 bake 按当前 mesh AABB 重设（FrameCameraToAABB 覆写），
        // 此处先建好 entity + 占位 Camera。
        mMeshCameraEntity = w.CreateEntity();
        Camera cam = Camera::Perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
        cam.view   = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f),
                                 glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
        w.AddComponent(mMeshCameraEntity, cam);

        // 方向光：与材质球 / prefab 同款左上前方侧光。
        Entity lightE = w.CreateEntity();
        TransformComponent lt{};
        lt.rotation =
            MakeDirectionalLightRotationFromDir(glm::vec3(-0.4f, -0.5f, -1.0f));
        w.AddComponent(lightE, lt);
        w.AddComponent(lightE, DirectionalLight{});

        // 单个 renderable entity：identity Transform + Renderable{mesh, 默认材质}。
        // mesh / material 指针每次 bake 更新（见下）。
        mMeshRenderableEntity = w.CreateEntity();
        w.AddComponent(mMeshRenderableEntity, TransformComponent{});
        RenderableComponent rc;
        rc.mesh             = meshHandle;
        rc.materialInstance = defaultMat;
        w.AddComponent(mMeshRenderableEntity, rc);

        mMeshScratchBuilt = true;
    }
    else
    {
        // 复用 scratch：仅更新 mesh handle + material 指针（camera / light 不变，
        // 相机姿态下面按当前 AABB 重设）。
        auto* rc = mpMeshScratchWorld->GetComponent<RenderableComponent>(
            mMeshRenderableEntity);
        if (rc == nullptr) { return false; }
        rc->mesh             = meshHandle;
        rc->materialInstance = defaultMat;
    }

    World& world = *mpMeshScratchWorld;

    // 4) 算单 mesh 的 world AABB —— identity transform（mesh 摆原点不旋转不缩放），
    //    所以 world AABB == local AABB。空 mesh → hasGeometry=false 走退化兜底。
    glm::vec3 aabbMin(0.0f);
    glm::vec3 aabbMax(0.0f);
    bool      hasGeometry = false;
    if (!pMesh->Empty())
    {
        const LocalAABB localAABB = ComputeMeshLocalAABB(*pMesh);
        const LocalAABB worldAABB =
            TransformAABB(localAABB, ComposeWorldMatrix(TransformComponent{}));
        aabbMin     = worldAABB.min;
        aabbMax     = worldAABB.max;
        hasGeometry = true;
    }

    // 5) 据 AABB 摆相机（与 prefab 共用 helper）。
    if (!FrameCameraToAABB(world, mMeshCameraEntity, aabbMin, aabbMax, hasGeometry))
    {
        return false;
    }

    // 6) 公共尾段。content hash = mesh 路径 FNV（与 RequestThumbnail 比对源一致）。
    //    renderable 常驻、只换指针，无 prefab 那样的实例子树，无需 DestroySubtree。
    ThumbEntry& entry = mCache[meshPath];
    const std::uint64_t hash = HashString(kFnvOffset, meshPath);
    return FinalizeBake(entry, meshPath, world, frameIndex, hash, ThumbKind::Mesh);
}

void ThumbnailService::ReleaseEntry(ThumbEntry& entry)
{
    if (entry.descriptorSet != VK_NULL_HANDLE)
    {
        ImGui_ImplVulkan_RemoveTexture(entry.descriptorSet);
        entry.descriptorSet = VK_NULL_HANDLE;
    }
    entry.pRt.reset();  // RHITexture 析构走 deferred-destroy（device 自管时序）
}

void ThumbnailService::EvictIfNeeded(std::uint64_t frameIndex)
{
    while (mCache.size() > kCacheCap)
    {
        // 找 lastUsedFrame 最旧、且非本帧引用（lastUsedFrame < frameIndex）的
        // 条目淘汰。本帧 GetOrRequest 命中的条目 lastUsedFrame 可能仍是旧值
        // （命中不在帧内更新），但它的 descriptor set 正被 ImGui draw data 引用
        // ——FlushPending 在 ImGui::Render 之前调，此刻 draw data 尚未提交，
        // RemoveTexture 仍安全（descriptor set 下一帧才被 GPU 实际采样）。
        auto victim = mCache.end();
        std::uint64_t oldest = frameIndex;  // 上界：本帧
        for (auto it = mCache.begin(); it != mCache.end(); ++it)
        {
            if (it->second.lastUsedFrame < oldest)
            {
                oldest = it->second.lastUsedFrame;
                victim = it;
            }
        }
        if (victim == mCache.end())
        {
            // 全部条目都是本帧（lastUsedFrame == frameIndex）：放弃淘汰，
            // 避免误删本帧正在显示的缩略图。缓存暂时超 cap，下一帧再收。
            break;
        }
        ReleaseEntry(victim->second);
        mCache.erase(victim);
    }
}

void ThumbnailService::CreateSampler()
{
    // 复用 ScenePanel::CreateScenePanelSampler 的 loader 解析路径（编辑器统一走
    // OrangeRender 透出的 volk loader，避免与静态 vulkan-1.lib 的 dispatch 错位）。
    auto* pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        Orange::Renderer::Interop::GetVulkanGetInstanceProcAddr());
    if (pfnGetInstanceProcAddr == nullptr) { return; }
    const auto handles = Orange::Renderer::Interop::GetVulkanDeviceHandles(mDevice);
    if (handles.vkInstance == nullptr || handles.vkDevice == nullptr) { return; }
    auto pfnGetDeviceProcAddrFn = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        pfnGetInstanceProcAddr(static_cast<VkInstance>(handles.vkInstance),
                               "vkGetDeviceProcAddr"));
    if (pfnGetDeviceProcAddrFn == nullptr) { return; }
    auto pfnCreate = reinterpret_cast<PFN_vkCreateSampler>(
        pfnGetDeviceProcAddrFn(static_cast<VkDevice>(handles.vkDevice),
                               "vkCreateSampler"));
    if (pfnCreate == nullptr) { return; }

    VkSamplerCreateInfo s{};
    s.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    s.magFilter    = VK_FILTER_LINEAR;
    s.minFilter    = VK_FILTER_LINEAR;
    s.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.minLod       = 0.0f;
    s.maxLod       = 0.0f;
    if (pfnCreate(static_cast<VkDevice>(handles.vkDevice), &s, nullptr, &mSampler)
        != VK_SUCCESS)
    {
        mSampler = VK_NULL_HANDLE;
    }
}

void ThumbnailService::DestroySampler()
{
    if (mSampler == VK_NULL_HANDLE) { return; }
    auto* pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        Orange::Renderer::Interop::GetVulkanGetInstanceProcAddr());
    if (pfnGetInstanceProcAddr == nullptr) { mSampler = VK_NULL_HANDLE; return; }
    const auto handles = Orange::Renderer::Interop::GetVulkanDeviceHandles(mDevice);
    if (handles.vkDevice == nullptr) { mSampler = VK_NULL_HANDLE; return; }
    auto pfnGetDeviceProcAddrFn = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        pfnGetInstanceProcAddr(static_cast<VkInstance>(handles.vkInstance),
                               "vkGetDeviceProcAddr"));
    if (pfnGetDeviceProcAddrFn == nullptr) { mSampler = VK_NULL_HANDLE; return; }
    auto pfnDestroy = reinterpret_cast<PFN_vkDestroySampler>(
        pfnGetDeviceProcAddrFn(static_cast<VkDevice>(handles.vkDevice),
                               "vkDestroySampler"));
    if (pfnDestroy != nullptr)
    {
        pfnDestroy(static_cast<VkDevice>(handles.vkDevice), mSampler, nullptr);
    }
    mSampler = VK_NULL_HANDLE;
}

}  // namespace Orange::Editor::Render
