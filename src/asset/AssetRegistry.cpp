// AssetRegistry —— Impl 与 type-erased dispatch 实现。
//
// 内部存储分两层：
//   * mLoaders : type_index → LoaderEntry  注册过的 loader 集合
//   * mTables  : type_index → AssetTable   每种 T 一张资源表
//
// AssetTable 用 vector<Slot> + 自由列表做 handle 分配——slot 索引 +1
// 即对外的 handle value（0 保留给 invalid 比较的尾值；本实现用
// kInvalidValue=u64::max 兜底，所以 0..kMax-1 任意 slot 均可作 valid
// handle）。同 path 走 mPathToHandle 实现 dedup：第二次 Load 同 path
// 的同类型 T 直接返回首次的 handle，loader 不会再被调用。

#include "orange/engine/asset/AssetRegistry.h"

#include <cstdint>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Orange::Engine::Asset
{
namespace
{

// AssetRegistry 的私有 using 别名（LoaderDeleter / LoadInvoker /
// AssetDeleter）是 private 类成员，匿名 namespace 不能引用，所以这里
// 直接用展开形式。函数指针签名需与 AssetRegistry.h 中的 private using
// 对齐——任一处改了，另一处必须同步。
struct LoaderEntry
{
    void* loader{nullptr};
    void (*loaderDeleter)(void*) noexcept{nullptr};
    ResultCode (*invoker)(void*, std::string_view, void**) noexcept{nullptr};
    void (*assetDeleter)(void*) noexcept{nullptr};
};

struct AssetSlot
{
    std::string path;
    void*       asset{nullptr};
    bool        live{false};
};

struct AssetTable
{
    std::vector<AssetSlot> slots;
    std::vector<std::uint64_t> freeList;
    std::unordered_map<std::string, std::uint64_t> pathToHandle;
    void (*assetDeleter)(void*) noexcept{nullptr};
};

}  // namespace

struct AssetRegistry::Impl
{
    std::unordered_map<std::type_index, LoaderEntry> loaders;
    std::unordered_map<std::type_index, AssetTable>  tables;
    std::size_t liveAssets{0};

    ~Impl() noexcept
    {
        // 析构顺序：先逐 table 释放 asset，再逐 loader 释放 loader 实
        // 例。两次都用各自的 deleter——避免依赖 T 在此 TU 是否完整。
        for (auto& [type, table] : tables)
        {
            for (auto& slot : table.slots)
            {
                if (slot.live && slot.asset && table.assetDeleter)
                {
                    table.assetDeleter(slot.asset);
                }
            }
        }
        tables.clear();

        for (auto& [type, entry] : loaders)
        {
            if (entry.loader && entry.loaderDeleter)
            {
                entry.loaderDeleter(entry.loader);
            }
        }
        loaders.clear();
    }
};

AssetRegistry::AssetRegistry() : mpImpl(std::make_unique<Impl>())
{
}

AssetRegistry::~AssetRegistry() = default;

AssetRegistry::AssetRegistry(AssetRegistry&&) noexcept            = default;
AssetRegistry& AssetRegistry::operator=(AssetRegistry&&) noexcept = default;

std::size_t AssetRegistry::Size() const noexcept
{
    return mpImpl ? mpImpl->liveAssets : 0;
}

bool AssetRegistry::Empty() const noexcept
{
    return Size() == 0;
}

Result<void, ResultCode> AssetRegistry::RegisterLoaderErased(
    const std::type_info& type,
    void* loaderRaw,
    LoaderDeleter loaderDeleter,
    LoadInvoker invoker,
    AssetDeleter assetDeleter)
{
    if (loaderRaw == nullptr || loaderDeleter == nullptr
        || invoker == nullptr || assetDeleter == nullptr)
    {
        // 调用方应保证四个函数指针都非空——这里防御性兜底。
        if (loaderRaw && loaderDeleter)
        {
            loaderDeleter(loaderRaw);
        }
        return ResultCode::InvalidArgument;
    }

    const std::type_index key{type};

    // 替换路径：先释放旧 loader 再塞新的；旧 loader 已加载的 asset
    // 留在表里继续可用（hot-swap 不应连带卸载历史资源）。
    auto it = mpImpl->loaders.find(key);
    if (it != mpImpl->loaders.end())
    {
        if (it->second.loader && it->second.loaderDeleter)
        {
            it->second.loaderDeleter(it->second.loader);
        }
        it->second.loader        = loaderRaw;
        it->second.loaderDeleter = loaderDeleter;
        it->second.invoker       = invoker;
        it->second.assetDeleter  = assetDeleter;
    }
    else
    {
        LoaderEntry entry{};
        entry.loader        = loaderRaw;
        entry.loaderDeleter = loaderDeleter;
        entry.invoker       = invoker;
        entry.assetDeleter  = assetDeleter;
        mpImpl->loaders.emplace(key, entry);
    }

    // 同步刷新本类型 table 的 deleter，便于后续析构走最新的。已存在
    // 的 slot 仍由旧 deleter 释放？—— 当前 hot-swap 不允许跨 deleter
    // 类型变更（同一 T 必然 deleter 一致）。这里直接覆盖即可。
    auto& table = mpImpl->tables[key];
    table.assetDeleter = assetDeleter;

    return Result<void, ResultCode>{};
}

ResultCode AssetRegistry::LoadErased(const std::type_info& type,
                                     std::string_view path,
                                     std::uint64_t& outHandle)
{
    outHandle = 0;
    const std::type_index key{type};

    auto& table = mpImpl->tables[key];

    // dedup：path 命中已加载的 slot → 直接返回老 handle。
    //
    // 这一步刻意排在"loader 是否注册"检查之前——Insert 路径先于
    // RegisterLoader 把资源挂到 pathToHandle 是合法用法（程序式构造
    // 资源、scene 序列化前注入资源等），此时 Load 同 path 同类型应直
    // 接命中 cache，不应该因为缺 loader 而报 Unsupported。
    const std::string pathKey{path};
    if (auto cacheIt = table.pathToHandle.find(pathKey);
        cacheIt != table.pathToHandle.end())
    {
        const std::uint64_t cached = cacheIt->second;
        const std::size_t idx = static_cast<std::size_t>(cached - 1);
        if (idx < table.slots.size() && table.slots[idx].live)
        {
            outHandle = cached;
            return ResultCode::Ok;
        }
        // path 在表里但 slot 已释放——擦掉旧映射，重新走加载路径。
        table.pathToHandle.erase(cacheIt);
    }

    auto loaderIt = mpImpl->loaders.find(key);
    if (loaderIt == mpImpl->loaders.end())
    {
        return ResultCode::Unsupported;
    }

    void* assetRaw = nullptr;
    ResultCode rc = loaderIt->second.invoker(loaderIt->second.loader, path, &assetRaw);
    if (rc != ResultCode::Ok)
    {
        if (assetRaw && loaderIt->second.assetDeleter)
        {
            loaderIt->second.assetDeleter(assetRaw);
        }
        return rc;
    }
    if (assetRaw == nullptr)
    {
        // loader 报 Ok 但没产物——视为内部错误，避免后续走空指针。
        return ResultCode::InternalError;
    }

    std::uint64_t slotIndex = 0;
    if (!table.freeList.empty())
    {
        slotIndex = table.freeList.back();
        table.freeList.pop_back();
        auto& slot = table.slots[static_cast<std::size_t>(slotIndex)];
        slot.path  = pathKey;
        slot.asset = assetRaw;
        slot.live  = true;
    }
    else
    {
        slotIndex = static_cast<std::uint64_t>(table.slots.size());
        AssetSlot slot{};
        slot.path  = pathKey;
        slot.asset = assetRaw;
        slot.live  = true;
        table.slots.emplace_back(std::move(slot));
    }

    const std::uint64_t handleValue = slotIndex + 1;
    table.pathToHandle.emplace(pathKey, handleValue);
    outHandle = handleValue;
    ++mpImpl->liveAssets;
    return ResultCode::Ok;
}

ResultCode AssetRegistry::InsertErased(const std::type_info& type,
                                       std::string_view path,
                                       void* assetRaw,
                                       AssetDeleter assetDeleter,
                                       std::uint64_t& outHandle)
{
    outHandle = 0;
    if (assetRaw == nullptr || assetDeleter == nullptr)
    {
        if (assetRaw && assetDeleter)
        {
            assetDeleter(assetRaw);
        }
        return ResultCode::InvalidArgument;
    }

    const std::type_index key{type};
    auto& table = mpImpl->tables[key];

    // 同 T 的 deleter 必然一致；多次 Insert 重复设 deleter 等于幂等
    // 覆盖。设 deleter 让 Registry 析构时能正确释放该 type 的资源。
    table.assetDeleter = assetDeleter;

    const std::string pathKey{path};

    // 已存在 path：先释放旧资源，复用其 slot；保持 handle 稳定。
    if (auto cacheIt = table.pathToHandle.find(pathKey);
        cacheIt != table.pathToHandle.end())
    {
        const std::uint64_t cached = cacheIt->second;
        const std::size_t idx = static_cast<std::size_t>(cached - 1);
        if (idx < table.slots.size() && table.slots[idx].live)
        {
            auto& slot = table.slots[idx];
            if (slot.asset)
            {
                assetDeleter(slot.asset);
            }
            slot.asset = assetRaw;
            // path 不变；live 保持 true。
            outHandle = cached;
            return ResultCode::Ok;
        }
        table.pathToHandle.erase(cacheIt);
    }

    std::uint64_t slotIndex = 0;
    if (!table.freeList.empty())
    {
        slotIndex = table.freeList.back();
        table.freeList.pop_back();
        auto& slot = table.slots[static_cast<std::size_t>(slotIndex)];
        slot.path  = pathKey;
        slot.asset = assetRaw;
        slot.live  = true;
    }
    else
    {
        slotIndex = static_cast<std::uint64_t>(table.slots.size());
        AssetSlot slot{};
        slot.path  = pathKey;
        slot.asset = assetRaw;
        slot.live  = true;
        table.slots.emplace_back(std::move(slot));
    }

    const std::uint64_t handleValue = slotIndex + 1;
    table.pathToHandle.emplace(pathKey, handleValue);
    outHandle = handleValue;
    ++mpImpl->liveAssets;
    return ResultCode::Ok;
}

const void* AssetRegistry::GetErased(const std::type_info& type,
                                     std::uint64_t handleValue) const noexcept
{
    if (handleValue == 0)
    {
        return nullptr;
    }
    const std::type_index key{type};
    auto it = mpImpl->tables.find(key);
    if (it == mpImpl->tables.end())
    {
        return nullptr;
    }
    const auto& table = it->second;
    const std::size_t idx = static_cast<std::size_t>(handleValue - 1);
    if (idx >= table.slots.size())
    {
        return nullptr;
    }
    const auto& slot = table.slots[idx];
    if (!slot.live)
    {
        return nullptr;
    }
    return slot.asset;
}

bool AssetRegistry::UnloadErased(const std::type_info& type,
                                 std::uint64_t handleValue)
{
    if (handleValue == 0)
    {
        return false;
    }
    const std::type_index key{type};
    auto tableIt = mpImpl->tables.find(key);
    if (tableIt == mpImpl->tables.end())
    {
        return false;
    }
    auto& table = tableIt->second;
    const std::size_t idx = static_cast<std::size_t>(handleValue - 1);
    if (idx >= table.slots.size())
    {
        return false;
    }
    auto& slot = table.slots[idx];
    if (!slot.live)
    {
        return false;
    }

    if (slot.asset && table.assetDeleter)
    {
        table.assetDeleter(slot.asset);
    }

    table.pathToHandle.erase(slot.path);
    slot.path.clear();
    slot.asset = nullptr;
    slot.live  = false;
    table.freeList.push_back(static_cast<std::uint64_t>(idx));
    if (mpImpl->liveAssets > 0)
    {
        --mpImpl->liveAssets;
    }
    return true;
}

std::string_view AssetRegistry::PathOfErased(const std::type_info& type,
                                             std::uint64_t handleValue) const noexcept
{
    if (handleValue == 0)
    {
        return {};
    }
    const std::type_index key{type};
    auto it = mpImpl->tables.find(key);
    if (it == mpImpl->tables.end())
    {
        return {};
    }
    const auto& table = it->second;
    const std::size_t idx = static_cast<std::size_t>(handleValue - 1);
    if (idx >= table.slots.size())
    {
        return {};
    }
    const auto& slot = table.slots[idx];
    if (!slot.live)
    {
        return {};
    }
    // path 持有 std::string；返回它的 view 即可。生存期由 slot 决定，
    // 与 Get 返回的 const T* 一致——Unload 后失效。
    return slot.path;
}

}  // namespace Orange::Engine::Asset
