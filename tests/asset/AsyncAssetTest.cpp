// AsyncAssetTest —— AssetRegistry::LoadAsync / IsLoaded / WaitFor 三个
// 公共面 + 与 sync Load 的 dedup 互通行为 + Registry 析构期间 worker
// 干净退出的端到端单元测试。
//
// 不依赖磁盘 IO：用一个内存里的 SlowLoader<int> 模拟 "loader 调一次需
// 要若干 ms 的 IO" 行为，比真正的 MeshLoader 更可控（ctest 跑得快、
// 不依赖 fixture 文件）。

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/IAssetLoader.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

using namespace std::chrono_literals;
using Orange::Engine::Result;
using Orange::Engine::ResultCode;
using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Asset::IAssetLoader;

namespace
{

    // 单个被加载的资源——封装一个 int payload + 来源 path（让 unit test
    // 可以观察 loader 是否真跑过、产生的资源对不对）。
    struct IntAsset
    {
        int         value;
        std::string sourcePath;
    };

    // 模拟"加载需要时间"的 loader：每次 Load 内部 sleep 50ms 再返回，让
    // 主线程有充足时间观察 Pending 状态。
    class SlowLoader : public IAssetLoader<IntAsset>
    {
    public:
        std::atomic<int>          callCount{0};
        std::chrono::milliseconds delay{50ms};

        Result<std::unique_ptr<IntAsset>, ResultCode> Load(std::string_view path) override
        {
            ++callCount;
            std::this_thread::sleep_for(delay);
            if (path == "fail")
            {
                return ResultCode::IoError;
            }
            auto asset        = std::make_unique<IntAsset>();
            asset->value      = static_cast<int>(path.size()); // 任意可观察值
            asset->sourcePath = std::string{path};
            return asset;
        }
    };

    void TestLoadAsyncReturnsImmediately()
    {
        AssetRegistry reg;
        auto*         rawLoader = new SlowLoader();
        auto          reg1      = reg.RegisterLoader<IntAsset>(std::unique_ptr<IAssetLoader<IntAsset>>(rawLoader));
        assert(reg1.IsOk());

        const auto t0           = std::chrono::steady_clock::now();
        auto       handleResult = reg.LoadAsync<IntAsset>("hello");
        const auto elapsed      = std::chrono::steady_clock::now() - t0;

        assert(handleResult.IsOk());
        auto handle = handleResult.Value();
        assert(handle.IsValid());

        // LoadAsync 应该几乎立即返回（毫秒级）；50ms 是 loader 内部 sleep，
        // LoadAsync 本身只入队 + notify。允许 25ms 调度抖动余量。
        assert(elapsed < 25ms);

        // 此刻 Get / IsLoaded 应该都看到 Pending 状态。
        assert(reg.IsLoaded(handle) == false);
        assert(reg.Get(handle) == nullptr);

        // 等齐：WaitFor 永等模式。
        const bool ok = reg.WaitFor(handle, 0ms);
        assert(ok);
        assert(reg.IsLoaded(handle));
        const auto* asset = reg.Get(handle);
        assert(asset != nullptr);
        assert(asset->value == 5); // "hello" → 5 字符
        assert(asset->sourcePath == "hello");
        assert(rawLoader->callCount.load() == 1);

        std::fprintf(stdout, "  [PASS] LoadAsync returns immediately, becomes Ready\n");
    }

    void TestLoadAsyncDedup()
    {
        AssetRegistry reg;
        auto*         rawLoader = new SlowLoader();
        rawLoader->delay        = 80ms;
        auto reg1               = reg.RegisterLoader<IntAsset>(std::unique_ptr<IAssetLoader<IntAsset>>(rawLoader));
        assert(reg1.IsOk());

        auto h1 = reg.LoadAsync<IntAsset>("dup");
        auto h2 = reg.LoadAsync<IntAsset>("dup");
        auto h3 = reg.LoadAsync<IntAsset>("dup");
        assert(h1.IsOk() && h2.IsOk() && h3.IsOk());
        // 三次 LoadAsync 同 path → 同 handle。
        assert(h1.Value().Value() == h2.Value().Value());
        assert(h2.Value().Value() == h3.Value().Value());

        reg.WaitFor(h1.Value(), 0ms);
        // loader 只调了一次（dedup 命中）。
        assert(rawLoader->callCount.load() == 1);

        std::fprintf(stdout, "  [PASS] LoadAsync same path dedups -> 1 loader call\n");
    }

    void TestSyncLoadWaitsForAsyncPending()
    {
        AssetRegistry reg;
        auto*         rawLoader = new SlowLoader();
        rawLoader->delay        = 100ms;
        auto reg1               = reg.RegisterLoader<IntAsset>(std::unique_ptr<IAssetLoader<IntAsset>>(rawLoader));
        assert(reg1.IsOk());

        // async 启动一个慢加载。
        auto asyncHandle = reg.LoadAsync<IntAsset>("shared").Value();

        // 立刻 sync Load 同 path —— 应当 block 在 readyCv 上直到 worker
        // 完成，最终拿到 *同一* handle，loader 不被重复调用。
        const auto t0         = std::chrono::steady_clock::now();
        auto       syncResult = reg.Load<IntAsset>("shared");
        const auto elapsed    = std::chrono::steady_clock::now() - t0;
        assert(syncResult.IsOk());
        assert(syncResult.Value().Value() == asyncHandle.Value());

        // sync Load 应该真等到了 worker 完成（≥ 50ms 余量），不是立即返回。
        assert(elapsed >= 50ms);
        assert(rawLoader->callCount.load() == 1);

        // Get 现在能拿到 asset。
        const auto* asset = reg.Get(asyncHandle);
        assert(asset != nullptr);
        assert(asset->sourcePath == "shared");

        std::fprintf(stdout, "  [PASS] sync Load blocks until async pending slot completes\n");
    }

    void TestWaitForTimeout()
    {
        AssetRegistry reg;
        auto*         rawLoader = new SlowLoader();
        rawLoader->delay        = 200ms; // 故意慢，让 timeout 跑满
        auto reg1               = reg.RegisterLoader<IntAsset>(std::unique_ptr<IAssetLoader<IntAsset>>(rawLoader));
        assert(reg1.IsOk());

        auto handle = reg.LoadAsync<IntAsset>("slow").Value();

        // 30ms 超时——loader 200ms 还没跑完，应当返回 false（仍 Pending）。
        const bool finishedWithin30ms = reg.WaitFor(handle, 30ms);
        assert(finishedWithin30ms == false);
        assert(reg.IsLoaded(handle) == false);

        // 永等模式拿到结果。
        const bool finished = reg.WaitFor(handle, 0ms);
        assert(finished);
        assert(reg.IsLoaded(handle));

        std::fprintf(stdout, "  [PASS] WaitFor honors timeout, then succeeds with infinite wait\n");
    }

    void TestLoadAsyncFailedStatus()
    {
        AssetRegistry reg;
        auto*         rawLoader = new SlowLoader();
        auto          reg1      = reg.RegisterLoader<IntAsset>(std::unique_ptr<IAssetLoader<IntAsset>>(rawLoader));
        assert(reg1.IsOk());

        auto handle = reg.LoadAsync<IntAsset>("fail").Value();
        reg.WaitFor(handle, 0ms);
        // Failed 状态：IsLoaded false，Get 返回 nullptr。
        assert(reg.IsLoaded(handle) == false);
        assert(reg.Get(handle) == nullptr);

        std::fprintf(stdout, "  [PASS] failed async load -> Failed status, Get nullptr\n");
    }

    void TestRegistryDestructionWithPendingJobs()
    {
        // Registry 在 worker 还有大量 pending job 时立即析构——StopAndJoin
        // 应当干净退出，没有死锁、没有 use-after-free。
        auto  reg        = std::make_unique<AssetRegistry>();
        auto* rawLoader  = new SlowLoader();
        rawLoader->delay = 100ms;
        auto reg1        = reg->RegisterLoader<IntAsset>(std::unique_ptr<IAssetLoader<IntAsset>>(rawLoader));
        assert(reg1.IsOk());

        // 入队 10 个 job，让 worker 来不及全跑完。
        for (int i = 0; i < 10; ++i)
        {
            const std::string path = "job_" + std::to_string(i);
            reg->LoadAsync<IntAsset>(path);
        }

        // 立即销毁 registry——内部 StopAndJoin 应该能让 worker 安全退出。
        // 验证：销毁不会崩、不会死锁（10s 超时对照）。
        const auto t0 = std::chrono::steady_clock::now();
        reg.reset();
        const auto elapsed = std::chrono::steady_clock::now() - t0;
        // worker 至多在跑当前 job + 当前 IO 完成（100ms 内）就退出。
        assert(elapsed < 5s);

        std::fprintf(stdout, "  [PASS] registry destruction with pending jobs is safe (took %lldms)\n",
                     static_cast<long long>(
                         std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()));
    }

    void TestLoadAsyncWithoutLoaderReturnsUnsupported()
    {
        AssetRegistry reg; // 没有 RegisterLoader<IntAsset>

        auto result = reg.LoadAsync<IntAsset>("x");
        assert(result.IsErr());
        assert(result.Error() == ResultCode::Unsupported);

        std::fprintf(stdout, "  [PASS] LoadAsync without loader -> Unsupported\n");
    }

} // namespace

int main()
{
    std::fprintf(stdout, "[AsyncAssetTest] running\n");
    TestLoadAsyncReturnsImmediately();
    TestLoadAsyncDedup();
    TestSyncLoadWaitsForAsyncPending();
    TestWaitForTimeout();
    TestLoadAsyncFailedStatus();
    TestRegistryDestructionWithPendingJobs();
    TestLoadAsyncWithoutLoaderReturnsUnsupported();
    std::fprintf(stdout, "[AsyncAssetTest] all tests passed.\n");
    return 0;
}
