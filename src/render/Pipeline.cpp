// Pipeline 实现：当前阶段把 World 收成 RenderScene，但还没把 scene
// 真正下发给 OrangeRender RenderGraph——那一步是 Task 07。
//
// Render(world) 的语义：每帧顶部 Clear scene，再 Collect 一遍把 World
// 翻成 (camera, drawable list)。这一帧没有相机时也仍然 Collect（最多
// 拿到 0 个 drawable），让"World 暂时还没人放 Camera"也是受支持的
// 启动状态。

#include "orange/engine/render/Pipeline.h"

#include "orange/engine/render/RenderScene.h"

namespace Orange::Engine::Render
{

struct Pipeline::Impl
{
    RenderScene scene;
};

Pipeline::Pipeline() : mpImpl(std::make_unique<Impl>())
{
}

Pipeline::~Pipeline() = default;

Pipeline::Pipeline(Pipeline&&) noexcept            = default;
Pipeline& Pipeline::operator=(Pipeline&&) noexcept = default;

void Pipeline::Render(Orange::Engine::World& world)
{
    auto& scene = mpImpl->scene;
    scene.Clear();
    scene.Collect(world);

    // Task 07 起在这里加：用 OrangeRender RenderGraph 把
    // scene.MainCamera() / scene.Drawables() 真正下发到 GPU。
}

}  // namespace Orange::Engine::Render
