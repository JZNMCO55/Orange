#ifndef ORANGE_ENGINE_RENDER_I_RENDER_PASS_H
#define ORANGE_ENGINE_RENDER_I_RENDER_PASS_H

// ---------------------------------------------------------------------------
// IRenderPass —— 游戏侧"不修改引擎源码地往 Pipeline 编排里插一段自定
// 义渲染"的扩展点（详见 docs/extension-points.md §4）。
//
// 与 IPostProcessPass 的区别：
//   * IPostProcessPass 是引擎自家"HDR / Bloom / Tonemap / LUT / GodRays"
//     的内部抽象——各 pass 的真录制由 Pipeline 内部硬编码（dynamic_cast
//     找类型再走 RecordXxx），Setup / Execute 是预留 stub 让公共面稳定。
//   * IRenderPass 是给**外部**用的——游戏 / 编辑器 / 第三方扩展通过
//     `Pipeline::InsertPass(stage, std::unique_ptr<IRenderPass>)` 把自己
//     的 pass 接进帧路径。Pipeline 在固定 hook 点调 pass 的 Execute，
//     pass 自管 GPU 资源、自己录制 RHI 命令、自己处理 barrier。
//
// 头文件隔离约束：本头**不** include `<orange/...>`——OrangeRender RHI
// 类型只用前向声明引用，与 IPostProcessPass 同思路。RenderGraphBuilder
// / RenderPassContext 的具体字段通过 RenderPassContext.h 暴露；那条头
// 也保持公共面对 RHI 类型的隔离（透过不透明 void*）。
//
// 当前阶段（0.x 首版）的 PipelineStage 枚举仅三档：
//   * AfterShadow      —— shadow map 之后、main pass 之前
//   * AfterMainPass    —— main + particle pass 之后、bloom / godrays 之前
//   * AfterPostProcess —— stage B 收尾后（swap-chain 已写出，ImGui /
//                         debug overlay 走 renderer.SubmitItem 路径）
//
// 按需扩 Opaque / Transparent / UI 等中间档，公共面追加枚举不破调用方。
// 0.x 这三档覆盖 80% 用例（水面 / 屏幕扭曲 / 编辑器 overlay）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstdint>

namespace Orange::Engine::Render
{

class RenderGraphBuilder;
class RenderPassContext;

// Pipeline 编排里供 InsertPass 选择的 hook 点。
enum class PipelineStage : std::uint8_t
{
    AfterShadow,        // shadow map 已写完、main pass 还没开始
    AfterMainPass,      // main + particle 已写完，bloom / godrays / tonemap 还没跑
    AfterPostProcess,   // stage B 已收尾，swap-chain 已写出
};

class ORANGE_ENGINE_API IRenderPass
{
public:
    virtual ~IRenderPass() = default;

    IRenderPass()                              = default;
    IRenderPass(const IRenderPass&)            = delete;
    IRenderPass& operator=(const IRenderPass&) = delete;
    IRenderPass(IRenderPass&&)                 = delete;
    IRenderPass& operator=(IRenderPass&&)      = delete;

    // 调试 / FindPass 用名称。同 stage 下名字唯一不强制——Pipeline 不
    // 做去重，调用方自己负责命名。空字符串合法（debug log 显示 "(unnamed)"）。
    virtual const char* Name() const noexcept = 0;

    // Pipeline 在 InsertPass 时调一次（让 pass 建 GPU 资源——pipeline /
    // descriptor 等长寿命对象）；HDR target 重建（OnResize / 首次）时
    // Pipeline 再调一次让 pass 同步刷资源。Setup 应当幂等：多次调用不
    // 累积副作用。
    //
    // RenderGraphBuilder 当前是占位接口（详见 RenderPassContext.h）——
    // pass 调 Read / Write 声明依赖，0.x Pipeline 忽略；后续真接
    // RenderGraph 自动调度时这些声明会变成实际的 barrier 推断输入。
    virtual void Setup(RenderGraphBuilder& builder) = 0;

    // 每帧到当前 stage hook 点时被 Pipeline 调一次。pass 通过 ctx 获取
    // 当前 cmd list / HDR target view / 主相机 viewProj 等，自己录制
    // RHI 命令。具体字段见 RenderPassContext.h。
    virtual void Execute(RenderPassContext& ctx) = 0;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_I_RENDER_PASS_H
