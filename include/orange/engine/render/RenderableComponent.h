#ifndef ORANGE_ENGINE_RENDER_RENDERABLE_COMPONENT_H
#define ORANGE_ENGINE_RENDER_RENDERABLE_COMPONENT_H

// ---------------------------------------------------------------------------
// RenderableComponent —— 实体 → "可被渲染的几何 + 表面" 的关联组件。
//
// 当前形态：mesh handle + 非拥有 MaterialInstance 指针 + visibility 开关。
// MaterialInstance 由 sample / 游戏代码侧持有（典型：在 main 里持
// `std::vector<std::unique_ptr<MaterialInstance>>`），component 仅承载
// 一个裸指针——保持组件 trivially-copyable，避免 EnTT archetype 因
// move-only PIMPL 触发整行迁移。生命周期约束：MaterialInstance 必须活
// 到 World 析构之后；典型做法是先析构 World，再析构 instance 容器。
//
// `materialInstance == nullptr` 是受支持的退化态——Pipeline 在该路径下
// 走 fallback（典型：当前阶段的 hardcoded textured pipeline，下一子任
// 务起切到 textured template Pipeline 缓存）。这样 sample 在过渡期间
// 既可以使用 MaterialSystem，也可以临时不挂 instance 用最简形态跑通。
//
// 选 handle 持有 mesh：
//   * 资源生命周期由 AssetRegistry 持有，不能被本组件意外延寿；
//   * handle 可在序列化路径上原样写入 / 读取，不依赖运行时指针；
//   * 跨实体复用同一 mesh 时，registry 的 dedup 缓存自动生效。
// ---------------------------------------------------------------------------

#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/MeshAsset.h>

namespace Orange::Engine::Render
{

class MaterialInstance;

struct RenderableComponent
{
    Asset::AssetHandle<Asset::MeshAsset> mesh{};
    MaterialInstance*                    materialInstance{nullptr};
    bool                                 visible{true};
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_RENDERABLE_COMPONENT_H
