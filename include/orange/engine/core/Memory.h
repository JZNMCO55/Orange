#ifndef ORANGE_ENGINE_CORE_MEMORY_H
#define ORANGE_ENGINE_CORE_MEMORY_H

// ---------------------------------------------------------------------------
// Core::Memory —— per-category 字节级计数 API。
//
// 用法（模块 opt-in instrumentation）：
//
// 1. 启动期声明 category（一次性）：
//    ```
//    Core::Memory::RegisterCategory("Render::MeshGPU");
//    Core::Memory::RegisterCategory("Asset::Texture");
//    ```
//
// 2. 模块在 alloc / free 时手动调：
//    ```
//    Core::Memory::AddBytes("Render::MeshGPU", meshBytes);
//    Core::Memory::SubBytes("Render::MeshGPU", oldMeshBytes);
//    ```
//
// 3. UI 面板（OrangeEditor ProfilerPanel "Memory" tab）走 Snapshot() 读最新
//    counter 值，与 Profiler::Snapshot() 同节奏。
//
// **定位**：本 API 是 *逻辑层 opt-in*，不是 *allocator 级别拦截*——模块自
// 己决定要追什么，追多细。优点：零侵入新分配器、无 ABI 风险；缺点：未
// instrument 的内存（C++ stdlib 容器内部、第三方库内部）不会被算进来。
// 完整 allocator hook（heap-level tracking）是未来工作（非平凡 refactor）。
//
// **线程模型**：v0.9 范围内**仅主线程**（与 Profiler 同节奏）。多线程
// instrument 调用 AddBytes 是 UB。
//
// 公共头零第三方 include（与 Log / Profiler 同节奏）。后端见 src/core/Memory.cpp。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace Orange::Engine::Core::Memory
{

    struct CategorySnapshot
    {
        const char*   name           = nullptr; // 字符串字面量指针（caller 持有）
        std::uint64_t bytesCurrent   = 0;       // 当前累计 = sum(AddBytes) - sum(SubBytes)
        std::uint64_t bytesHighWater = 0;       // 历史高水位（自 Register 起）
        std::uint64_t allocCount     = 0;       // AddBytes 调用次数
        std::uint64_t freeCount      = 0;       // SubBytes 调用次数
    };

    // 注册一个 category。`name` 必须是寿命覆盖整个进程的字符串指针。重名
    // 调用 silent ignore，第一次定义生效（与 Profiler::DeclareSampleBin 同节奏）。
    ORANGE_ENGINE_API void RegisterCategory(const char* name);

    // 在指定 category 上累加 `bytes`；同时刷新 high-water mark。未注册的
    // category 名：silent skip（与 Profiler ScopedSampler 未注册 bin 同节奏）。
    ORANGE_ENGINE_API void AddBytes(const char* category, std::uint64_t bytes) noexcept;

    // 在指定 category 上减去 `bytes`；如果减下来 < 0 钳到 0（防止 double-free
    // 类计数错误把后续读出为 underflow 大数）。未注册的 category 名：silent skip。
    ORANGE_ENGINE_API void SubBytes(const char* category, std::uint64_t bytes) noexcept;

    // 全 category 当前快照。元素顺序 = RegisterCategory 调用顺序。返回的 span
    // 在下一次 RegisterCategory / AddBytes / SubBytes 之前都有效（v0.9 单线
    // 程下即下一次任意 Memory 调用之前）。
    ORANGE_ENGINE_API std::span<const CategorySnapshot> Snapshot() noexcept;

    // 当前已注册的 category 数。诊断 + 测试用。
    ORANGE_ENGINE_API std::size_t CategoryCount() noexcept;

    // 全清重置（清空所有 category + counter）。typical 用例：测试代码间隔离
    // / 编辑器 Play→Stop 切换。
    ORANGE_ENGINE_API void Reset() noexcept;

} // namespace Orange::Engine::Core::Memory

#endif // ORANGE_ENGINE_CORE_MEMORY_H
