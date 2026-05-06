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

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_PIPELINE_H
