#ifndef ORANGE_ENGINE_RENDER_I_POST_PROCESS_PASS_H
#define ORANGE_ENGINE_RENDER_I_POST_PROCESS_PASS_H

// ---------------------------------------------------------------------------
// IPostProcessPass —— 后处理链中单个 pass 的抽象接口。
//
// 与 docs/extension-points.md 写明的 IRenderPass 路径同思路：每个 pass
// 在 `Setup` 阶段声明 / 拿到要读 / 写的 resource、在 `Execute` 阶段录制
// 实际绘制命令。后处理是 RenderPass 的子集——通常是 fullscreen quad 读
// 上一阶段的 color、写下一阶段的 color。
//
// **当前阶段的 context 已带骨架字段**：双段 frame
// 流程把 HDR off-screen view 与 swap-chain 收尾路径都暴露给 pass；
// Pipeline 在每帧调 Setup 一次（chain 装载或 HDR target 重建后）+ Execute
// 一次（按 chain 顺序逐 pass）。
//
// 子类（HdrPass / BloomPass / TonemapPass / LutPass）当前的 Setup /
// Execute 仍是空 stub，由后续逐步填实——本期
// 只把 context 字段定下来，子类签名不破。
//
// 头文件隔离约束：本头不 #include `<orange/...>`——RHI / Renderer 的类
// 型只用前向声明引用。游戏侧 / 引擎侧 src 实现 pass 时再 #include 完整
// 头去调成员；公共面只承担 "type tag" 的角色。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstdint>

// ---------------------------------------------------------------------------
// OrangeRender / RHI 前向声明 —— 不引入实际头，保持引擎公共面对
// `<orange/...>` 的隔离。需要调成员的实现 TU 自己 #include 完整头。
// ---------------------------------------------------------------------------
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

// Setup 阶段的 context。Pipeline 在 chain 装载或 HDR target 重建后调一
// 次，让 pass 拿到长寿命资源（Pipeline-managed HDR view、swap-chain 写
// 出 view、共享 device 与 renderer 引用）做 pipeline / descriptor 预编。
// 字段值由 Pipeline 实时填——pass 别假设跨帧稳定，每次 Setup 都按当前
// context 重建依赖于 view 指针的 descriptor。
struct PostProcessSetupContext
{
    Orange::Renderer::IRenderer* pRenderer       {nullptr};
    Orange::Rhi::RHIDevice*      pDevice         {nullptr};

    // 离屏 HDR scene color 的默认 view（RGBA16F）。Pipeline 主 pass 已
    // 在调用 Execute 前把它 transition 到 ShaderReadOnly。
    Orange::Rhi::RHITextureView* pHdrColor       {nullptr};

    // swap-chain 颜色目标的 view。当前 OrangeRender 公共面尚未 RHI 化
    // swap-chain（见 vendor/OrangeRender docs/rhi_audit.md A1），这条字
    // 段在 Pipeline 接通直 RHI 路径之前为 nullptr——chain 收尾的 pass 走
    // `IRenderer::SubmitItem` 让 OrangeRender 在它私有的 swap-chain pass
    // 内代发 fullscreen draw（pRenderer 即入口），不直接消费 view 指针。
    Orange::Rhi::RHITextureView* pSwapchainColor {nullptr};
};

// Execute 阶段的 context。Pipeline 每帧按 chain 顺序逐 pass 调一次。中
// 间离屏 pass（bloom 各 mip 跳）通过 `offscreenCmd` 录制 `BeginRendering /
// Draw* / EndRendering`；最终 pass（tonemap 写回 swap-chain）通过
// `pRenderer->SubmitItem(...)` 走 `IRenderer` 路径——这与 §6.6 文档的
// 路径取舍对照一致。
struct PostProcessExecuteContext
{
    Orange::Renderer::IRenderer*  pRenderer       {nullptr};
    Orange::Rhi::RHIDevice*       pDevice         {nullptr};

    // Pipeline 为本帧准备好的离屏 cmd list。pass 在中间链路时通过它录
    // 制 BeginRendering / Bind / Draw / EndRendering。Pipeline 在帧末
    // 统一 Submit + WaitIdle（0.x 阶段），pass 不要自己 Submit / End。
    Orange::Rhi::RHICommandList*  pOffscreenCmd   {nullptr};

    Orange::Rhi::RHITextureView*  pHdrColor       {nullptr};
    Orange::Rhi::RHITextureView*  pSwapchainColor {nullptr};

    // Pipeline 为后处理链共享的 descriptor pool——pass 内部允许从这里
    // AllocateDescriptorSet 取 set。Pipeline 负责池容量足够（按 chain
    // 内 pass 总需求量 + 帧内复用安全余量预算）。
    Orange::Rhi::RHIDescriptorPool* pSharedPool   {nullptr};

    // 本帧序号（与 Pipeline 内部 frameIndex 一致），pass 用作 ring-buffer
    // 索引、debug label 等用途。
    std::uint64_t                  frameIndex     {0};
};

class ORANGE_ENGINE_API IPostProcessPass
{
public:
    virtual ~IPostProcessPass() = default;

    // 链上唯一标识 + debug 名。用于 `PostProcessChain::FindByName` 与
    // 日志 / profiling label。同链上多个 pass 同名是允许但不推荐——
    // FindByName 命中第一个。
    virtual const char* Name() const noexcept = 0;

    // 在 chain 装载 / HDR target 重建后调用一次。当前
    // 4 个内置 pass 仍是空 stub，子类按需重写。
    virtual void Setup(PostProcessSetupContext& ctx) = 0;

    // 每帧渲染时调用，按 chain 顺序触发。
    virtual void Execute(PostProcessExecuteContext& ctx) = 0;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_I_POST_PROCESS_PASS_H
