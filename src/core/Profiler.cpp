#include <orange/engine/core/Profiler.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Orange::Engine::Core::Profiler
{

namespace
{

// 单 bin 内部状态：
//   * declaration 段：name / parent 在 DeclareSampleBin 时填，之后不变
//   * live 累加段：ScopedSampler dtor 通过 RecordSample 写
//   * snapshot 段：FinalizeFrame 时从 live 拷过来 + 计算 exclusive
// 把 live 与 snapshot 分开避免 UI 读 snapshot 时与 instrumentation 写
// live 段冲突（v0.9 单线程不要求 atomic，但分离让未来扩多线程时只锁
// live 段）。
struct SampleBin
{
    const char*   name        = nullptr;
    const char*   parentName  = nullptr;

    // live 累加（每帧重置）
    double        liveInclusiveMs = 0.0;
    std::uint32_t liveCallCount   = 0;

    // 最近一帧 snapshot（FinalizeFrame 写入；Snapshot() 暴露索引到外部）
    double        snapInclusiveMs = 0.0;
    double        snapExclusiveMs = 0.0;
    std::uint32_t snapCallCount   = 0;
};

// 内部状态。函数局部静态保证 main 之前 ORANGE_PROFILE_DECLARE_BIN 宏的
// file-static 初始化也能正确触发 lazy 构造（避免 static-init-fiasco）。
struct State
{
    // 顺序保存所有 bin（按 DeclareSampleBin 调用次序）。UI 渲染时直接
    // 遍历这个数组即可拿到稳定顺序。
    std::vector<SampleBin> bins;

    // name → index 反查（ScopedSampler 析构时 O(1) 找 bin）。key 是
    // 字符串字面量指针，但 hash 走指针 value（同一字面量在同 TU 可能
    // 出现多次但 const char* 指针未必一致，所以用 string_view hash）。
    std::unordered_map<std::string_view, std::size_t> nameToIndex;

    // 给 UI 暴露的 snapshot 视图（FinalizeFrame 后写入）。与 bins 同序。
    std::vector<SampleBinSnapshot> snapshot;
};

State& GetState()
{
    static State s;
    return s;
}

// hi-res 时钟。std::chrono::steady_clock 在 Windows 上典型是 QPC 路径，
// ~ns 分辨率；Linux 是 CLOCK_MONOTONIC。
using Clock = std::chrono::steady_clock;

std::int64_t Now()
{
    return Clock::now().time_since_epoch().count();
}

double TicksToMs(std::int64_t ticks)
{
    // Clock::duration 是 nanoseconds（typical）；这里用 chrono 转换避免
    // 假设具体单位。
    using NsRatio = std::ratio_divide<Clock::period, std::milli>;
    return static_cast<double>(ticks) *
           static_cast<double>(NsRatio::num) /
           static_cast<double>(NsRatio::den);
}

}  // namespace

void DeclareSampleBin(const char* name, const char* parent)
{
    if (name == nullptr)
    {
        return;
    }
    auto& s = GetState();
    if (s.nameToIndex.find(std::string_view{name}) != s.nameToIndex.end())
    {
        // 重名 silent ignore：第一次定义生效，避免 ODR-like 双重声明意外。
        return;
    }
    s.bins.push_back(SampleBin{name, parent, 0.0, 0, 0.0, 0.0, 0});
    s.nameToIndex.emplace(std::string_view{name}, s.bins.size() - 1);
    s.snapshot.push_back(SampleBinSnapshot{name, parent, 0.0, 0.0, 0});
}

ScopedSampler::ScopedSampler(const char* binName) noexcept
    : mName(binName), mStartTick(Now())
{
}

ScopedSampler::~ScopedSampler() noexcept
{
    if (mName == nullptr)
    {
        return;
    }
    const std::int64_t elapsed = Now() - mStartTick;
    auto& s = GetState();
    auto it = s.nameToIndex.find(std::string_view{mName});
    if (it == s.nameToIndex.end())
    {
        // 未声明的 bin —— silent skip（参 Profiler.h 文档：方便先插宏后补声明）
        return;
    }
    auto& bin = s.bins[it->second];
    bin.liveInclusiveMs += TicksToMs(elapsed);
    ++bin.liveCallCount;
}

void FinalizeFrame() noexcept
{
    auto& s = GetState();

    // Pass 1：把 live 段拷到 snapshot 段（inclusive + callCount）。
    for (auto& bin : s.bins)
    {
        bin.snapInclusiveMs = bin.liveInclusiveMs;
        bin.snapCallCount   = bin.liveCallCount;
    }

    // Pass 2：对每个 bin 计算 exclusive = inclusive - sum(child.inclusive)。
    // 直接 O(N²) 扫——v0.9 期 bin 数十量级，不上索引。
    for (auto& bin : s.bins)
    {
        double childSum = 0.0;
        for (const auto& maybeChild : s.bins)
        {
            if (maybeChild.parentName == nullptr) { continue; }
            if (std::string_view{maybeChild.parentName} == std::string_view{bin.name})
            {
                childSum += maybeChild.snapInclusiveMs;
            }
        }
        bin.snapExclusiveMs = bin.snapInclusiveMs - childSum;
        if (bin.snapExclusiveMs < 0.0) { bin.snapExclusiveMs = 0.0; }
    }

    // Pass 3：把 snapshot 写到 UI 暴露的 std::span 容器；reset live 累加器。
    for (std::size_t i = 0; i < s.bins.size(); ++i)
    {
        auto& bin = s.bins[i];
        s.snapshot[i].name        = bin.name;
        s.snapshot[i].parentName  = bin.parentName;
        s.snapshot[i].inclusiveMs = bin.snapInclusiveMs;
        s.snapshot[i].exclusiveMs = bin.snapExclusiveMs;
        s.snapshot[i].callCount   = bin.snapCallCount;
        bin.liveInclusiveMs       = 0.0;
        bin.liveCallCount         = 0;
    }
}

std::span<const SampleBinSnapshot> Snapshot() noexcept
{
    const auto& s = GetState();
    return std::span<const SampleBinSnapshot>{s.snapshot};
}

std::size_t BinCount() noexcept
{
    return GetState().bins.size();
}

void Reset() noexcept
{
    auto& s = GetState();
    s.bins.clear();
    s.snapshot.clear();
    s.nameToIndex.clear();
}

}  // namespace Orange::Engine::Core::Profiler
