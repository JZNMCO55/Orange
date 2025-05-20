#ifndef ORANGE_ENGINE_LOGGER_H
#define ORANGE_ENGINE_LOGGER_H

#include <memory>
#include <string>
#include <format>

namespace Orange
{
    class Logger
    {
    public:
        // 需要在程序启动时添加到初始化列表
        static void Init();

        // 编译时格式检查接口
        template <typename... Args>
        static void LogError(std::format_string<Args...> fmt, Args &&...args)
        {
            InternalLogError(std::format(fmt, std::forward<Args>(args)...));
        }

        template <typename... Args>
        static void LogWarn(std::format_string<Args...> fmt, Args &&...args)
        {
            InternalLogWarn(std::format(fmt, std::forward<Args>(args)...));
        }

        template <typename... Args>
        static void LogInfo(std::format_string<Args...> fmt, Args &&...args)
        {
            InternalLogInfo(std::format(fmt, std::forward<Args>(args)...));
        }

        template <typename... Args>
        static void LogDebug(std::format_string<Args...> fmt, Args &&...args)
        {
            InternalLogDebug(std::format(fmt, std::forward<Args>(args)...));
        }

        template <typename... Args>
        static void LogTrace(std::format_string<Args...> fmt, Args &&...args)
        {
            InternalLogTrace(std::format(fmt, std::forward<Args>(args)...));
        }

        template <typename... Args>
        static void LogCritical(std::format_string<Args...> fmt, Args &&...args)
        {
            InternalLogCritical(std::format(fmt, std::forward<Args>(args)...));
        }

    public:
        static void InternalLogError(const std::string &message);
        static void InternalLogWarn(const std::string &message);
        static void InternalLogInfo(const std::string &message);
        static void InternalLogDebug(const std::string &message);
        static void InternalLogTrace(const std::string &message);
        static void InternalLogCritical(const std::string &message);
    };
}

#ifdef ORANGE_DEBUG
#define ORG_LOG_ERROR(...) Orange::Logger::LogError(__VA_ARGS__)
#define ORG_LOG_WARN(...) Orange::Logger::LogWarn(__VA_ARGS__)
#define ORG_LOG_INFO(...) Orange::Logger::LogInfo(__VA_ARGS__)
#define ORG_LOG_DEBUG(...) Orange::Logger::LogDebug(__VA_ARGS__)
#define ORG_LOG_TRACE(...) Orange::Logger::LogTrace(__VA_ARGS__)
#define ORG_LOG_CRITICAL(...) Orange::Logger::LogCritical(__VA_ARGS__)

#elif ORANGE_RELEASE
#define ORG_LOG_INFO(...)
#define ORG_LOG_WARN(...)
#define ORG_LOG_DEBUG(...)
#define ORG_LOG_TRACE(...)
#define ORG_LOG_ERROR(...) Orange::Logger::LogError(__VA_ARGS__)
#define ORG_LOG_CRITICAL(...) Orange::Logger::LogCritical(__VA_ARGS__)
#elif ORANGE_DIST
#define ORG_LOG_ERROR(...)
#define ORG_LOG_WARN(...)
#define ORG_LOG_INFO(...)
#define ORG_LOG_DEBUG(...)
#define ORG_LOG_TRACE(...)
#define ORG_LOG_CRITICAL(...)
#endif

#endif