// Core::Log 实现
//
// Backend 的选择在编译期：
//   * 定义了 ORANGE_ENGINE_WITH_SPDLOG → 经由唯一一个
//     `spdlog::stdout_color_mt("orange_engine")` logger 输出。
//   * 否则                              → 极简的 stderr 兜底。让引擎在
//     未安装 spdlog 的环境（CI bootstrap、header-only smoke check）也
//     能用。
//
// 两条路径共享同一个 `sLevel` atomic，使 `SetLevel` 的语义在不同 build
// 下保持一致。stderr 兜底刻意写得很小——日志格式化性能不在目标范围内，
// 真要追性能就走 spdlog。

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
