// AssetRegistry —— PIMPL 骨架。
//
// 当前阶段只把生命周期 (ctor / dtor / move) 落实，让 AssetRegistry
// 的公共类型可以被消费者持有、移动、析构。Load / Get / Unload /
// RegisterLoader 这几个 template 方法的真实实现要等 Task 02 落
// loader 与内部存储时一并补上——届时 Impl 中会出现按
// `std::type_index` 路由的 erased 入口，公共 template 方法的内联实
// 现 forward 到那里。
//
// 在那之前，调用上述 template 方法会触发链接器 unresolved symbol，
// 这是刻意保留的"未接通"硬信号。

#include "orange/engine/asset/AssetRegistry.h"

namespace Orange::Engine::Asset
{

struct AssetRegistry::Impl
{
    // Task 02 起这里会长出：
    //   std::unordered_map<std::type_index, LoaderEntry>  loaders;
    //   std::unordered_map<std::type_index, AssetTable>   assets;
    // 当前阶段保持空 struct——既能让 unique_ptr<Impl> 完整析构，又不
    // 提前固化任何存储选型。
};

AssetRegistry::AssetRegistry() : mpImpl(std::make_unique<Impl>())
{
}

AssetRegistry::~AssetRegistry() = default;

AssetRegistry::AssetRegistry(AssetRegistry&&) noexcept            = default;
AssetRegistry& AssetRegistry::operator=(AssetRegistry&&) noexcept = default;

}  // namespace Orange::Engine::Asset
