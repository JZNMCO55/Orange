#ifndef ORANGE_ENGINE_CORE_LOG_H
#define ORANGE_ENGINE_CORE_LOG_H

// ---------------------------------------------------------------------------
// Phase 1 / Task 04 — Core::Log
//
// Logging facade. Public header is third-party-free by design:
// * spdlog (the default backend when ORANGE_ENGINE_WITH_SPDLOG=ON) is wrapped
//   only in src/core/Log.cpp. Public consumers see no spdlog symbols.
// * Format strings flow through std::format (C++20) — same syntax as
//   fmtlib, no extra dependency on the public header.
//
// Use the ORANGE_LOG_* macros rather than calling Log::Format directly:
// the macros are early-out friendly and let the impl side hook in
// per-call-site source location later (Phase 6 line-info patch) without
// touching call sites.
//
// Initialization is lazy — the first Write() auto-initializes the backend.
// Calling Log::Initialize() explicitly only matters when an embedder wants
// to control sink configuration before any log line is emitted.
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

// Low-level sink. Prefer the ORANGE_LOG_* macros / Format().
ORANGE_ENGINE_API void Write(Level level, std::string_view message) noexcept;

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

}  // namespace Orange::Engine::Log

#define ORANGE_LOG_TRACE(...)    ::Orange::Engine::Log::Format(::Orange::Engine::Log::Level::Trace,    __VA_ARGS__)
#define ORANGE_LOG_DEBUG(...)    ::Orange::Engine::Log::Format(::Orange::Engine::Log::Level::Debug,    __VA_ARGS__)
#define ORANGE_LOG_INFO(...)     ::Orange::Engine::Log::Format(::Orange::Engine::Log::Level::Info,     __VA_ARGS__)
#define ORANGE_LOG_WARN(...)     ::Orange::Engine::Log::Format(::Orange::Engine::Log::Level::Warn,     __VA_ARGS__)
#define ORANGE_LOG_ERROR(...)    ::Orange::Engine::Log::Format(::Orange::Engine::Log::Level::Error,    __VA_ARGS__)
#define ORANGE_LOG_CRITICAL(...) ::Orange::Engine::Log::Format(::Orange::Engine::Log::Level::Critical, __VA_ARGS__)

#endif  // ORANGE_ENGINE_CORE_LOG_H
