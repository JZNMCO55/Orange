#ifndef ORANGE_EDITOR_RENDER_EDITOR_GRID_AUX_PASS_PROVIDER_H
#define ORANGE_EDITOR_RENDER_EDITOR_GRID_AUX_PASS_PROVIDER_H

// ---------------------------------------------------------------------------
// EditorGridAuxPassProvider —— 编辑器 viewport 地面 grid pass 的
// IAuxPassProvider 实现（v1.3.0 由 OrangeEngine 内置 grid pass 迁出至编辑
// 器自家路径）。
//
// 设计意图：grid 是"编辑器审美 / 工具向 pass" —— 引擎公共面应保持中性，
// 不含 viewport grid / outline / wireframe 等工具向资源。本 provider 自管
// shader / PSO / descriptor pool / set，并通过 Pipeline::SetAuxPassProvider
// 注册到引擎 hook 点，让编辑器关闭 / 卸载本 provider 时 engine 端零残留。
//
// 调用约定（详见 `orange/engine/render/IAuxPassProvider.h`）：
//   * Pipeline 在主几何 pass + 粒子 + grid 之后、debug draw + 后处理之前
//     调用 RenderAuxPass(ctx)；
//   * Pipeline 已经把 ctx.pSceneDepth 翻成 ShaderReadOnly（v1.3.0 hook 前
//     置契约，省去 provider 自己 transition 逻辑）；
//   * ctx.pHdrColor 进入时 ShaderReadOnly，provider 自行 transition 到
//     ColorAttachment 做 alpha-blend，离开时**必须**翻回 ShaderReadOnly；
//   * 单帧寿命 ctx，所有指针均不可跨帧持有。
//
// 资源 / shader：
//   * fragment shader: `shaders/orange_editor/grid.frag.spv`（Pristine
//     Grid + sceneDepth 手动 compare + discard）
//   * vertex shader: `shaders/orange_editor/fullscreen.vert.spv`
//     （big-triangle 模板，与 engine 端 fullscreen.vert 字字相同语义）
//   * descriptor set layout: 1 binding (CombinedImageSampler, sceneDepth)
//   * push constant 128 B (mat4 invViewProj + mat4 viewProj)
//
// 生命周期：构造 → Initialize(device) → 注册到 Pipeline → 主循环每帧
// RenderAuxPass 被调 → Shutdown 释放资源（必须 device.WaitIdle 后）→
// Pipeline::SetAuxPassProvider(nullptr) 摘掉注册 → 析构。
// ---------------------------------------------------------------------------

#include <orange/engine/render/IAuxPassProvider.h>

#include <memory>

namespace Orange::Renderer
{
    class RenderDevice;
}
namespace Orange::Rhi
{
    class RHIDescriptorPool;
    class RHIDescriptorSet;
    class RHIDescriptorSetLayout;
    class RHIPipeline;
    class RHIShaderModule;
    class RHITexture;
} // namespace Orange::Rhi

class EditorGridAuxPassProvider final : public Orange::Engine::Render::IAuxPassProvider
{
public:
    EditorGridAuxPassProvider();
    ~EditorGridAuxPassProvider() override;

    EditorGridAuxPassProvider(const EditorGridAuxPassProvider&)            = delete;
    EditorGridAuxPassProvider& operator=(const EditorGridAuxPassProvider&) = delete;

    // 一次性初始化 GPU 资源（shader 模块 + descriptor set layout + pipeline）。
    // 失败返 false 并日志告警；调用方应判断后决定是否注册到 Pipeline
    // （未初始化的 provider 注册后 RenderAuxPass 仅 silent skip，与不注册
    // 行为基本等价但浪费一次 hook 调用）。
    //
    // device 借用语义（不取所有权）：调用方负责保证 device 活到 Shutdown
    // 之前；典型路径是编辑器自己创建的 RenderDevice，借给 Pipeline +
    // ImGui Vulkan backend + 本 provider 三方共用。
    bool Initialize(Orange::Renderer::RenderDevice& device);

    // 释放 GPU 资源（descriptor set / pool / pipeline / layout / shader 模
    // 块按反序）。调用方应在 Pipeline::SetAuxPassProvider(nullptr) 摘掉注
    // 册 + device.WaitIdle 之后调；幂等。
    void Shutdown();

    bool IsInitialized() const noexcept;

    // 显隐开关（默认关）。关闭时 RenderAuxPass silent skip，仍保留 GPU 资
    // 源 —— 切换频繁时避免重复 PSO / descriptor 重建开销。
    void SetEnabled(bool enabled) noexcept { mEnabled = enabled; }
    bool IsEnabled() const noexcept { return mEnabled; }

    // IAuxPassProvider 实现。详见 IAuxPassProvider.h 调用约定。
    void RenderAuxPass(Orange::Engine::Render::AuxPassContext& ctx) override;

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;
    bool                  mEnabled{false};
};

#endif // ORANGE_EDITOR_RENDER_EDITOR_GRID_AUX_PASS_PROVIDER_H
