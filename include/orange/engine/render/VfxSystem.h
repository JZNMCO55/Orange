#ifndef ORANGE_ENGINE_RENDER_VFX_SYSTEM_H
#define ORANGE_ENGINE_RENDER_VFX_SYSTEM_H

// ---------------------------------------------------------------------------
// VfxSystem —— 粒子子系统的根入口。
//
// 职责拆分：
//   * sim：每个 entity 的 ParticleEmitterComponent 关联一个 CPU 粒子池
//     （AoS）。Tick(world, dt) 把所有 emitter 推进一步——按 emissionRate
//     spawn 新粒子、推进每个粒子的 age / position / color / size，到期
//     回收。粒子池由 VfxSystem 中央拥有，不放进 component 字段。
//   * draw：每帧 sim 之后把所有 emitter 的所有粒子打包成一段 instance
//     buffer，一次 instanced draw 提交给 RHI（screen-aligned billboard
//     quad，additive blending）。Pipeline 在 main pass 之后 / postprocess
//     之前调一次 `DrawParticles`，让粒子写到与 main pass 同一个 HDR target，
//     自然喂给 bloom（颜色 a > 1 时 → 发光晕）。
//
// 头隔离：本头**不**含 `<orange/...>` / Vulkan / volk —— 与 CLAUDE.md
// "Header isolation" 不变量保持一致。RHI / Renderer 的真实参数通过
// 不透明 `void*` 在公共面交换；实现一侧（src/render/VfxSystem.cpp）
// 自己 reinterpret_cast 回 OrangeRender 类型——这与 PostProcessChain 的
// IPostProcessPass setup/execute context 同思路。
//
// 生命周期：
//   * 构造默认 no-op；调用方在 Pipeline.Initialize 之后调
//     `VfxSystem::Initialize(renderDevice)` 一次（ctor 不接 RHI 是为了
//     保证 VfxSystem 实例可在 Pipeline 之外构造、用于纯 sim 测试）；
//   * Tick 在主循环每帧调一次（典型在 AppHost 主循环、Pipeline.Render
//     之前）；
//   * 调用方把 `VfxSystem*` 通过 `Pipeline::SetVfxSystem` 注册给 Pipeline。
//     Pipeline 持有非拥有指针；nullptr 时跳过 particle pass，sample 不
//     break。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Result.h>
#include <orange/engine/scene/Entity.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace Orange::Engine
{
class World;
}  // namespace Orange::Engine

namespace Orange::Engine::Asset
{
class AssetRegistry;
}  // namespace Orange::Engine::Asset

namespace Orange::Engine::Render
{

class ORANGE_ENGINE_API VfxSystem
{
public:
    VfxSystem();
    ~VfxSystem();

    VfxSystem(const VfxSystem&)            = delete;
    VfxSystem& operator=(const VfxSystem&) = delete;

    VfxSystem(VfxSystem&&) noexcept;
    VfxSystem& operator=(VfxSystem&&) noexcept;

    // 在指定 RenderDevice 上初始化粒子绘制资源（pipeline、instance buffer
    // FIF slices、内置 SPIR-V 加载）。`pRenderDevice` 是 OrangeRender 的
    // RenderDevice*（不透明）；`framesInFlight` 通常与 Pipeline 同值（典
    // 型 2）。`assets` 用于加载内置 additive_billboard SPIR-V。
    //
    // 重复 Initialize 返回 AlreadyInitialized；未 Initialize 时 Tick 仍
    // 可跑（纯 CPU sim 不依赖 GPU），但 DrawParticles 是 no-op。
    Result<void, ResultCode> Initialize(void*                     pRenderDevice,
                                        std::uint32_t             framesInFlight,
                                        Asset::AssetRegistry&     assets);

    // 释放 GPU 资源。Pipeline.Shutdown 之前调一次。幂等。
    void Shutdown();

    bool IsInitialized() const noexcept;

    // 推进所有 emitter 一帧。`dt` < 0 当 0 处理。Entity 已销毁的池在
    // 下次 Tick 时被自动回收（通过 world.IsValid 校验）。
    void Tick(const World& world, float dt);

    // 把粒子绘制录制进 Pipeline 给的 cmd list。参数全部不透明：
    //   * pCmdList     —— OrangeRender Rhi::RHICommandList*。已 Begin
    //     且当前在 ColorAttachment HDR target 的 BeginRendering 段中
    //     （Pipeline 主 pass 末尾，调用方负责进 / 出渲染段）。
    //   * pHdrColorView —— OrangeRender Rhi::RHITextureView*；当前 HDR
    //     scene color 的 default view。VfxSystem 用它 BeginRendering
    //     新一段（LoadOp::Load 保留主 pass 内容）。
    //   * pDepthView   —— 主 pass 的 depth attachment view（粒子 pass
    //     仍 read 它做 depth test，不写 depth）。可空——空时粒子不参与
    //     depth test。
    //   * viewProjMatrixData —— 16 个 float（mat4，列主序，与 Pipeline
    //     主 pass 同一个 viewProj）。
    //   * frameIndex   —— 单调递增帧序号；VfxSystem 取 modulo
    //     framesInFlight 选 instance buffer slice。
    //   * hdrWidth / hdrHeight —— 当前 HDR target 尺寸（像素）。
    //
    // 未 Initialize / 无粒子 → no-op。
    void DrawParticles(void*               pCmdList,
                       void*               pHdrColorView,
                       void*               pDepthView,
                       const float*        viewProjMatrixData,
                       std::uint64_t       frameIndex,
                       std::uint32_t       hdrWidth,
                       std::uint32_t       hdrHeight);

    // 诊断 / 测试用：当前所有发射器累计的活粒子数（VfxSystemTest 验
    // 收 spawn / 回收语义靠这个）。
    std::size_t TotalLiveParticleCount() const noexcept;

    // 诊断 / 测试用：指定 entity 的 emitter 池里当前活粒子数。entity
    // 没有池 / entity 无效 → 0。
    std::size_t LiveParticleCount(Entity emitterEntity) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_VFX_SYSTEM_H
