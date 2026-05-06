// Pipeline —— PIMPL 骨架。
//
// 当前阶段（Phase 2 / Task 05）只把对象生命周期与 Render() 的非操作
// 占位 body 落到 .cpp。后续：
//   * Task 06 在 Impl 内加 RenderScene 收集逻辑（World → drawable list）；
//   * Task 07 在 Impl 内接 OrangeRender RenderGraph，真正下发绘制。
//
// 在那之前，Render() 有意保持 no-op：让 sample / 测试 / AppHost 能
// 把 Pipeline 串进主循环、不报错，同时也不假装在做实际渲染。

#include "orange/engine/render/Pipeline.h"

namespace Orange::Engine::Render
{

struct Pipeline::Impl
{
    // Task 06 起这里会长出：
    //   std::vector<DrawCommand> drawList;
    //   …
    // Task 07 起会引入 OrangeRender RHI / RenderGraph 句柄；这两类
    // 第三方头此 TU 落地后才会 #include 进来——在那之前 Impl 保持
    // 空 struct，让 unique_ptr<Impl> 完整析构。
};

Pipeline::Pipeline() : mpImpl(std::make_unique<Impl>())
{
}

Pipeline::~Pipeline() = default;

Pipeline::Pipeline(Pipeline&&) noexcept            = default;
Pipeline& Pipeline::operator=(Pipeline&&) noexcept = default;

void Pipeline::Render(Orange::Engine::World& /*world*/)
{
    // 占位：Task 06 / 07 接通后填入"收集 + 下发"两段逻辑。当前刻意
    // 保持空操作而不是 assert(false) —— 让 sample / 测试可以无副作
    // 用地把 Pipeline 串进主循环。
}

}  // namespace Orange::Engine::Render
