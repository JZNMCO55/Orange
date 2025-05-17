#include "Logger.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
namespace Orange
{
    struct LogImpl
    {
        std::shared_ptr<spdlog::logger> Logger;
        LogImpl()
        {
            Logger = spdlog::stdout_color_mt("Orange");
            Logger->set_level(spdlog::level::trace);
        }

        ~LogImpl()
        {
        }

        void LogError(const std::string &message)
        {
            Logger->error(message);
        }

        void LogWarn(const std::string &message)
        {
            Logger->warn(message);
        }

        void LogInfo(const std::string &message)
        {
            Logger->info(message);
        }

        void LogDebug(const std::string &message)
        {
            Logger->debug(message);
        }

        void LogTrace(const std::string &message)
        {
            Logger->trace(message);
        }

        void LogCritical(const std::string &message)
        {
            Logger->critical(message);
        }
    };

    static std::unique_ptr<LogImpl> impl = nullptr;

    void Logger::Init()
    {
        if (impl)
        {
            return;
        }
        impl = std::make_unique<LogImpl>();
    }

    void Logger::LogError(const std::string &message)
    {
        impl->LogError(message);
    }

    void Logger::LogWarn(const std::string &message)
    {
        impl->LogWarn(message);
    }

    void Logger::LogInfo(const std::string &message)
    {
        impl->LogInfo(message);
    }

    void Logger::LogDebug(const std::string &message)
    {
        impl->LogDebug(message);
    }

    void Logger::LogTrace(const std::string &message)
    {
        impl->LogTrace(message);
    }

    void Logger::LogCritical(const std::string &message)
    {
        impl->LogCritical(message);
    }
}
