#ifndef ORANGE_ENGINE_ASSET_ASSET_REGISTRY_H
#define ORANGE_ENGINE_ASSET_ASSET_REGISTRY_H

// ---------------------------------------------------------------------------
// AssetRegistry —— 引擎运行时的资源注册表。
//
// 职责：
//   * 把按类型 T 注册过来的 `IAssetLoader<T>` 收集起来，按 typeid(T)
//     做派遣；
//   * 收到 `Load<T>(path)` 后调用对应 loader、把结果存入内部表，并
//     返回一个稳定的 `AssetHandle<T>`；
//   * `Get<T>(handle)` 返回只读引用，handle 失效时返回 nullptr；
//   * `Unload<T>(handle)` 显式释放。
//
// PIMPL 是必要的：内部存储要按运行时类型做 type erasure（`std::
// unordered_map<std::type_index, ...>`），把这部分实现细节挡在公共
// 头之外，让公共面只看到"同步、Result-返回"的窄接口。
//
// 当前阶段（Phase 2 / Task 01）只把 PIMPL 骨架与公共声明落地——
// `Load` / `Get` / `Unload` / `RegisterLoader` 的真实语义将在 Task 02
// 通过一组非模板 type-erased 私有入口落到 .cpp 中（template 部分仍
// 留在头里 forward 给 erased 入口）。在那之前对它们的调用会触发
// 链接器报告未实现，刻意作为"还没接通"的硬信号。
//
// 设计取舍：为什么不用 std::any / std::variant 而用 type_index 路
// 由？
//   * std::any 把每个 asset 装箱一次，destruct 路径走 RTTI；asset 数
//     量一旦上千，开销不可忽略；
//   * std::variant 要求所有 asset 类型在编译期可枚举——和"游戏侧能
//     注册自定义资源类型"这一扩展点直接冲突；
//   * type_index + 自定义 deleter 的 type erasure 既保留了运行时可
//     扩展，又避免给热路径加额外间接层。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/IAssetLoader.h>
#include <orange/engine/core/Result.h>

#include <memory>
#include <string_view>

namespace Orange::Engine::Asset
{

class ORANGE_ENGINE_API AssetRegistry
{
public:
    AssetRegistry();
    ~AssetRegistry();

    AssetRegistry(const AssetRegistry&)            = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;

    AssetRegistry(AssetRegistry&&) noexcept;
    AssetRegistry& operator=(AssetRegistry&&) noexcept;

    // 注册一种 asset type T 的 loader。同一 T 重复注册会替换之前的
    // loader（相当于 hot-swap）。不允许传 nullptr。
    template <typename T>
    void RegisterLoader(std::unique_ptr<IAssetLoader<T>> loader);

    // 同步加载。语义：
    //   * 同 path 重复 Load<T> 走 dedup，返回同一 handle；
    //   * 没注册过 T 的 loader → 返回 ResultCode::Unsupported；
    //   * loader 自己返回的错误透传出来。
    template <typename T>
    Result<AssetHandle<T>, ResultCode> Load(std::string_view path);

    // 通过 handle 取只读资源。无效 / 已卸载 handle 返回 nullptr，绝
    // 不抛。返回值的生存期由 Registry 保证：直至 `Unload` 或 Registry
    // 析构。
    template <typename T>
    const T* Get(AssetHandle<T> handle) const noexcept;

    // 显式卸载。返回 false 表示 handle 不在表中。
    template <typename T>
    bool Unload(AssetHandle<T> handle);

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine::Asset

#endif  // ORANGE_ENGINE_ASSET_ASSET_REGISTRY_H
