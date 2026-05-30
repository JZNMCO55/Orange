#include "ThumbnailService.h"

#include "../BuiltinAssets.h"  // EnsureMaterialInstance（按 path 取 live instance）
#include "../EditorHost.h"

#include <orange/engine/render/Camera.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/MaterialTypes.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/TransformComponent.h>

#include <orange/renderer/VulkanInterop.h>
#include <orange/rhi/RHIDevice.h>
#include <orange/rhi/RHITypes.h>

#include <backends/imgui_impl_vulkan.h>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstring>

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
    if (materialPath.empty()) { return 0; }

    auto it = mCache.find(materialPath);
    if (it != mCache.end() && it->second.descriptorSet != VK_NULL_HANDLE)
    {
        // 命中：标记本帧被引用（LRU），并比对当前 live instance 的
        // content-hash。一致直接返回旧缩略图；变了入 pending 留 FlushPending
        // 帧外重烘（GetOrRequest 是帧内 UI 路径，禁止 GPU 工作 / 改缓存元数据），
        // 本帧仍返回旧缩略图避免闪烁。
        mReferencedThisFrame.push_back(materialPath);
        if (auto* inst = ::EnsureMaterialInstance(mHost, materialPath); inst != nullptr)
        {
            const std::uint64_t curHash = ComputeContentHash(*inst);
            if (curHash != it->second.contentHash
                && std::find(mPending.begin(), mPending.end(), materialPath)
                       == mPending.end())
            {
                mPending.push_back(materialPath);
            }
        }
        return reinterpret_cast<ImTextureID>(it->second.descriptorSet);
    }

    // 未命中（或缓存里有占位但 set 还没建好）→ 去重入 pending。
    if (std::find(mPending.begin(), mPending.end(), materialPath) == mPending.end())
    {
        mPending.push_back(materialPath);
    }
    return 0;
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
            const std::string path = mPending.front();
            mPending.erase(mPending.begin());
            // BakeThumbnail 无论成功失败都把 path 出队（失败不无限重试，避免每帧
            // 反复烘一个坏 .material 拖慢编辑器；用户改好后下次 GetOrRequest 重入队）。
            BakeThumbnail(path, frameIndex);
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
    // 不在此直接 ReleaseEntry：Invalidate 可能在帧内（Material Save）被调，此刻
    // descriptor set 可能仍被本帧 ImGui draw data 引用，帧内 RemoveTexture 不安全。
    it->second.contentHash = 0;
    // 显式入队（去重），让下一帧 FlushPending 立即重烘。
    if (std::find(mPending.begin(), mPending.end(), materialPath) == mPending.end())
    {
        mPending.push_back(materialPath);
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
    DestroySampler();
}

bool ThumbnailService::BakeThumbnail(const std::string& materialPath,
                                     std::uint64_t      frameIndex)
{
    if (mpPipeline == nullptr) { return false; }
    if (mSampler == VK_NULL_HANDLE) { return false; }

    // 1) 取 / lazy-create 对应 .material 的 live MaterialInstance。
    Orange::Engine::Render::MaterialInstance* inst =
        ::EnsureMaterialInstance(mHost, materialPath);
    if (inst == nullptr) { return false; }

    // 2) 确保 scratch world 就位（首次 bake 建好 camera + dir-light + sphere
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

    // 3) 取 / 建 target RT（96×96 BGRA8，usage = RenderTarget | Sampled）。
    //    复用缓存里已有 entry 的 RT（resize 不变，重烘只是重渲内容）。
    ThumbEntry& entry = mCache[materialPath];
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
            mCache.erase(materialPath);
            return false;
        }
    }

    // 4) 用 viewport Pipeline 把 scratch world 渲到 target RT。
    auto rr = mpPipeline->RenderToTexture(*mpScratchWorld, entry.pRt.get(),
                                          kThumbSize, kThumbSize);
    if (rr.IsErr())
    {
        // 渲染失败：保留 RT（下次重试可复用），但不更新 descriptor set / hash。
        // descriptorSet 仍为旧值（首次失败则 VK_NULL_HANDLE → GetOrRequest 返回 0）。
        return false;
    }

    // 5) 包 descriptor set 供 ImGui 采样。RenderToTexture 成功后 target 处于
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

    // 6) 写缓存元数据。
    entry.contentHash   = ComputeContentHash(*inst);
    entry.lastUsedFrame = frameIndex;
    return true;
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
