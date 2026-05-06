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

#include <memory>

namespace Orange::Engine
{
class World;
}

namespace Orange::Engine::Render
{

class ORANGE_ENGINE_API Pipeline
{
public:
    Pipeline();
    ~Pipeline();

    Pipeline(const Pipeline&)            = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    Pipeline(Pipeline&&) noexcept;
    Pipeline& operator=(Pipeline&&) noexcept;

    // 渲染一帧。Phase 2 当前阶段：
    //   * Task 05 仅提供空 body 占位；
    //   * Task 06 在内部实现 RenderScene 收集（World → drawable list）；
    //   * Task 07 接 OrangeRender RenderGraph 下发实际绘制。
    //
    // 按约定：`world` 中应至少存在一个挂有 `Render::Camera` 组件的
    // 实体；当前实现选第一个命中的相机作为本帧 view/projection 来源。
    void Render(::Orange::Engine::World& world);

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_PIPELINE_H
