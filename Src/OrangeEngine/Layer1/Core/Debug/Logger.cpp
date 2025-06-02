#include "Logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace Orange
{
    struct LogImpl
    {
        LogImpl()
        {
            // 简化的初始化
        }

        ~LogImpl()
        {
        }

        std::string GetTimestamp()
        {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::stringstream ss;
            ss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
            return ss.str();
        }

        void LogError(const std::string &message)
        {
            std::cerr << "[" << GetTimestamp() << "] [ERROR] " << message << std::endl;
        }

        void LogWarn(const std::string &message)
        {
            std::cout << "[" << GetTimestamp() << "] [WARN] " << message << std::endl;
        }

        void LogInfo(const std::string &message)
        {
            std::cout << "[" << GetTimestamp() << "] [INFO] " << message << std::endl;
        }

        void LogDebug(const std::string &message)
        {
            std::cout << "[" << GetTimestamp() << "] [DEBUG] " << message << std::endl;
        }

        void LogTrace(const std::string &message)
        {
            std::cout << "[" << GetTimestamp() << "] [TRACE] " << message << std::endl;
        }

        void LogCritical(const std::string &message)
        {
            std::cerr << "[" << GetTimestamp() << "] [CRITICAL] " << message << std::endl;
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

    void Logger::InternalLogError(const std::string &message)
    {
        if (!impl)
            Init();
        impl->LogError(message);
    }

    void Logger::InternalLogWarn(const std::string &message)
    {
        if (!impl)
            Init();
        impl->LogWarn(message);
    }

    void Logger::InternalLogInfo(const std::string &message)
    {
        if (!impl)
            Init();
        impl->LogInfo(message);
    }

    void Logger::InternalLogDebug(const std::string &message)
    {
        if (!impl)
            Init();
        impl->LogDebug(message);
    }

    void Logger::InternalLogTrace(const std::string &message)
    {
        if (!impl)
            Init();
        impl->LogTrace(message);
    }

    void Logger::InternalLogCritical(const std::string &message)
    {
        if (!impl)
            Init();
        impl->LogCritical(message);
    }
}
