#ifndef ORANGE_ENGINE_CORE_PROFILER_H
#define ORANGE_ENGINE_CORE_PROFILER_H

// ---------------------------------------------------------------------------
// Core::Profiler —— in-game 分层 profiler（AutoProfile RAII + sample bin 树）。
//
// 设计来源：vendor/Orange-Wiki/wiki/techniques/debugging/in-game-profiler.md
// （Gregory 2018 §10.8）。**自实现**路径，不依赖 Tracy；ADR-003 选定方案 B
// 的工程落地。Tracy gate（ORANGE_ENGINE_WITH_TRACY）保留 CMake stub 不动。
//
// 使用方式（典型路径）：
//
// 1. 启动期声明 sample bin 树（一次性）：
//    ```
//    Core::Profiler::DeclareSampleBin("Frame");
//    Core::Profiler::DeclareSampleBin("Render", "Frame");
//    Core::Profiler::DeclareSampleBin("Physics", "Frame");
//    Core::Profiler::DeclareSampleBin("Shadow",  "Render");
//    ```
//    或用 ORANGE_PROFILE_DECLARE_BIN(name, parent) 宏在 TU 内 lazy 声明。
//
// 2. 热点路径用宏 / RAII 圈作用域：
//    ```
//    void Render() {
//        ORANGE_PROFILE_SCOPE("Render");
//        RenderShadows();      // 内部含 ORANGE_PROFILE_SCOPE("Shadow")
//        RenderMain();
//    }
//    ```
//
// 3. AppHost 帧末 / Layer 末尾调一次 FinalizeFrame()：本帧 inclusive 时间
//    汇总 + 计算 exclusive（inclusive - sum(child.inclusive)）+ 写入 snapshot
//    + 重置 live 累加器。
//
// 4. UI 面板（OrangeEditor ProfilerPanel）通过 Snapshot() 读最新帧数据
//    （inclusive_ms / exclusive_ms / call_count）渲染树形 + 柱状图。
//
// 线程模型：**仅主线程**。未来 job system 上线时扩 thread-local sample bin
// + lock-free 聚合（ADR-003 已登记 future work）。当前在非主线程调用
// ScopedSampler 是 UB（无 lock 保护内部 map）。
//
// 性能：ScopedSampler ctor/dtor 各一次 steady_clock::now()（典型 QPC 路径，
// ~20-50ns）+ unordered_map lookup（~100ns）。100k+ sample/s 量级不显著
// 影响 frame time；hot inner loop 不建议每次迭代都 PROFILE_SCOPE。
//
// 公共头零第三方 include（与 Log.h 同节奏）。后端实现见 src/core/Profiler.cpp。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Orange::Engine::Core::Profiler
{

// 单个 sample bin 的最新一帧 snapshot。FinalizeFrame() 写入；Snapshot()
// 返回 std::span 对应稳定段（下一次 FinalizeFrame 之前不失效）。
struct SampleBinSnapshot
{
    const char*   name        = nullptr;       // 字符串字面量指针（DeclareSampleBin 调用方持有）
    const char*   parentName  = nullptr;       // nullptr = root
    double        inclusiveMs = 0.0;           // 含子函数耗时
    double        exclusiveMs = 0.0;           // 排除子函数耗时 = inclusive - sum(child.inclusive)
    std::uint32_t callCount   = 0;             // 本帧 ScopedSampler 实例化次数
};

// 注册一个 sample bin。`name` / `parent` 必须是寿命覆盖整个进程的字符串
// 指针（典型：字符串字面量）。重名调用：第二次 silent ignore，第一次定义
// 生效（避免 ODR-like 双重声明意外）。`parent = nullptr` 表示 root bin。
//
// 此调用 thread-unsafe；典型 startup 期单线程调用 + ORANGE_PROFILE_DECLARE_BIN
// 宏的 file-static 初始化都成立。
ORANGE_ENGINE_API void DeclareSampleBin(const char* name,
                                         const char* parent = nullptr);

// RAII scope sampler。构造记录 hi-res 起点；析构计算 elapsed 并累加到
// 对应 bin。`binName` 必须是已 Declare 过的 bin 名指针；未 Declare
// 的名字：dtor silent skip（不 crash，便于"先插 PROFILE_SCOPE 再补 declare"
// 的迭代流程，但这样数据不会被统计）。
class ORANGE_ENGINE_API ScopedSampler
{
public:
    explicit ScopedSampler(const char* binName) noexcept;
    ~ScopedSampler() noexcept;

    ScopedSampler(const ScopedSampler&)            = delete;
    ScopedSampler& operator=(const ScopedSampler&) = delete;
    ScopedSampler(ScopedSampler&&)                 = delete;
    ScopedSampler& operator=(ScopedSampler&&)      = delete;

private:
    const char*  mName;
    std::int64_t mStartTick;
};

// 帧末调用一次（典型：AppHost 主循环 frame 末尾）：
//   1. 对每个 bin，exclusive = inclusive - sum(child.inclusive)
//   2. 把 live 累加器 atomically 拷到 snapshot；
//   3. 重置 live 累加器（inclusive_ms = 0, call_count = 0）准备下一帧。
//
// Snapshot() 返回的数据在两次 FinalizeFrame 之间稳定；UI 面板典型路径是
// "每帧读一次 + 渲染"。
ORANGE_ENGINE_API void FinalizeFrame() noexcept;

// 当前已 finalize 帧的 bin 快照。返回 span 在下一次 FinalizeFrame 调用
// 之前都有效。元素顺序：按 DeclareSampleBin 调用顺序（稳定，便于 UI
// 上下次帧布局保持一致）。
ORANGE_ENGINE_API std::span<const SampleBinSnapshot> Snapshot() noexcept;

// 当前注册的 bin 数（含 root / 含未在 Snapshot 出现的）。诊断 + 测试用。
ORANGE_ENGINE_API std::size_t BinCount() noexcept;

// 重置整个 profiler 状态（清空所有 bin + 累加器 + snapshot）。typical 用
// 例：测试代码间隔离 / 编辑器 Play→Stop 切换。
ORANGE_ENGINE_API void Reset() noexcept;

}  // namespace Orange::Engine::Core::Profiler

// ---------------------------------------------------------------------------
// 便捷宏
// ---------------------------------------------------------------------------

#define ORANGE_PROFILE_CONCAT_INNER_(a, b) a##b
#define ORANGE_PROFILE_CONCAT_(a, b) ORANGE_PROFILE_CONCAT_INNER_(a, b)

// 在当前作用域圈一段 sample。name 是已 Declare 过的 bin 名指针（典型：
// 字符串字面量；用 const char* 变量也可，但要保证 lifetime 覆盖到帧末）。
#define ORANGE_PROFILE_SCOPE(name) \
    ::Orange::Engine::Core::Profiler::ScopedSampler \
        ORANGE_PROFILE_CONCAT_(orangeProfile_, __LINE__){(name)}

// 在文件作用域声明一个 sample bin。`name` / `parent` 必须是寿命覆盖整
// 个进程的字符串字面量。本宏展开为一个 namespace-scope static bool，
// 初始化器调用 DeclareSampleBin —— main() 之前自动注册。
#define ORANGE_PROFILE_DECLARE_BIN(name, parent)                              \
    namespace                                                                 \
    {                                                                         \
    [[maybe_unused]] const bool ORANGE_PROFILE_CONCAT_(orangeProfileBin_,     \
                                                       __LINE__) =            \
        (::Orange::Engine::Core::Profiler::DeclareSampleBin((name), (parent)),\
         true);                                                               \
    }

#endif  // ORANGE_ENGINE_CORE_PROFILER_H
