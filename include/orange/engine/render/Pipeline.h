#ifndef ORANGE_ENGINE_RENDER_PIPELINE_H
#define ORANGE_ENGINE_RENDER_PIPELINE_H

// ---------------------------------------------------------------------------
// Pipeline —— "World → 一帧画面"的根入口。
//
// 当前 task 仅交付公共面：
//   * 生命周期（默认构造、移动、析构）；
//   * 单一驱动方法 `Render(World&)` —— 后续 Task 06 把 World 翻译为
//     drawable list，Task 07 用 OrangeRender RenderGraph 真正下发。
//
// 公共头**不**包含任何 OrangeRender / Vulkan 头：依据 CLAUDE.md 的
// "Header isolation" 不变量，`<orange/...>` 只允许出现在
// `src/render/**`。Pipeline 走 PIMPL 把 OrangeRender 的 RHI / RenderGraph
// 类型完全藏在 .cpp 一侧。
//
// `InsertPass` 在 Phase 3 起会出现，Phase 5 才真正接通；当前 task 不
// 提前 stub 它（避免一个仅 assert(false) 的占位接口污染公共面）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Result.h>

#include <cstdint>
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

namespace Orange::Engine::Render
{

class MaterialSystem;
class PostProcessChain;
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

    // 渲染一帧。Phase 2 当前阶段：
    //   * Task 06 实现 RenderScene 收集（World → drawable list）；
    //   * Task 07 接通 OrangeRender RenderGraph：每帧 BeginFrame +
    //     提交一个内置 minimal-mesh draw（gl_VertexIndex 走的硬编码
    //     triangle）+ EndFrame。drawable list 已收集，但当前内置
    //     pipeline 不读其几何——后续 task 把 mesh upload 路径接进
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
    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_PIPELINE_H
