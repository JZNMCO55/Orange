#ifndef ORANGE_ENGINE_LOGGER_H
#define ORANGE_ENGINE_LOGGER_H

#include <memory>
#include <string>
#include <fmt/format.h>

namespace Orange
{
    class Logger
    {
    public:
        // 需要在程序启动时添加到初始化列表
        static void Init();

        static void LogError(const std::string &message);
        static void LogWarn(const std::string &message);
        static void LogInfo(const std::string &message);
        static void LogDebug(const std::string &message);
        static void LogTrace(const std::string &message);
        static void LogCritical(const std::string &message);

        // 格式化日志函数
        template <typename... Args>
        static void LogErrorFmt(const std::string &fmt, Args &&...args)
        {
            LogError(fmt::format(fmt, std::forward<Args>(args)...));
        }

        template <typename... Args>
        static void LogWarnFmt(const std::string &fmt, Args &&...args)
        {
            LogWarn(fmt::format(fmt, std::forward<Args>(args)...));
        }

        template <typename... Args>
        static void LogInfoFmt(const std::string &fmt, Args &&...args)
        {
            LogInfo(fmt::format(fmt, std::forward<Args>(args)...));
        }

        template <typename... Args>
        static void LogDebugFmt(const std::string &fmt, Args &&...args)
        {
            LogDebug(fmt::format(fmt, std::forward<Args>(args)...));
        }

        template <typename... Args>
        static void LogTraceFmt(const std::string &fmt, Args &&...args)
        {
            LogTrace(fmt::format(fmt, std::forward<Args>(args)...));
        }

        template <typename... Args>
        static void LogCriticalFmt(const std::string &fmt, Args &&...args)
        {
            LogCritical(fmt::format(fmt, std::forward<Args>(args)...));
        }
    };
}

#ifdef ORANGE_DEBUG
#define ORG_LOG_ERROR(...) Orange::Logger::LogErrorFmt(__VA_ARGS__)
#define ORG_LOG_WARN(...) Orange::Logger::LogWarnFmt(__VA_ARGS__)
#define ORG_LOG_INFO(...) Orange::Logger::LogInfoFmt(__VA_ARGS__)
#define ORG_LOG_DEBUG(...) Orange::Logger::LogDebugFmt(__VA_ARGS__)
#define ORG_LOG_TRACE(...) Orange::Logger::LogTraceFmt(__VA_ARGS__)
#define ORG_LOG_CRITICAL(...) Orange::Logger::LogCriticalFmt(__VA_ARGS__)

#elif ORANGE_RELEASE
#define ORG_LOG_INFO(...)
#define ORG_LOG_WARN(...)
#define ORG_LOG_DEBUG(...)
#define ORG_LOG_TRACE(...)
#define ORG_LOG_ERROR(...) Orange::Logger::LogErrorFmt(__VA_ARGS__)
#define ORG_LOG_CRITICAL(...) Orange::Logger::LogCriticalFmt(__VA_ARGS__)
#elif ORANGE_DIST
#define ORG_LOG_ERROR(...)
#define ORG_LOG_WARN(...)
#define ORG_LOG_INFO(...)
#define ORG_LOG_DEBUG(...)
#define ORG_LOG_TRACE(...)
#define ORG_LOG_CRITICAL(...)
#endif

#endif