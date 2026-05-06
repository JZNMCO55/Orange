#ifndef ORANGE_ENGINE_ASSET_ASSET_HANDLE_H
#define ORANGE_ENGINE_ASSET_ASSET_HANDLE_H

// ---------------------------------------------------------------------------
// AssetHandle —— 资源在 AssetRegistry 中的不透明身份。
//
// 直接复用 Core::TypedHandle<T>：phantom tag 保证 `AssetHandle<MeshAsset>`
// 与 `AssetHandle<TextureAsset>` 在类型系统层互不兼容，避免把一个
// mesh handle 误传成 texture handle；同时 std::hash 特化（在
// Handle.h 中）也跟着继承过来，AssetRegistry 内部可直接拿它建表。
//
// 之所以另起一个名字而不是让消费者直接写 `TypedHandle<T>`：asset 是
// 一个明确的语义层，独立 alias 让公共 API 读起来有领域味，且未来若
// 要给 handle 附加状态（loading / ready / failed）可以从 alias 平滑
// 升级到独立 class，不破调用点。
// ---------------------------------------------------------------------------

#include <orange/engine/core/Handle.h>

namespace Orange::Engine::Asset
{

template <typename T>
using AssetHandle = ::Orange::Engine::TypedHandle<T>;

}  // namespace Orange::Engine::Asset

#endif  // ORANGE_ENGINE_ASSET_ASSET_HANDLE_H
