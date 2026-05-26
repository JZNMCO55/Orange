#ifndef ORANGE_ENGINE_RENDER_I_AUX_PASS_PROVIDER_H
#define ORANGE_ENGINE_RENDER_I_AUX_PASS_PROVIDER_H

// ---------------------------------------------------------------------------
// IAuxPassProvider —— 引擎主 pass 与后处理之间的辅助 pass 钩子（v1.3.0 引入）。
//
// 设计意图：让"编辑器审美 / 工具向 pass"（如 grid / outline / wireframe
// / debug overlay）从外部注入，不污染 engine 公共面 —— 满足 OrangeEngine
// "Game-specific concepts forbidden in engine" 与 OrangeRender API 中性化
// 原则同节奏。
//
// 调用时机（Pipeline::Render 内）：
//   主几何 pass → IBL ambient + 粒子 + 主光 shadow 全部完成
//     ↓
//   IAuxPassProvider::RenderAuxPass(ctx)   ← 本 hook
//     ↓
//   bloom downsample / upsample / tonemap / output
//
// 调用约定：
//   * provider 进入时 hdrColor 处于 ShaderReadOnly layout（主 pass 末尾遗留）；
//     provider 自行 transition 到需要的 layout（典型 ColorAttachment 做
//     alpha-blend 叠加）；离开时**必须**留在 ShaderReadOnly（下游 bloom
//     系列按此假设）。
//   * sceneDepth 进入时**总是** ShaderReadOnly —— Pipeline 在调用 hook 前
//     已统一 transition（v1.3.0 grid 真迁出后引入的契约：让 provider 仅做
//     纯渲染、不操心 depth state 流转）；provider 不得修改 depth layout。
//     `ctx.sceneDepthIsShaderReadOnly` 字段保留并固定为 true，供兼容性
//     消费者读取。
//   * AuxPassContext 内所有指针由 Pipeline 拥有，生命周期 = 单帧；provider
//     不得跨帧持有。
//
// 落地状态（基准 2026-05-24）：v1.2.1 ship 接口与 hook 调用点；v1.3.0
// 实际接通 grid 真迁出 —— 引擎端 grid pass 资源 / shader / 调用点全部
// 移除，编辑器侧 EditorGridAuxPassProvider 通过本 hook 注入实现完整功
// 能等价；本接口此后是辅助 pass 的唯一注入入口（外部 outline / wireframe /
// debug overlay 等同模式接入）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <glm/mat4x4.hpp>

#include <cstdint>

// 前向声明 RHI 类型 —— 实际定义在 OrangeRender 公共面。本头不 include
// <orange/rhi/...>，让引擎公共头保持 "header isolation" invariant（参
// CLAUDE.md "Design guardrails / Header isolation" 段）。Provider 在
// editor / 游戏端实现时按需 include OR 头消费 ctx 字段。
namespace Orange::Rhi
{
class RHICommandList;
class RHITexture;
class RHISampler;
class RHIShaderModule;
// 前向声明枚举（带底层类型）—— 与 DebugDrawScene.h 同款 header-isolation 安全
// 模式（公共头不 #include <orange/rhi/...>，仅声明）。
enum class TextureFormat : std::uint32_t;
}  // namespace Orange::Rhi

namespace Orange::Engine::Render
{

// 单帧辅助 pass 的上下文 —— Pipeline 在主 pass 完成后填好喂给
// IAuxPassProvider::RenderAuxPass。所有指针由 Pipeline 拥有，调用结束
// 后字段失效。
struct ORANGE_ENGINE_API AuxPassContext
{
    Orange::Rhi::RHICommandList* pCmd          = nullptr;
    // hdrColor 进入时 ShaderReadOnly；provider 自行 transition + Begin/End
    // Rendering；离开时必须 ShaderReadOnly。
    Orange::Rhi::RHITexture*     pHdrColor     = nullptr;
    // sceneDepth 可能 DepthStencilAttachment 或 ShaderReadOnly（取决于
    // 上游是否 transition）；按 sceneDepthIsShaderReadOnly 判定当前状态。
    Orange::Rhi::RHITexture*     pSceneDepth   = nullptr;
    // 复用 Pipeline 内 hdrSampler 用于采 sceneDepth（避免 provider 自创
    // 重复资源）。
    Orange::Rhi::RHISampler*     pHdrSampler   = nullptr;
    std::uint32_t                hdrWidth      = 0;
    std::uint32_t                hdrHeight     = 0;
    glm::mat4                    invViewProj   = glm::mat4(1.0f);
    glm::mat4                    viewProj      = glm::mat4(1.0f);
    bool                         sceneDepthIsShaderReadOnly = false;

    // hdrColor / sceneDepth 的实际 TextureFormat（Pipeline 从 RT desc 取，权威）。
    // provider 创建匹配的 PSO 时用，避免 hardcode 引擎 HDR target 格式常量——
    // engine 单点改 HDR 格式后所有 provider 自动跟进（GAP-2026-05-24）。
    Orange::Rhi::TextureFormat   hdrColorFormat{};
    Orange::Rhi::TextureFormat   sceneDepthFormat{};
    // 引擎内置 fullscreen.vert（big-triangle，gl_VertexIndex → 全屏覆盖）已编译
    // 的 RHIShaderModule，供 provider 直接传给 fullscreen PSO 创建，免各自维护
    // sibling copy + 重复 spv 编译路径（GAP-2026-05-24）。生命周期 = Pipeline 持有。
    Orange::Rhi::RHIShaderModule* pFullscreenVs = nullptr;
};

// 辅助 pass 钩子接口。注册到 Pipeline 后每帧主 pass 完成、后处理之前
// 调用 RenderAuxPass(ctx)。
//
// 多 provider 支持留待后续——v1.3.0 单 provider，注册第二个会覆盖前者。
class ORANGE_ENGINE_API IAuxPassProvider
{
public:
    virtual ~IAuxPassProvider() = default;

    // 单次 aux pass 渲染入口。约定见本头文件开头"调用约定"段。
    virtual void RenderAuxPass(AuxPassContext& ctx) = 0;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_I_AUX_PASS_PROVIDER_H
