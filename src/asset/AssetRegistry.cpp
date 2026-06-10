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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
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

// Async 加载状态机：
//   * Pending —— slot 已分配 path、handle 已发出，但 asset 还在 worker
//     线程加载中；Get 返回 nullptr，IsLoaded == false。
//   * Ready   —— asset 已写入 slot.asset，正常可消费。
//   * Failed  —— loader 返回错误；slot.asset == nullptr，IsLoaded ==
//     false（与 Pending 区别仅在"是否还在等"——sync waiters 看到
//     Failed 不再等下去）。
enum class LoadStatus : std::uint8_t
{
    Pending,
    Ready,
    Failed,
};

struct AssetSlot
{
    std::string path;
    void*       asset{nullptr};
    bool        live{false};
    LoadStatus  status{LoadStatus::Ready};
};

struct AssetTable
{
    std::vector<AssetSlot> slots;
    std::vector<std::uint64_t> freeList;
    std::unordered_map<std::string, std::uint64_t> pathToHandle;
    void (*assetDeleter)(void*) noexcept{nullptr};
};

// Worker thread 任务条目——type + handle 唯一定位 slot；path 在 worker
// 释放锁后调 loader 时复用（不再回头查表）。
struct AsyncJob
{
    std::type_index type;
    std::uint64_t   handleValue;
    std::string     path;
};

}  // namespace

struct AssetRegistry::Impl
{
    // 单一 mutex 覆盖 loaders / tables / liveAssets / jobs 全部表写操作。
    // worker 调 loader 期间释放锁——loader 的 IO 不应阻塞主线程的其它
    // Load / Get / Insert 路径。GetErased / IsLoadedErased / WaitForErased
    // 也持锁——0.x 内 Get 频次不高（mesh GPU 上传只在新 entity 添加时），
    // 单一 mutex 足够；后续若成 hot path 再换 reader-writer / lockfree。
    mutable std::mutex                               tablesMutex;
    // 注：mJobsCv 与 mReadyCv 共享 tablesMutex，避免维护两个独立锁的
    // ABA 与持锁顺序难题。jobs 上有任务时 worker 醒；slot status 变化
    // 时 sync waiters 醒。
    std::condition_variable                          jobsCv;
    std::condition_variable                          readyCv;

    std::unordered_map<std::type_index, LoaderEntry> loaders;
    std::unordered_map<std::type_index, AssetTable>  tables;
    std::size_t                                      liveAssets{0};

    // Async worker 任务队列 + 控制信号。
    std::deque<AsyncJob> jobs;
    std::atomic<bool>    shutdown{false};
    std::thread          worker;

    void Start();
    void StopAndJoin() noexcept;
    void WorkerLoop();
};

void AssetRegistry::Impl::Start()
{
    shutdown.store(false);
    worker = std::thread([this]() { WorkerLoop(); });
}

void AssetRegistry::Impl::StopAndJoin() noexcept
{
    {
        std::lock_guard<std::mutex> lock(tablesMutex);
        shutdown.store(true);
    }
    jobsCv.notify_all();
    if (worker.joinable())
    {
        worker.join();
    }
    // 析构期间没消费完的 job —— pending slot 留给 Impl 析构里 path/asset
    // 走默认释放路径；async waiters 在 readyCv 上的等待应已经在 stop 之
    // 前由调用方自己 join / wait 完成（registry 析构 = 全套 shut-down，
    // 此时还在 Wait 的代码本身就是 use-after-free 风险，不是本路径要兜
    // 底的问题）。
    jobs.clear();
}

void AssetRegistry::Impl::WorkerLoop()
{
    for (;;)
    {
        std::unique_lock<std::mutex> lock(tablesMutex);
        jobsCv.wait(lock, [this] { return shutdown.load() || !jobs.empty(); });

        if (shutdown.load() && jobs.empty())
        {
            return;
        }

        AsyncJob job = std::move(jobs.front());
        jobs.pop_front();

        // 拷一份 loader 信息——执行 invoker 之前会释放锁，loader 表
        // 间隔被 hot-swap 的概率虽低，但保留快照避免 use-after-modify。
        auto loaderIt = loaders.find(job.type);
        if (loaderIt == loaders.end())
        {
            // loader 被卸了？slot 标 Failed 让 waiters 早退。
            auto tableIt = tables.find(job.type);
            if (tableIt != tables.end())
            {
                auto& tab = tableIt->second;
                const std::size_t idx = static_cast<std::size_t>(job.handleValue - 1);
                if (idx < tab.slots.size() && tab.slots[idx].live)
                {
                    tab.slots[idx].status = LoadStatus::Failed;
                }
            }
            readyCv.notify_all();
            continue;
        }
        const LoaderEntry loaderSnapshot = loaderIt->second;

        // 释放锁跑 loader——IO 不阻塞主线程的其它 Load / Get。
        lock.unlock();
        void* assetRaw = nullptr;
        const ResultCode rc =
            loaderSnapshot.invoker(loaderSnapshot.loader, job.path, &assetRaw);
        lock.lock();

        auto tableIt = tables.find(job.type);
        if (tableIt == tables.end())
        {
            // 极端：tables 被擦了（理论上 0.x 不会发生；只在 registry
            // 析构期间才可能擦表，那时 shutdown 也已 true）。释放产物。
            if (assetRaw && loaderSnapshot.assetDeleter)
            {
                loaderSnapshot.assetDeleter(assetRaw);
            }
            continue;
        }
        auto& tab = tableIt->second;
        const std::size_t idx = static_cast<std::size_t>(job.handleValue - 1);
        // 仅判 live 不够：slot 可能在 IO 期间被 Unload 后**复用**绑到另一 path
        // （live 又翻回 true）。补 path 校验——slot 当前 path 不再是本 job 的
        // path 则视为孤儿，释放产物并 drop，避免回写污染他人 slot。
        if (idx >= tab.slots.size() || !tab.slots[idx].live
            || tab.slots[idx].path != job.path)
        {
            // slot 在 IO 期间被 Unload / 复用 —— 资源孤儿了，释放并 drop。
            if (assetRaw && loaderSnapshot.assetDeleter)
            {
                loaderSnapshot.assetDeleter(assetRaw);
            }
            continue;
        }
        auto& slot = tab.slots[idx];

        if (rc != ResultCode::Ok || assetRaw == nullptr)
        {
            if (assetRaw && loaderSnapshot.assetDeleter)
            {
                loaderSnapshot.assetDeleter(assetRaw);
            }
            slot.status = LoadStatus::Failed;
        }
        else
        {
            slot.asset  = assetRaw;
            slot.status = LoadStatus::Ready;
            ++liveAssets;
        }
        readyCv.notify_all();
    }
}

AssetRegistry::AssetRegistry() : mpImpl(std::make_unique<Impl>())
{
    mpImpl->Start();
}

AssetRegistry::~AssetRegistry()
{
    if (mpImpl)
    {
        // 析构顺序：先停 worker（可能还在 mJobsCv 上等），再清表。
        // worker join 之前不能动表——worker 可能正在持锁修 slot。
        mpImpl->StopAndJoin();

        // 再走原路径释放：先逐 table 释放 asset，再逐 loader 释放
        // loader 实例。两次都用各自的 deleter——避免依赖 T 在此 TU
        // 是否完整。
        for (auto& [type, table] : mpImpl->tables)
        {
            for (auto& slot : table.slots)
            {
                if (slot.live && slot.asset && table.assetDeleter)
                {
                    table.assetDeleter(slot.asset);
                }
            }
        }
        mpImpl->tables.clear();

        for (auto& [type, entry] : mpImpl->loaders)
        {
            if (entry.loader && entry.loaderDeleter)
            {
                entry.loaderDeleter(entry.loader);
            }
        }
        mpImpl->loaders.clear();
    }
}

AssetRegistry::AssetRegistry(AssetRegistry&&) noexcept            = default;
AssetRegistry& AssetRegistry::operator=(AssetRegistry&&) noexcept = default;

std::size_t AssetRegistry::Size() const noexcept
{
    if (!mpImpl)
    {
        return 0;
    }
    std::lock_guard<std::mutex> lock(mpImpl->tablesMutex);
    return mpImpl->liveAssets;
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

    std::lock_guard<std::mutex> lock(mpImpl->tablesMutex);
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
    const std::string     pathKey{path};

    std::unique_lock<std::mutex> lock(mpImpl->tablesMutex);
    auto& table = mpImpl->tables[key];

    // dedup：path 命中已加载的 slot → 看 slot 状态决定走哪条。
    //
    // 这一步刻意排在"loader 是否注册"检查之前——Insert 路径先于
    // RegisterLoader 把资源挂到 pathToHandle 是合法用法（程序式构造
    // 资源、scene 序列化前注入资源等），此时 Load 同 path 同类型应直
    // 接命中 cache，不应该因为缺 loader 而报 Unsupported。
    if (auto cacheIt = table.pathToHandle.find(pathKey);
        cacheIt != table.pathToHandle.end())
    {
        const std::uint64_t cached = cacheIt->second;
        const std::size_t idx = static_cast<std::size_t>(cached - 1);
        if (idx < table.slots.size() && table.slots[idx].live)
        {
            // Pending 状态：等 worker 完成。Failed 状态：直接返回 handle，
            // 调用方按 IsLoaded == false 处理（不在这里自动 retry，避免
            // 隐式重新 IO 把 sync caller 卡住——retry 留给调用方主动
            // Unload+Load）。
            if (table.slots[idx].status == LoadStatus::Pending)
            {
                mpImpl->readyCv.wait(lock, [&]() {
                    return table.slots[idx].status != LoadStatus::Pending;
                });
            }
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
    const LoaderEntry loaderSnapshot = loaderIt->second;

    // 先分配 Pending slot 并把 path 挂进 dedup 表——之后释放锁跑 IO，
    // 同 path 的并发 Load / LoadAsync 看到 Pending 会等而不是重复跑。
    std::uint64_t slotIndex = 0;
    if (!table.freeList.empty())
    {
        slotIndex = table.freeList.back();
        table.freeList.pop_back();
        auto& slot = table.slots[static_cast<std::size_t>(slotIndex)];
        slot.path   = pathKey;
        slot.asset  = nullptr;
        slot.live   = true;
        slot.status = LoadStatus::Pending;
    }
    else
    {
        slotIndex = static_cast<std::uint64_t>(table.slots.size());
        AssetSlot slot{};
        slot.path   = pathKey;
        slot.asset  = nullptr;
        slot.live   = true;
        slot.status = LoadStatus::Pending;
        table.slots.emplace_back(std::move(slot));
    }
    const std::uint64_t handleValue = slotIndex + 1;
    table.pathToHandle.emplace(pathKey, handleValue);

    // 释放锁跑 loader IO——同 path 并发 Load 会等本 slot 变 Ready；其
    // 它 path / 其它类型不受阻塞。
    lock.unlock();
    void* assetRaw = nullptr;
    ResultCode rc = loaderSnapshot.invoker(loaderSnapshot.loader, path, &assetRaw);
    lock.lock();

    auto& slotAfter = mpImpl->tables[key].slots[static_cast<std::size_t>(slotIndex)];

    // IO 期间释放了 tablesMutex：本 Pending slot 可能被 Unload（idx 进 freeList）
    // 并被另一次 Load 复用、绑定到**不同 path**（live 又翻回 true）。仅判 live
    // 不足——必须同时校验 path 仍是 pathKey 且仍 Pending，否则无条件回写会把本次
    // 加载结果写进别人的 slot（张冠李戴）+ 泄漏。不再属于本次 Load 时，释放刚加载
    // 出来的孤儿 asset 并以错误返回（本次 Load 已在 IO 期间被 Unload 取消）。
    if (!slotAfter.live || slotAfter.path != pathKey
        || slotAfter.status != LoadStatus::Pending)
    {
        if (assetRaw && loaderSnapshot.assetDeleter)
        {
            loaderSnapshot.assetDeleter(assetRaw);
        }
        mpImpl->readyCv.notify_all();
        return ResultCode::InternalError;
    }

    if (rc != ResultCode::Ok)
    {
        if (assetRaw && loaderSnapshot.assetDeleter)
        {
            loaderSnapshot.assetDeleter(assetRaw);
        }
        slotAfter.status = LoadStatus::Failed;
        mpImpl->readyCv.notify_all();
        return rc;
    }
    if (assetRaw == nullptr)
    {
        slotAfter.status = LoadStatus::Failed;
        mpImpl->readyCv.notify_all();
        return ResultCode::InternalError;
    }

    slotAfter.asset  = assetRaw;
    slotAfter.status = LoadStatus::Ready;
    ++mpImpl->liveAssets;
    mpImpl->readyCv.notify_all();
    outHandle = handleValue;
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

    std::lock_guard<std::mutex> lock(mpImpl->tablesMutex);
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
            slot.asset  = assetRaw;
            slot.status = LoadStatus::Ready;  // Insert 提供现成资源
            // path 不变；live 保持 true。Insert 完成可能解锁等 Pending
            // 的 Load（虽然 Pending → Insert 同 path 是不太自然的用法）。
            mpImpl->readyCv.notify_all();
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
        slot.path   = pathKey;
        slot.asset  = assetRaw;
        slot.live   = true;
        slot.status = LoadStatus::Ready;
    }
    else
    {
        slotIndex = static_cast<std::uint64_t>(table.slots.size());
        AssetSlot slot{};
        slot.path   = pathKey;
        slot.asset  = assetRaw;
        slot.live   = true;
        slot.status = LoadStatus::Ready;
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
    std::lock_guard<std::mutex> lock(mpImpl->tablesMutex);
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
    // Pending / Failed 状态不暴露 asset：consumer 按 nullptr 处理（与
    // 既有 Get 的"无效 / 已卸载 → nullptr"路径对齐）。
    if (!slot.live || slot.status != LoadStatus::Ready)
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
    std::lock_guard<std::mutex> lock(mpImpl->tablesMutex);
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
    std::lock_guard<std::mutex> lock(mpImpl->tablesMutex);
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
    // 与 Get 返回的 const T* 一致——Unload 后失效。Pending 状态下 path
    // 仍然合法（async 入队时已写入），返回不阻塞。
    return slot.path;
}

ResultCode AssetRegistry::LoadAsyncErased(const std::type_info& type,
                                          std::string_view path,
                                          std::uint64_t& outHandle)
{
    outHandle = 0;
    const std::type_index key{type};
    const std::string     pathKey{path};

    std::lock_guard<std::mutex> lock(mpImpl->tablesMutex);
    auto& table = mpImpl->tables[key];

    // dedup：路径已在表（Pending / Ready / Failed 任一状态）→ 复用。
    // 不重排队避免重复 IO；调用方按 IsLoaded / WaitFor 等齐。
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
        table.pathToHandle.erase(cacheIt);
    }

    // 必须有 loader 注册——async 没法跨线程 fall-through 到 Insert 兜底。
    auto loaderIt = mpImpl->loaders.find(key);
    if (loaderIt == mpImpl->loaders.end())
    {
        return ResultCode::Unsupported;
    }

    // 分配 Pending slot + 入 dedup 表。
    std::uint64_t slotIndex = 0;
    if (!table.freeList.empty())
    {
        slotIndex = table.freeList.back();
        table.freeList.pop_back();
        auto& slot = table.slots[static_cast<std::size_t>(slotIndex)];
        slot.path   = pathKey;
        slot.asset  = nullptr;
        slot.live   = true;
        slot.status = LoadStatus::Pending;
    }
    else
    {
        slotIndex = static_cast<std::uint64_t>(table.slots.size());
        AssetSlot slot{};
        slot.path   = pathKey;
        slot.asset  = nullptr;
        slot.live   = true;
        slot.status = LoadStatus::Pending;
        table.slots.emplace_back(std::move(slot));
    }
    const std::uint64_t handleValue = slotIndex + 1;
    table.pathToHandle.emplace(pathKey, handleValue);

    // 入队 worker job + 唤醒 worker。
    AsyncJob job{key, handleValue, pathKey};
    mpImpl->jobs.emplace_back(std::move(job));
    mpImpl->jobsCv.notify_one();

    outHandle = handleValue;
    return ResultCode::Ok;
}

bool AssetRegistry::IsLoadedErased(const std::type_info& type,
                                   std::uint64_t handleValue) const noexcept
{
    if (handleValue == 0)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(mpImpl->tablesMutex);
    const std::type_index key{type};
    auto it = mpImpl->tables.find(key);
    if (it == mpImpl->tables.end())
    {
        return false;
    }
    const auto& table = it->second;
    const std::size_t idx = static_cast<std::size_t>(handleValue - 1);
    if (idx >= table.slots.size())
    {
        return false;
    }
    const auto& slot = table.slots[idx];
    return slot.live && slot.status == LoadStatus::Ready;
}

bool AssetRegistry::WaitForErased(const std::type_info& type,
                                  std::uint64_t handleValue,
                                  std::int64_t timeoutMs) const noexcept
{
    if (handleValue == 0)
    {
        return false;
    }
    std::unique_lock<std::mutex> lock(mpImpl->tablesMutex);
    const std::type_index key{type};
    auto it = mpImpl->tables.find(key);
    if (it == mpImpl->tables.end())
    {
        return false;
    }
    auto& table = it->second;
    const std::size_t idx = static_cast<std::size_t>(handleValue - 1);
    if (idx >= table.slots.size() || !table.slots[idx].live)
    {
        return false;
    }
    // 不能持久化 `table.slots[idx]` 的引用跨 readyCv.wait：wait 期间释放
    // tablesMutex，并发 Load/Insert 的 slots.emplace_back 可能让 vector
    // 扩容重分配，使该引用悬垂 → predicate / 返回语句读 slot.status 即 UAF。
    // slots 只增不缩（Unload 走 freeList 标记，不 erase），故 idx 恒有效——
    // predicate 与返回每次重索引 table.slots[idx] 取当前有效地址。
    // （table 是 map mapped value 的引用，对 map/unordered_map 的 insert 都
    // 稳定，可安全保留。）
    if (table.slots[idx].status != LoadStatus::Pending)
    {
        return true;  // 已经 Ready / Failed
    }

    auto pred = [&]() {
        return table.slots[idx].status != LoadStatus::Pending
               || mpImpl->shutdown.load();
    };

    if (timeoutMs <= 0)
    {
        // 永等
        mpImpl->readyCv.wait(lock, pred);
    }
    else
    {
        const auto until = std::chrono::steady_clock::now()
                         + std::chrono::milliseconds(timeoutMs);
        if (!mpImpl->readyCv.wait_until(lock, until, pred))
        {
            return false;  // 超时仍 Pending
        }
    }
    // shutdown 触发的也算"不再 Pending"；调用方拿到 false 时是从
    // IsLoaded 判定 Ready/Failed。重索引（理由同上，不持悬垂引用）。
    return table.slots[idx].status != LoadStatus::Pending;
}

}  // namespace Orange::Engine::Asset
