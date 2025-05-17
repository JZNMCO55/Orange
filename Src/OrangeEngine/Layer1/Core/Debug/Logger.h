#ifndef ORANGE_ENGINE_LOGGER_H
#define ORANGE_ENGINE_LOGGER_H

#include <memory>
#include <string>

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