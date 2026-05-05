// ---------------------------------------------------------------------------
// Phase 1 / Task 04 — Core::Log implementation
//
// Backend selection is compile-time:
//   * ORANGE_ENGINE_WITH_SPDLOG defined  → route through a single
//     `spdlog::stdout_color_mt("orange_engine")` logger.
//   * otherwise                          → minimal stderr fallback. Keeps
//     the engine usable in environments where spdlog is not installed
//     (e.g. CI bootstraps, header-only smoke checks).
//
// Both paths share the same `sLevel` atomic so `SetLevel` semantics are
// consistent across builds. The stderr fallback is intentionally tiny —
// log formatting performance is not a goal until spdlog is wired in.
// ---------------------------------------------------------------------------

#include "orange/engine/core/Log.h"

#include <atomic>
#include <cstdio>
#include <mutex>

#if defined(ORANGE_ENGINE_WITH_SPDLOG)
    #include <memory>

    #include <spdlog/spdlog.h>
    #include <spdlog/sinks/stdout_color_sinks.h>
#endif

namespace Orange::Engine::Log
{
namespace
{

std::atomic<Level> sLevel{Level::Info};
std::mutex         sStderrMutex;

const char* LevelTag(Level level) noexcept
{
    switch (level)
    {
        case Level::Trace:    return "trace";
        case Level::Debug:    return "debug";
        case Level::Info:     return "info";
        case Level::Warn:     return "warn";
        case Level::Error:    return "error";
        case Level::Critical: return "critical";
        case Level::Off:      return "off";
    }
    return "info";
}

#if defined(ORANGE_ENGINE_WITH_SPDLOG)

std::shared_ptr<spdlog::logger> sLogger;

spdlog::level::level_enum ToSpdlog(Level level) noexcept
{
    switch (level)
    {
        case Level::Trace:    return spdlog::level::trace;
        case Level::Debug:    return spdlog::level::debug;
        case Level::Info:     return spdlog::level::info;
        case Level::Warn:     return spdlog::level::warn;
        case Level::Error:    return spdlog::level::err;
        case Level::Critical: return spdlog::level::critical;
        case Level::Off:      return spdlog::level::off;
    }
    return spdlog::level::info;
}

void EnsureLogger()
{
    if (sLogger)
    {
        return;
    }
    sLogger = spdlog::stdout_color_mt("orange_engine");
    sLogger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    sLogger->set_level(ToSpdlog(sLevel.load(std::memory_order_relaxed)));
}

#endif

void WriteFallback(Level level, std::string_view message) noexcept
{
    std::lock_guard<std::mutex> guard(sStderrMutex);
    std::fprintf(stderr,
                 "[%s] %.*s\n",
                 LevelTag(level),
                 static_cast<int>(message.size()),
                 message.data());
}

}  // namespace

void Initialize()
{
#if defined(ORANGE_ENGINE_WITH_SPDLOG)
    EnsureLogger();
#endif
}

void Shutdown()
{
#if defined(ORANGE_ENGINE_WITH_SPDLOG)
    if (sLogger)
    {
        spdlog::drop("orange_engine");
        sLogger.reset();
    }
#endif
}

void SetLevel(Level level) noexcept
{
    sLevel.store(level, std::memory_order_relaxed);
#if defined(ORANGE_ENGINE_WITH_SPDLOG)
    if (sLogger)
    {
        sLogger->set_level(ToSpdlog(level));
    }
#endif
}

Level GetLevel() noexcept
{
    return sLevel.load(std::memory_order_relaxed);
}

bool IsEnabled(Level level) noexcept
{
    return static_cast<int>(level) >= static_cast<int>(sLevel.load(std::memory_order_relaxed));
}

void Write(Level level, std::string_view message) noexcept
{
    if (!IsEnabled(level))
    {
        return;
    }
#if defined(ORANGE_ENGINE_WITH_SPDLOG)
    EnsureLogger();
    if (sLogger)
    {
        sLogger->log(ToSpdlog(level), std::string_view{message});
        return;
    }
#endif
    WriteFallback(level, message);
}

}  // namespace Orange::Engine::Log
