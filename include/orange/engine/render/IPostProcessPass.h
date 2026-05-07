#ifndef ORANGE_ENGINE_RENDER_I_POST_PROCESS_PASS_H
#define ORANGE_ENGINE_RENDER_I_POST_PROCESS_PASS_H

// ---------------------------------------------------------------------------
// IPostProcessPass —— 后处理链中单个 pass 的抽象接口。
//
// 与 docs/extension-points.md 写明的 IRenderPass 路径同思路（Phase 5
// RenderGraph 注入）：每个 pass 在 `Setup` 阶段声明它要读 / 写哪些
// resource、在 `Execute` 阶段录制实际绘制命令。后处理是 RenderPass 的
// 子集——通常是 fullscreen quad 读上一阶段的 color、写下一阶段的 color。
//
// **当前阶段（Phase 3 / Task 03）的 context 是空 struct**：Pipeline 真
// 跑后处理链需要 OrangeRender 暴露 off-screen render target / descriptor
// set 路径，那是后续 task。Task 03 仅交付接口与默认链描述，签名提前
// 定下来——Phase 5 RenderGraph 接通时给 context 补字段（`RenderGraphBuilder&`
// / `Orange::Renderer::IRenderer&` / 输入输出 texture view 等）即可，
// 子类签名不破。
//
// 子类（HdrPass / BloomPass / TonemapPass / LutPass）当前的 Setup /
// Execute 是空 stub，但 Name() 与每类各自的参数字段已经稳定可用。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

namespace Orange::Engine::Render
{

// Setup 阶段的 context。Phase 5 RenderGraph 接通时追加：
//   * RenderGraphBuilder& builder —— 用来声明 pass 的 read / write resource；
//   * 上下游 pass 的 resource handle，让 chain 内 pass 之间能 wire 起来。
struct PostProcessSetupContext
{
};

// Execute 阶段的 context。Pipeline 真跑时追加：
//   * Orange::Renderer::IRenderer& renderer —— SubmitItem / SubmitView 入口；
//   * 当前帧 input / output texture view、uniform、frame index 等运行
//     时数据。
struct PostProcessExecuteContext
{
};

class ORANGE_ENGINE_API IPostProcessPass
{
public:
    virtual ~IPostProcessPass() = default;

    // 链上唯一标识 + debug 名。用于 `PostProcessChain::FindByName` 与
    // 日志 / profiling label。同链上多个 pass 同名是允许但不推荐——
    // FindByName 命中第一个。
    virtual const char* Name() const noexcept = 0;

    // 在 chain 装载时调用一次（Pipeline 真跑后处理链的 task 起）。Phase
    // 3 / Task 03 阶段不会被任何调用方驱动，子类给空实现即可。
    virtual void Setup(PostProcessSetupContext& ctx) = 0;

    // 在每帧渲染时调用。同 Setup，Task 03 阶段不会被驱动，子类空实现。
    virtual void Execute(PostProcessExecuteContext& ctx) = 0;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_I_POST_PROCESS_PASS_H
