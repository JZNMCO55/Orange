#ifndef ORANGE_ENGINE_CORE_LOG_H
#define ORANGE_ENGINE_CORE_LOG_H

// ---------------------------------------------------------------------------
// Core::Log —— 日志门面。公共头不依赖任何第三方：
// * spdlog（在 ORANGE_ENGINE_WITH_SPDLOG=ON 时是默认 backend）只在
//   src/core/Log.cpp 中包含；公共消费者看不到 spdlog 的任何符号。
// * 格式化走 std::format（C++20）——与 fmtlib 同语法，公共头无需新增依赖。
//
// 调用日志请用 ORANGE_LOG_* 宏，而不是直接调用 Log::Format：
// 宏对 early-out 友好，并保留了未来在调用点注入 source location 的余地，
// 改 backend 时不必修改调用现场。
//
// 初始化是 lazy 的——首次 Write() 会自动初始化 backend。只有 embedder
// 想在第一行日志输出前控制 sink 配置时，才需要显式调用 Log::Initialize()。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace Orange::Engine::Log
{

    enum class Level : int
    {
        Trace = 0,
        Debug,
        Info,
        Warn,
        Error,
        Critical,
        Off,
    };

    ORANGE_ENGINE_API void  Initialize();
    ORANGE_ENGINE_API void  Shutdown();
    ORANGE_ENGINE_API void  SetLevel(Level level) noexcept;
    ORANGE_ENGINE_API Level GetLevel() noexcept;
    ORANGE_ENGINE_API bool  IsEnabled(Level level) noexcept;

    // 底层 sink。一般请使用 ORANGE_LOG_* 宏 / Format()。
    ORANGE_ENGINE_API void Write(Level level, std::string_view message) noexcept;

    // 应用层 sink hook（OrangeEditor v0.8 Console 面板等消费者用）：把每条
    // 日志原文 + level 转给注册 callback，让消费方自己缓存 / 过滤 / 渲染。
    // 与 backend 输出**并行**（spdlog / stderr fallback 仍然照常写），不替
    // 代主输出路径。userData 透传，callback 内部可挂自定义 context。
    // callback 必须 thread-safe（日志可能从任意线程调用）。
    // 设置 nullptr / 调 ClearLogSink 卸载。
    using LogSinkFn = void (*)(Level level, std::string_view message, void* userData);
    ORANGE_ENGINE_API void SetLogSink(LogSinkFn fn, void* userData) noexcept;
    ORANGE_ENGINE_API void ClearLogSink() noexcept;

    template <typename... Args>
    inline void Format(Level level, std::format_string<Args...> fmt, Args&&... args)
    {
        if (!IsEnabled(level))
        {
            return;
        }
        std::string message = std::format(fmt, std::forward<Args>(args)...);
        Write(level, std::string_view{message});
    }

    inline void Format(Level level, std::string_view text) noexcept
    {
        if (!IsEnabled(level))
        {
            return;
        }
        Write(level, text);
    }

} // namespace Orange::Engine::Log

#define ORANGE_LOG_TRACE(...) ::Orange::Engine::Log::Format(::Orange::Engine::Log::Level::Trace, __VA_ARGS__)
#define ORANGE_LOG_DEBUG(...) ::Orange::Engine::Log::Format(::Orange::Engine::Log::Level::Debug, __VA_ARGS__)
#define ORANGE_LOG_INFO(...) ::Orange::Engine::Log::Format(::Orange::Engine::Log::Level::Info, __VA_ARGS__)
#define ORANGE_LOG_WARN(...) ::Orange::Engine::Log::Format(::Orange::Engine::Log::Level::Warn, __VA_ARGS__)
#define ORANGE_LOG_ERROR(...) ::Orange::Engine::Log::Format(::Orange::Engine::Log::Level::Error, __VA_ARGS__)
#define ORANGE_LOG_CRITICAL(...) ::Orange::Engine::Log::Format(::Orange::Engine::Log::Level::Critical, __VA_ARGS__)

#endif // ORANGE_ENGINE_CORE_LOG_H
