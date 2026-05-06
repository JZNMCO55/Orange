#ifndef ORANGE_ENGINE_RENDER_RENDERABLE_COMPONENT_H
#define ORANGE_ENGINE_RENDER_RENDERABLE_COMPONENT_H

// ---------------------------------------------------------------------------
// RenderableComponent —— 实体 → "可被渲染的几何 + 表面" 的关联组件。
//
// Phase 2 的最小形态：一个 mesh handle + 一个可选 texture handle +
// visibility 开关。Phase 3 接入 Material 系统后，texture 槽会被一个
// `Asset::AssetHandle<MaterialInstance>`（或类似）取代——届时本组件
// 的 schema 升级 + 加 SchemaVersion（参见 Phase 1 的 Core::Serialization
// 约定）。
//
// 选择 handle 而非裸指针：
//   * 资源生命周期由 AssetRegistry 持有，不能被本组件意外延寿；
//   * handle 可在序列化路径上原样写入 / 读取，不依赖运行时指针；
//   * 跨实体复用同一 mesh 时，registry 的 dedup 缓存自动生效。
// ---------------------------------------------------------------------------

#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/TextureAsset.h>

namespace Orange::Engine::Render
{

struct RenderableComponent
{
    Asset::AssetHandle<Asset::MeshAsset>    mesh{};
    Asset::AssetHandle<Asset::TextureAsset> texture{};
    bool visible{true};
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_RENDERABLE_COMPONENT_H
