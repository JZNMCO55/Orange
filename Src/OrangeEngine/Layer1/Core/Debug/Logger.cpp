#include "Logger.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include "Platform/Detection/PlatformDetection.h"
namespace Orange
{
    struct LogImpl
    {
        std::shared_ptr<spdlog::logger> Logger;
        LogImpl()
        {
            auto fileName = Platform::PlatformDetection::GetExecutableDirectory() + "/logs/Orange.log";
            // 创建 sink
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(fileName, true);

            // 可选：设置 sink 的日志级别
            console_sink->set_level(spdlog::level::trace);
            file_sink->set_level(spdlog::level::trace);

            // 设置统一的格式
            console_sink->set_pattern("[%T] [%^%l%$] %v");
            file_sink->set_pattern("[%Y-%m-%d %T] [%l] %v");

            std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};

            Logger = std::make_shared<spdlog::logger>("Orange", sinks.begin(), sinks.end());
            Logger->set_level(spdlog::level::trace);
            // Logger->flush_on(spdlog::level::warn); // 遇到 warn 或更严重时立即 flush
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
