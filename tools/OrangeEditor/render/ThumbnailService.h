#ifndef ORANGE_EDITOR_RENDER_THUMBNAIL_SERVICE_H
#define ORANGE_EDITOR_RENDER_THUMBNAIL_SERVICE_H

// ThumbnailService —— Asset Browser 里 .material / .prefab.json 的"渲染缩略图"服务。
//
// 职责：把任意 .material 应用到内置 sphere mesh（材质球）、或把任意 .prefab.json
// 实例化到 scratch world（prefab 预览），用编辑器 viewport 的同一个 Pipeline 实例
// 渲到一张 96×96 离屏 RT，再经 ImGui_ImplVulkan_AddTexture 包成 ImTextureID 供
// Asset Browser 的 ImGui::Image 显示，替代原 "[Mat]" / "[Prefab]" 文本 icon。
//
// 两类缩略图（ThumbKind）共用同一套 lazy / 缓存 / LRU / descriptor 机制 + 公共
// 烘焙尾段 FinalizeBake（RT 取建 + RenderToTexture + AddTexture + 元数据写入）；
// 仅"如何构造被渲染的 scratch world"与"content hash 算法"按 kind 分派。
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

    // 缩略图种类 —— 决定"如何构造被渲染的 scratch world"与"content hash 源"。
    // Material：材质球（sphere + .material instance）；Prefab：prefab 实例化预览；
    // Mesh：DCC 导入的 .mesh 用默认材质渲染的单 mesh 预览；Scene：整张 .scene.json
    // 加载到 scratch world 后框相机渲染的场景快照预览（snapshot）。
    enum class ThumbKind
    {
        Material,
        Prefab,
        Mesh,
        Scene,
    };

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

        // prefab 缩略图入口（与上面 material 入口对位）。实例化 .prefab.json 到
        // scratch world → 算 AABB 框相机 → RenderToTexture → 缓存。content-hash
        // 源是 PrefabAsset 的 templateBlob（编辑或重存后会变 → 自动重烘）。
        ImTextureID GetOrRequestPrefabThumbnail(const std::string& prefabPath);

        // mesh 缩略图入口（与上面对位）。Load<MeshAsset> → 用默认 PBR 材质渲到
        // mesh-scratch world → 算单 mesh local AABB 框相机 → RenderToTexture →
        // 缓存。content-hash 源是 mesh 路径的 FNV（mesh 文件内容变化罕见，路径 hash
        // 足够；mesh 缩略图 invalidate 需求低）。
        ImTextureID GetOrRequestMeshThumbnail(const std::string& meshPath);

        // scene snapshot 缩略图入口（与上面对位，第四类 / 最后一类）。读 .scene.json
        // 文件内容 → LoadFromString 追加进 scene-scratch world → 遍历 (Transform,
        // Renderable) 算合并 world AABB 框相机 → RenderToTexture → 缓存 → 清掉本次
        // 加载的全部实体（多根，RAII 守卫遍历 created 列表 DestroySubtree）。content
        // -hash 源是 scene 文件内容的 FNV（文件内容变了 → hash 变 → 自动重烘）。
        ImTextureID GetOrRequestSceneThumbnail(const std::string& scenePath);

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
            // 该条目属于哪类缩略图——命中时按它选 content hash 源，bake 时分派。
            ThumbKind kind{ThumbKind::Material};
        };

        // 待烘队列元素：path + kind。material / prefab 共用同一队列，FlushPending
        // 帧外按 kind 分派到对应 bake 路径。
        struct PendingItem
        {
            std::string path;
            ThumbKind   kind{ThumbKind::Material};
        };

        // 自建 LINEAR / CLAMP sampler（复用 ScenePanel 的 sampler 创建逻辑）。
        void CreateSampler();
        void DestroySampler();

        // material / prefab 两个 GetOrRequest 入口的公共实现：命中比对（按 kind 取
        // content hash 源）→ 一致返回旧 ImTextureID，变了入 pending；未命中入 pending
        // 返回 0。把 kind 写进 entry / pending，FlushPending 据此分派烘焙路径。
        ImTextureID RequestThumbnail(const std::string& path, ThumbKind kind);

        // mPending 里是否已含 path（按 path 去重，与 kind 无关——同 path 只可能一类）。
        bool IsPending(const std::string& path) const;

        // 按 entry.kind 分派烘焙：Material → BakeMaterialThumbnail；Prefab →
        // BakePrefabThumbnail。两者构造各自的 scratch world 后都收敛到 FinalizeBake。
        // 返回 false 表示本次 bake 失败，调用方据此把 path 从 pending 移除（不无限重试）。
        bool BakeThumbnail(const std::string& path, ThumbKind kind, std::uint64_t frameIndex);

        // 材质球烘焙：取 EnsureMaterialInstance → 复用 / 建材质 scratch world →
        // FinalizeBake。
        bool BakeMaterialThumbnail(const std::string& materialPath, std::uint64_t frameIndex);

        // prefab 烘焙：Load<PrefabAsset> → 实例化到 prefab scratch world → 算 AABB
        // 框相机 → FinalizeBake → DestroySubtree 清实例（RAII 守卫保证任何退出路径
        // 都清理，避免实例残留污染下次 AABB）。
        bool BakePrefabThumbnail(const std::string& prefabPath, std::uint64_t frameIndex);

        // mesh 烘焙：Load<MeshAsset> → 复用 mesh scratch world（常驻 camera + light +
        // 单个 renderable，每次只换 rc.mesh，material 固定为默认 PBR）→ 算单 mesh
        // local AABB（identity transform）框相机 → FinalizeBake。mesh scratch 不像
        // prefab 那样实例化新子树，renderable 常驻，无需 DestroySubtree。
        bool BakeMeshThumbnail(const std::string& meshPath, std::uint64_t frameIndex);

        // scene snapshot 烘焙：读 .scene.json 文件内容 → LoadFromString 把整张场景
        // 追加进 scene scratch world（常驻 camera + dir-light 兜底）→ 算合并 world
        // AABB 框相机 → FinalizeBake → RAII 守卫遍历本次 created 实体逐个
        // DestroySubtree 清干净（scene 是多根，不像 prefab 单根）。任何退出路径都
        // 清理，否则下次 bake 累积污染 AABB。content hash = scene 文件内容 FNV。
        bool BakeSceneThumbnail(const std::string& scenePath, std::uint64_t frameIndex);

        // 公共烘焙尾段（material / prefab 共用）：取 / 建 entry.pRt → RenderToTexture
        // 渲 scratchWorld 到该 RT → AddTexture 包 descriptor set → 写缓存元数据
        // （contentHash / lastUsedFrame / kind）。返回 false 表示 RT 建失败 / 渲染失败
        // / descriptor 包失败。
        bool FinalizeBake(ThumbEntry&            entry,
                          const std::string&     path,
                          Orange::Engine::World& scratchWorld,
                          std::uint64_t          frameIndex,
                          std::uint64_t          contentHash,
                          ThumbKind              kind);

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
        VkDescriptorPool                  mImguiDescriptorPool; // 仅记录，AddTexture 不吃
        Orange::Engine::Render::Pipeline* mpPipeline{nullptr};  // 非拥有，layer 注入

        VkSampler mSampler{VK_NULL_HANDLE};

        std::unordered_map<std::string, ThumbEntry> mCache;

        // 待烘队列（按入队顺序）。RequestThumbnail 未命中时 push（按 path 去重）；
        // FlushPending 帧外逐个 pop 并按 kind 分派。
        std::vector<PendingItem> mPending;

        // 本帧被 GetOrRequestThumbnail 命中（即 Asset Browser 正在显示）的 path
        // 集合。FlushPending 帧外把这些条目的 lastUsedFrame 推到当前帧——LRU 据
        // 此准确反映"UI 真在引用"，频繁显示的缩略图不会被误淘汰。FlushPending 末
        // 尾清空。命中不在帧内直接改 lastUsedFrame：GetOrRequest 无 frameIndex，
        // 且帧内不碰缓存元数据更省心。
        std::vector<std::string> mReferencedThisFrame;

        // 可复用的材质球 scratch world —— 每次 bake 只换 entity 的 materialInstance
        // 指针，免重建 camera / light / entity（择简：单 entity，按需建好）。
        std::unique_ptr<Orange::Engine::World> mpScratchWorld;
        // scratch world 内承载 sphere + material 的那个 entity（首次 bake 时建好）。
        Orange::Engine::Entity mScratchRenderableEntity{};
        bool                   mScratchBuilt{false};

        // 可复用的 prefab scratch world —— 持有常驻 camera + dir-light entity。每次
        // prefab bake 把 prefab 实例化进来（新增若干实体）、算 AABB 摆相机、渲完后
        // DestroySubtree 清掉实例，只留 camera + light（下次 bake 复用）。相机姿态
        // 每次按当前 prefab AABD 重设（写 mPrefabCameraEntity 的 Camera 组件）。
        std::unique_ptr<Orange::Engine::World> mpPrefabScratchWorld;
        Orange::Engine::Entity                 mPrefabCameraEntity{};
        bool                                   mPrefabScratchBuilt{false};

        // 可复用的 mesh scratch world —— 持有常驻 camera + dir-light + 单个 renderable
        // entity。每次 mesh bake 只换 renderable 的 rc.mesh 指针（material 固定为默认
        // PBR）、按当前 mesh 的 local AABB 重摆相机。与材质球 scratch 模式同构（复用、
        // 只换指针），区别在于 mesh 可变、material 固定，而材质球反之。
        std::unique_ptr<Orange::Engine::World> mpMeshScratchWorld;
        Orange::Engine::Entity                 mMeshCameraEntity{};
        Orange::Engine::Entity                 mMeshRenderableEntity{};
        bool                                   mMeshScratchBuilt{false};

        // 可复用的 scene scratch world —— 持有常驻 camera + dir-light entity（兜底
        // 光，scene 自带 light 也会一起渲染）。每次 scene bake 把整张 .scene.json
        // LoadFromString 追加进来（多根，新增若干实体）、算合并 AABB 摆相机、渲完后
        // 遍历本次 created 列表逐个 DestroySubtree 清掉，只留常驻 camera + light
        // （下次 bake 复用）。相机姿态每次按当前 scene AABB 重设。
        std::unique_ptr<Orange::Engine::World> mpSceneScratchWorld;
        Orange::Engine::Entity                 mSceneCameraEntity{};
        bool                                   mSceneScratchBuilt{false};
    };

} // namespace Orange::Editor::Render

#endif // ORANGE_EDITOR_RENDER_THUMBNAIL_SERVICE_H
