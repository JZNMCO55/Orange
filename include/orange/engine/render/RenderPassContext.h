#ifndef ORANGE_ENGINE_RENDER_RENDER_PASS_CONTEXT_H
#define ORANGE_ENGINE_RENDER_RENDER_PASS_CONTEXT_H

// ---------------------------------------------------------------------------
// RenderPassContext / RenderGraphBuilder —— IRenderPass 与 Pipeline 之
// 间的隔离层。
//
// 设计意图（与 IPostProcessPass 的两个 context 对照一致）：让 IRenderPass
// 接公共 API 的实现端拿到所有需要的 RHI 句柄，但**公共头本身不感染
// `<orange/...>` 头**——OrangeRender RHI / Renderer 类型只用前向声明
// 引用，pass 实现端在 .cpp 内 `#include` 真头去调成员。
//
// `RenderGraphBuilder` 当前是一个占位接口：Read / Write 声明 pass 对资源
// 的依赖。0.x Pipeline 忽略这些声明（沿用手写 transition 模式），但
// 公共面留出来让 game pass 写 setup 时就按"声明依赖"风格组织代码——
// 将来真接 RenderGraph 自动调度时不破调用方。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstdint>

// OrangeRender / RHI 前向声明——本头不引入实际头，让 IRenderPass 派
// 生类 / IPostProcessPass / Pipeline 各自的实现 TU 自管 #include。
namespace Orange::Rhi
{
class RHIDevice;
class RHITextureView;
class RHICommandList;
class RHIDescriptorPool;
}  // namespace Orange::Rhi

namespace Orange::Renderer
{
class IRenderer;
}  // namespace Orange::Renderer

namespace Orange::Engine::Render
{

// Setup 阶段的资源依赖声明器。0.x 实现是 no-op——pass 调 Read / Write
// 不会触发 Pipeline 任何动作。但写代码时仍按"声明依赖"风格走，等到
// 真接 RenderGraph 自动调度时同一段 pass setup 代码不需要重写。
//
// `kind` 字段标识 Setup 触发原因：首次 InsertPass / HDR resize / 显式
// 重 Setup —— pass 自己用 kind 区分"分配长寿命资源"vs"刷新 per-resolution
// 资源"两种工作。
class ORANGE_ENGINE_API RenderGraphBuilder
{
public:
    enum class SetupKind : std::uint8_t
    {
        Initial,         // InsertPass 后首次 Setup
        Resize,          // HDR target / swap-chain 重建后重 Setup
        Manual,          // 调用方显式触发（debug / hot-reload）
    };

    RenderGraphBuilder() = default;

    // game pass 在 Setup 内通过这两个指针拿到 RHI 句柄建自己的
    // GraphicsPipeline / DescriptorSet / 长寿命 buffer。Pipeline 在调
    // Setup 之前给 builder 填好它们；pass 不应缓存指针跨过 Setup 调用
    // 的范围（resize 时 device 不变但 renderer 状态可能变）。
    Orange::Renderer::IRenderer* pRenderer{nullptr};
    Orange::Rhi::RHIDevice*      pDevice  {nullptr};

    SetupKind Kind() const noexcept { return mKind; }
    void      SetKind(SetupKind k) noexcept { mKind = k; }

    // 资源依赖声明——0.x Pipeline 忽略，仅保留 API 形态。`opaque` 是
    // 调用方对资源的不透明标识符（pass 自己定义；通常是指针 / 整数
    // 句柄）。
    void Read(const void* /*opaqueHandle*/) noexcept {}
    void Write(const void* /*opaqueHandle*/) noexcept {}

private:
    SetupKind mKind{SetupKind::Initial};
};

// Execute 阶段每帧上下文。各字段由 Pipeline 在调 IRenderPass::Execute
// 之前填好。AfterShadow / AfterMainPass 阶段 `pCmdList` 是 Pipeline 的
// offscreenCmd（pass 内部走 `BeginRendering / Bind / Draw / EndRendering`
// 直接录制）；AfterPostProcess 阶段 `pCmdList` 为 nullptr，pass 通过
// `pRenderer->SubmitItem` 提交 fullscreen item。
class ORANGE_ENGINE_API RenderPassContext
{
public:
    Orange::Renderer::IRenderer*    pRenderer       {nullptr};
    Orange::Rhi::RHIDevice*         pDevice         {nullptr};

    // AfterShadow / AfterMainPass 阶段非 nullptr；AfterPostProcess 阶段
    // 为 nullptr（pass 走 SubmitItem 路径）。
    Orange::Rhi::RHICommandList*    pCmdList        {nullptr};

    // HDR scene color 默认 view（RGBA16F）。AfterShadow / AfterMainPass
    // 阶段 Pipeline 已经把 hdrColor transition 到合适状态：
    //   * AfterShadow      —— Undefined / DepthStencilAttachment（pass
    //                          想写 HDR 需自己 transition + Begin/End）
    //   * AfterMainPass    —— ColorAttachment（pass 在 BeginRendering 段
    //                          内继续录，或 End 后再 Begin 写其它 target）
    //   * AfterPostProcess —— nullptr（swap-chain 已收尾，HDR 不再可写）
    Orange::Rhi::RHITextureView*    pHdrColorView   {nullptr};

    // 主 pass 的 sceneDepth view（D32Float）。0.x 仅 AfterMainPass 阶段
    // 有效（depth 已写完且即将给 godrays 采样）；其它阶段 nullptr。
    Orange::Rhi::RHITextureView*    pSceneDepthView {nullptr};

    // 共享 descriptor pool —— pass 内部允许从这里 AllocateDescriptorSet。
    // Pipeline 不强制 pass 使用这个池；pass 也可以自己建池。
    Orange::Rhi::RHIDescriptorPool* pSharedPool     {nullptr};

    // 主相机的 viewProj 矩阵，列主序 16 个 float。pass 复用主 pass 的
    // 视角投影；想用别的视角的 pass 自己计算。
    const float*                    pViewProjData   {nullptr};

    // 当前 HDR target / swap-chain 的渲染区像素尺寸。
    std::uint32_t                   width           {0};
    std::uint32_t                   height          {0};

    // 单调递增帧序号——pass 用作 ring-buffer 索引、debug label。
    std::uint64_t                   frameIndex      {0};
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_RENDER_PASS_CONTEXT_H
