#ifndef ORANGE_ENGINE_RENDER_PIPELINE_H
#define ORANGE_ENGINE_RENDER_PIPELINE_H

// ---------------------------------------------------------------------------
// Pipeline —— "World → 一帧画面"的根入口。
//
// 当前仅交付公共面：
//   * 生命周期（默认构造、移动、析构）；
//   * 单一驱动方法 `Render(World&)` —— 后续把 World 翻译为
//     drawable list，用 OrangeRender RenderGraph 真正下发。
//
// 公共头**不**包含任何 OrangeRender / Vulkan 头：依据 CLAUDE.md 的
// "Header isolation" 不变量，`<orange/...>` 只允许出现在
// `src/render/**`。Pipeline 走 PIMPL 把 OrangeRender 的 RHI / RenderGraph
// 类型完全藏在 .cpp 一侧。
//
// `InsertPass` 当前会出现但暂未真正接通；当前不
// 提前 stub 它（避免一个仅 assert(false) 的占位接口污染公共面）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Result.h>
#include <orange/engine/render/IRenderPass.h>

#include <cstdint>
#include <filesystem>
#include <memory>

namespace Orange::Engine
{
class World;
}

namespace Orange::Engine::Asset
{
class AssetRegistry;
}

namespace Orange::Engine::Platform
{
class Window;
}

namespace Orange::Engine::Scene
{
class WorldPartition;
}

// OrangeRender 公共面前向声明 —— 公共头**不**包含 `<orange/...>`，但前向
// 声明指针 / 引用类型是允许的（与 OrangeRender 自家 `VulkanInterop.h` 前
// 向声明 `RHITexture` / `RHICommandList` 同节奏）。消费者真正取用时再
// `#include <orange/renderer/RenderDevice.h>` / `<orange/rhi/RHITexture.h>`。
namespace Orange::Renderer
{
class RenderDevice;
}
namespace Orange::Rhi
{
class RHITexture;
}

namespace Orange::Engine::Render
{

class MaterialSystem;
class PostProcessChain;
class VfxSystem;
struct ShadowConfig;

class ORANGE_ENGINE_API Pipeline
{
public:
    Pipeline();
    ~Pipeline();

    Pipeline(const Pipeline&)            = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    Pipeline(Pipeline&&) noexcept;
    Pipeline& operator=(Pipeline&&) noexcept;

    // 在指定 window 上初始化 OrangeRender 渲染栈：RenderDevice、
    // Renderer（双缓冲 in-flight）、内置 GraphicsPipeline、textured-
    // mesh shader、UploadContext（mesh 顶点 / 索引上传用）。
    //
    // `assets` 用于在 Render() 时按 AssetHandle 反查 MeshAsset 的
    // CPU 数据；其生存期必须长于 Pipeline 的活跃期。重复 Initialize
    // 返回 AlreadyInitialized。`window` 的生存期同样必须长于 Pipeline。
    Result<void, ResultCode> Initialize(::Orange::Engine::Platform::Window&        window,
                                        ::Orange::Engine::Asset::AssetRegistry&    assets);

    // 离屏模式初始化：不绑定 swap-chain，渲染结果落到 Pipeline 内部持有
    // 的 `viewportColor` RT（BGRA8Unorm，SRGB-compatible），调用方通过
    // `GetOffscreenColor()` 拿到 `Orange::Rhi::RHITexture*` 后用
    // `Orange::Renderer::Interop::GetVulkanImageView()` 取 `VkImageView`
    // 喂给 `ImGui_ImplVulkan_AddTexture` —— 这是编辑器 Scene 面板视口的
    // 标准路径。
    //
    // 与 `Initialize(window, ...)` 互斥：同一 Pipeline 实例两条 Initialize
    // 入口只能选一条，第二次（不论同模式还是异模式）返回
    // `AlreadyInitialized`。`device` 借用语义（不取所有权）：调用方负责
    // 保证 device 活到 `Shutdown()` 之前；典型路径是宿主（编辑器）自己
    // 创建 `RenderDevice` 给 ImGui Vulkan backend 用，同一份借给 Pipeline。
    //
    // S1 范围（当前实现）：本路径**只**跑主 pass（含 shadow），bloom /
    // tonemap / godrays / `RequestCapture` / `InsertPass` 一律走 fallback
    // 或 silent-ignore——HDR 域 raw color 经一次 passthrough 直写到
    // viewportColor。若编辑器需要 LDR / tonemap 视觉效果，后续子任务再
    // 把后处理接进来；S1 目标是先把"渲染 → 拿 native view"链路打通。
    //
    // `width × height` 必须 > 0；为 0 返回 `InvalidArgument`。后续运行期
    // 调 `ResizeOffscreen` 改尺寸。
    Result<void, ResultCode> InitializeOffscreen(
        ::Orange::Renderer::RenderDevice&        device,
        ::Orange::Engine::Asset::AssetRegistry&  assets,
        std::uint32_t                            width,
        std::uint32_t                            height);

    // 通知 Pipeline 离屏目标尺寸需要变化（编辑器 Scene 面板 resize 路径）。
    // 与 `OnResize(width, height)` 同语义：标记 dirty，真正重建发生在下一
    // 次 `Render()` 顶部。未 `InitializeOffscreen` 时 no-op。`width × height
    // == 0`（面板折叠）允许，Pipeline 跳过下一帧 Render 的主 pass 直到尺
    // 寸再变正。
    void ResizeOffscreen(std::uint32_t width, std::uint32_t height) noexcept;

    // 当前离屏 final output color RT。仅 `InitializeOffscreen` 模式下且
    // 完成至少一次 `Render()` 后非空；window 模式 / 未 Initialize / 当前
    // 尺寸为 0 → 返回 nullptr。
    //
    // 生命周期：返回的 `RHITexture` 由 Pipeline 拥有，`Shutdown()` 或
    // `ResizeOffscreen` 触发重建时句柄失效——消费者拿到 ImGui descriptor
    // set 后**必须**在每次 resize 时重新调本接口并重新绑 view。
    //
    // 取 `VkImageView` 标准路径（编辑器侧）：
    // ```
    // auto* tex  = pipeline.GetOffscreenColor();
    // auto* view = Orange::Renderer::Interop::GetVulkanImageView(*tex);
    // VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(sampler,
    //     static_cast<VkImageView>(view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    // ImGui::Image(reinterpret_cast<ImTextureID>(ds), ...);
    // ```
    const ::Orange::Rhi::RHITexture* GetOffscreenColor() const noexcept;

    // 释放 OrangeRender 资源。在调用 Window 析构之前必须调用——
    // RenderDevice 会先 WaitIdle 再释放 swap-chain 上挂的资源。
    // 幂等：未初始化或重复调用都是 no-op。
    void Shutdown();

    bool IsInitialized() const noexcept;

    // 通知渲染器宿主窗口可绘制区域发生变化（resize / DPI 切换 / 显示器切
    // 换）。消费者应在 Platform 层的 WindowResizeEvent 路径上调一次。
    // 重建是延迟的（OrangeRender 内部在下一次 BeginFrame 顶部统一执行），
    // 所以调用方不需要担心 thread / frame 状态。
    //
    // 未 Initialize 时 OnResize 是 no-op；framebuffer 0×0（窗口最小化）
    // 同样允许——OrangeRender 那侧会在 extent 退化时跳过重建、把 dirty
    // 标记保留到下次有效 extent 出现。
    void OnResize(std::uint32_t width, std::uint32_t height);

    // 渲染一帧。当前阶段：
    //   * RenderScene 收集（World → drawable list）；
    //   * 接通 OrangeRender RenderGraph：每帧 BeginFrame +
    //     提交一个内置 minimal-mesh draw（gl_VertexIndex 走的硬编码
    //     triangle）+ EndFrame。drawable list 已收集，但当前内置
    //     pipeline 不读其几何——后续把 mesh upload 路径接进
    //     来时再切到按 drawable 驱动。
    //
    // 未 Initialize 时 Render 是 no-op；让"在 main loop 顶层无脑
    // 调一发"成为受支持的退化状态。
    void Render(::Orange::Engine::World& world);

    // 安装 / 卸载 PostProcessChain（非拥有指针；nullptr 走 fallback：
    // chain 缺失时 Pipeline 内置一条 passthrough fullscreen pass，把离
    // 屏 HDR 直接采样到 swap-chain，与 06.02 完成态在 LDR 域字节级一致）。
    // chain 必须活到 Pipeline 析构 / 下次 SetPostProcessChain 之前。
    void SetPostProcessChain(PostProcessChain* chain) noexcept;

    // 安装 / 卸载 MaterialSystem（非拥有指针）。当前阶段（06.02 / 06.03）
    // Pipeline 不消费 system——drawable 直接持 MaterialInstance* 自己路
    // 由；本接口预留给后续子任务（per-frame uniform / shadow descriptor
    // 等需要 system 协作的路径）。nullptr 时 Pipeline 走 fallback：
    // drawable.materialInstance == nullptr 时落到内置 textured Material。
    void SetMaterialSystem(MaterialSystem* system) noexcept;

    // 安装 / 卸载 WorldPartition（非拥有指针）。安装后 Render() 在
    // RenderScene 收集阶段会按 partition 的 layer 可见性过滤 drawable
    // ——隐藏 layer 的实体既不进主 pass 也不进 shadow pass。partition
    // 必须活到 Pipeline 析构 / 下次 SetWorldPartition 之前；nullptr 退
    // 化到"不按 layer 过滤"行为，与 v1.x 之前完全等价。
    void SetWorldPartition(const ::Orange::Engine::Scene::WorldPartition* partition) noexcept;

    // 安装 / 卸载 VfxSystem（非拥有指针）。安装后 Render() 在主 pass 与
    // bloom 之间插一段 instanced additive billboard pass，把 VfxSystem
    // 当前所有 emitter 的粒子绘制到 HDR target——粒子颜色 a > 1 时自动
    // 喂到 bloom。`system` 必须活到 Pipeline 析构 / 下次 SetVfxSystem
    // 之前；nullptr 跳过粒子 pass，sample 不 break。
    //
    // 调用方仍需要自己每帧调 `VfxSystem::Tick(world, dt)` 推进 sim—
    // Pipeline 不接管 sim，避免把 frame budget 耦合到 sim 时序。
    void SetVfxSystem(VfxSystem* system) noexcept;

    // 把游戏侧自定义 IRenderPass 挂到指定 stage 的 hook 点上。Pipeline
    // 立即调一次 pass 的 Setup（让 pass 建 GPU 资源），之后每帧到达该
    // stage 时按 Insert 顺序逐个调 Execute。
    //
    // 同 stage 多次 InsertPass → 按调用顺序排队（第一次 Insert 的 pass
    // 第一个 Execute）。
    //
    // pass 必须非空；nullptr silent-ignore——与 PostProcessChain::AddPass
    // 同行为，避免调用方 forgot-init 时 crash。
    //
    // 详见 docs/extension-points.md §4 与 IRenderPass.h。
    void InsertPass(PipelineStage stage, std::unique_ptr<IRenderPass> pass);

    // 清掉某个 stage 上所有已注册 pass。pass 的 unique_ptr 析构发生在
    // 本调用内（通常会触发 pass 的 destructor 释放 GPU 资源）。
    void RemovePassesAt(PipelineStage stage);

    // 清掉所有 stage 上的所有 pass。Pipeline::Shutdown 会自动做这件事，
    // 调用方一般不需要手动调；提供入口主要给 hot-reload / 编辑器
    // 切换 chain 等场景。
    void ClearInsertedPasses();

    // 当前 stage 上已注册的 pass 数（诊断 / 测试用）。
    std::size_t InsertedPassCount(PipelineStage stage) const noexcept;

    // 请求把下一帧的离屏 HDR 渲染结果落盘成 PNG。下一次 Render() 会在
    // bloom mip-chain 完成、tonemap 之前追加一次 GPU readback 到 host-
    // visible buffer，再在 CPU 端做 ACES Narkowicz tonemap、用 stb_image_write
    // 编 PNG 写到 outPath；写完后请求自动清空（不会重复触发）。
    //
    // 路径覆盖：HDR scene color（含 shadow / 主光衰减），但**不含 bloom**——
    // bloom 在 GPU 端 tonemap pass 内与 HDR 合成，本路径未参与该合成。
    // 这是 debug-only 的"够用"型截图，与屏幕看到的最终画面在 bloom halo
    // 上有差异，但场景结构 / 阴影 / 光照方向完全一致。
    //
    // outPath 父目录必须已存在（不自动 mkdir）；本调用幂等：本帧已经收
    // 到一次请求时第二次调用会覆盖前一个 outPath，仅最后一次生效。
    //
    // 仅 debug 用：本路径引入一次 GPU stall（额外 transition + copy +
    // wait fence）+ 一段 CPU 编码时间（~30 ms / 1280×720 量级），不要
    // 在生产 / release 路径上每帧调。
    void RequestCapture(const std::filesystem::path& outPath);

    // 设置当前帧时间（seconds，单调递增）。Pipeline 把它写进 light UBO
    // 的 uFrameInfo.x，shader 端用于 time-pulse / dissolve / 流光等
    // 跨帧持续效果。调用方应在 Render() 之前每帧调一次（典型路径：
    // RenderLayer::OnUpdate 拿 FrameContext.time.totalSeconds 喂进来）。
    // 未调用时 Pipeline 维持上一次值（构造时为 0）；不会触发 reinit。
    void SetFrameTime(float seconds) noexcept;

    // 安装 shadow 配置（by-value 拷贝）。chain 还没有装载或场景里没
    // DirectionalLight 时仍可调用——配置只在真正跑 shadow pass（Render
    // 看见 castsShadow == true 的 DirectionalLight）时生效；mapResolution
    // 切换会触发 shadow target 重建。未调用时使用 ShadowConfig 的默认
    // 值（1024 / 3×3 PCF / 0.005 depthBias / 0.01 normalBias）。
    void SetShadowConfig(const ShadowConfig& config) noexcept;

    // 当前帧已经按 const Material* 缓存的 RHI Pipeline 数量。Pipeline 在
    // Render() 时对每个 drawable 按其 MaterialInstance 绑定的 Material
    // 路由到一条 RHI Pipeline；同一 Material 多次出现只会编译一次。本
    // 方法主要供 ctest 与诊断用——0.x 阶段允许这种轻量 introspection，
    // 未来引入更正式的 PipelineStats 时会被它替代。
    //
    // 未 Initialize / 尚未渲染过任何 drawable 时返回 0。
    std::size_t TemplatePipelineCount() const noexcept;

    // 当前帧 HDR off-screen target 的尺寸。`width / height` 通过 out
    // 参数返回；未 Initialize / 窗口最小化时两者都置 0。诊断 + ctest 用，
    // 与 TemplatePipelineCount 同语义（轻量 introspection，未来 PipelineStats
    // 落地时被替代）。
    void GetHdrTargetSize(std::uint32_t& width, std::uint32_t& height) const noexcept;

    // 当前 bloom mip-chain 资源数量。chain 不含 BloomPass / 未 Render 过
    // 时返回 0；含 BloomPass 且 EnsureBloomResources 成功后返回 6（与
    // Pipeline 内部 kBloomMipCount 一致）。诊断 + ctest 用。
    std::size_t BloomMipCount() const noexcept;

    // 第 mipIndex 张 bloom mip 的尺寸（mipIndex < BloomMipCount() 时
    // 有效）；越界返回 (0, 0)。诊断 + ctest 用。
    void GetBloomMipSize(std::size_t mipIndex,
                         std::uint32_t& width,
                         std::uint32_t& height) const noexcept;

private:
    // 共享 RHI 资源创建逻辑（sampler / passthrough / bloom / tonemap /
    // godrays / 主 pass UBO / shadow caster pipeline / offscreen cmd list）。
    // Initialize / InitializeOffscreen 两条入口都调它；调用前必须保证
    // `mpImpl->renderDevice` + `mpImpl->upload` + `mpImpl->assets` 已就绪。
    // 失败时内部调 `Shutdown()` 整体回滚。
    Result<void, ResultCode> SetupRhiResources();

    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_PIPELINE_H
