#ifndef ORANGE_EDITOR_RENDER_THUMBNAIL_SERVICE_H
#define ORANGE_EDITOR_RENDER_THUMBNAIL_SERVICE_H

// ThumbnailService —— Asset Browser 里 .material 的"材质球缩略图"服务。
//
// 职责：把任意 .material 应用到内置 sphere mesh，用编辑器 viewport 的同一个
// Pipeline 实例渲到一张 96×96 离屏 RT，再经 ImGui_ImplVulkan_AddTexture 包成
// ImTextureID 供 Asset Browser 的 ImGui::Image 显示，替代原 "[Mat]" 文本 icon。
//
// 设计要点（schema-first，挂在 EditorHost 作全局编辑器服务，与 audioEngine 同
// 位）：
//   * lazy bake：只有 Asset Browser 真的要画某个 .material 时才入 pending 队
//     列；FlushPending 在帧外安全点逐个烘（≤ maxPerFrame，避免一帧烘几十张
//     GPU stall）。
//   * per-session 缓存：path → ThumbEntry（RT + descriptor set + content hash
//     + lastUsedFrame）。同一份 .material 只烘一次，命中直接返回 ImTextureID。
//   * content-hash invalidate（主路径）：bake 时对 MaterialInstance 的
//     templateName + 各 uniform override 值算轻量 hash 存进缓存；命中后比对当
//     前 instance hash，变了重烘。不依赖任何 UI 信号，对 DnD / Inspector 调参
//     / 外部改盘都鲁棒。
//   * LRU + 帧限额：缓存 cap kCacheCap，超出时按 lastUsedFrame 淘汰非本帧引用
//     的条目，淘汰 / invalidate 调 ImGui_ImplVulkan_RemoveTexture 释放
//     descriptor set（仅在帧外 FlushPending 内做，安全）。
//
// 复用编辑器 viewport 的同一个 Pipeline（layer 经 SetPipeline 注入），调
// Pipeline::RenderToTexture(world, rt, 96, 96)——该 API 用独立 scratch，不影响
// viewport 的 GetOffscreenColor 缓存（OE be0bb24）。Pipeline 为 nullptr 时
// （未就绪 / Pipeline reset）pending 不烘，GetOrRequestThumbnail 返回 0 让调用
// 方回退文本 icon。
//
// header isolation 说明：本文件位于 tools/OrangeEditor/，不在引擎 src/render/
// 的 header-isolation 约束内——与 ScenePanel.cpp 一致地直接消费 Vulkan +
// ImGui + Orange::Renderer::Interop 公共面。

#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/scene/World.h>

#include <orange/renderer/RenderDevice.h>
#include <orange/rhi/RHITexture.h>

#include <imgui.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct EditorHost;

namespace Orange::Engine::Render
{
class Pipeline;
}

namespace Orange::Editor::Render
{

class ThumbnailService
{
public:
    // 缩略图边长（正方形）。96 是 Asset Browser 列表行可接受的预渲尺寸——
    // ImGui::Image 实际按 64×64 显示（缩小采样更清晰），留余量给将来更大网格视图。
    static constexpr std::uint32_t kThumbSize = 96u;

    // 缓存上限。超出时 FlushPending 内按 LRU 淘汰非本帧引用的条目。
    static constexpr std::size_t kCacheCap = 96u;

    // host：调 EnsureMaterialInstance + 读 sphere mesh handle。
    // device：建 target RT（GetRhiDevice().CreateTexture）。
    // imguiDescriptorPool：与 main / ScenePanel 共享的 ImGui descriptor pool
    //   （ImGui_ImplVulkan_AddTexture 内部从它分配 set；这里仅持有作记录，
    //    AddTexture 不显式吃 pool 参数）。
    ThumbnailService(EditorHost&                     host,
                     Orange::Renderer::RenderDevice& device,
                     VkDescriptorPool                imguiDescriptorPool);
    ~ThumbnailService();

    ThumbnailService(const ThumbnailService&)            = delete;
    ThumbnailService& operator=(const ThumbnailService&) = delete;

    // layer 注入 / 清空 viewport Pipeline。nullptr 时 pending 不烘（GetOrRequest
    // 仍可命中已有缓存返回 ImTextureID；只是不再产出新缩略图）。
    void SetPipeline(Orange::Engine::Render::Pipeline* pipeline) noexcept;

    // 命中且 content-hash 一致 → 返回可直接喂 ImGui::Image 的 ImTextureID；
    // 未命中 / hash 变化 → 入 pending 队列返回 0（调用方回退文本 icon）。
    // 命中时顺带刷新 lastUsedFrame（LRU），所以本帧引用的条目不会被淘汰。
    ImTextureID GetOrRequestThumbnail(const std::string& materialPath);

    // 帧外安全点调用（viewport Render 已 WaitIdle、ImGui 未提交、引擎 BeginFrame
    // 未开始）：取 ≤ maxPerFrame 个 pending 逐个 bake；顺带做 LRU 淘汰。
    // frameIndex 用于 lastUsedFrame 记账 + "只淘汰非本帧引用"判定。
    void FlushPending(std::uint64_t frameIndex, int maxPerFrame = 3);

    // 主动失效某 path（Material Save 后调，content-hash 下属优化——hash 比对
    // 本就会逮到变化，但显式 invalidate 让下一帧立刻重烘而不等 hash 再算一遍）。
    void Invalidate(const std::string& materialPath);

    // 释放全部 RT + descriptor set。main 关停期 / device WaitIdle 之后调。
    void Shutdown();

private:
    struct ThumbEntry
    {
        std::unique_ptr<Orange::Rhi::RHITexture> pRt;
        VkDescriptorSet                          descriptorSet{VK_NULL_HANDLE};
        std::uint64_t                            contentHash{0};
        std::uint64_t                            lastUsedFrame{0};
    };

    // 自建 LINEAR / CLAMP sampler（复用 ScenePanel 的 sampler 创建逻辑）。
    void CreateSampler();
    void DestroySampler();

    // 对 path 烘一张缩略图（取 EnsureMaterialInstance → 建 / 取 RT →
    // RenderToTexture → AddTexture → 写缓存）。返回 false 表示本次 bake 失败
    // （Pipeline 缺失 / instance 缺失 / mesh 无效 / RT 建失败 / Render Err），
    // 调用方据此把 path 从 pending 移除（失败不无限重试）。
    bool BakeThumbnail(const std::string& materialPath, std::uint64_t frameIndex);

    // 释放单条 entry 的 descriptor set + RT（帧外调用，安全）。
    void ReleaseEntry(ThumbEntry& entry);

    // LRU 淘汰：缓存超 cap 时丢弃 lastUsedFrame 最旧、且非本帧引用的条目。
    void EvictIfNeeded(std::uint64_t frameIndex);

    // 对一个 MaterialInstance 的 templateName + 全部 uniform override 值算
    // 轻量 64-bit FNV-1a hash —— content-hash invalidate 的核心。
    static std::uint64_t ComputeContentHash(
        const Orange::Engine::Render::MaterialInstance& instance);

    EditorHost&                       mHost;
    Orange::Renderer::RenderDevice&   mDevice;
    VkDescriptorPool                  mImguiDescriptorPool;  // 仅记录，AddTexture 不吃
    Orange::Engine::Render::Pipeline* mpPipeline{nullptr};   // 非拥有，layer 注入

    VkSampler                         mSampler{VK_NULL_HANDLE};

    std::unordered_map<std::string, ThumbEntry> mCache;

    // 待烘队列（按入队顺序）。GetOrRequestThumbnail 未命中时 push（去重）；
    // FlushPending 帧外逐个 pop。
    std::vector<std::string> mPending;

    // 本帧被 GetOrRequestThumbnail 命中（即 Asset Browser 正在显示）的 path
    // 集合。FlushPending 帧外把这些条目的 lastUsedFrame 推到当前帧——LRU 据
    // 此准确反映"UI 真在引用"，频繁显示的缩略图不会被误淘汰。FlushPending 末
    // 尾清空。命中不在帧内直接改 lastUsedFrame：GetOrRequest 无 frameIndex，
    // 且帧内不碰缓存元数据更省心。
    std::vector<std::string> mReferencedThisFrame;

    // 可复用的 scratch world —— 每次 bake 只换 entity 的 materialInstance 指针，
    // 免重建 camera / light / entity（择简：单 entity，BakeThumbnail 内按需建好）。
    std::unique_ptr<Orange::Engine::World> mpScratchWorld;
    // scratch world 内承载 sphere + material 的那个 entity（首次 bake 时建好）。
    Orange::Engine::Entity mScratchRenderableEntity{};
    bool                   mScratchBuilt{false};
};

}  // namespace Orange::Editor::Render

#endif  // ORANGE_EDITOR_RENDER_THUMBNAIL_SERVICE_H
