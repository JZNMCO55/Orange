#include "Logger.h"
#include <iostream>
#include <fstream>
#include <memory>
#include <map>
#include <string>

namespace Orange
{
    // Static member definitions
    std::shared_ptr<spdlog::logger> Logger::s_CoreLogger;
    std::shared_ptr<spdlog::logger> Logger::s_ClientLogger;

    /**
     * @brief 默认标签配置映射
     * 
     * 定义了引擎各个系统的默认日志级别配置。
     * 这些配置可以通过EnabledTags()获取并修改。
     */
    std::map<std::string, Logger::TagDetails> Logger::s_DefaultTagDetails = {
        { "",                  TagDetails{ true, Level::Trace } },   // 默认标签
        { "Core",              TagDetails{ true, Level::Trace } },   // 核心系统
        { "Memory",            TagDetails{ true, Level::Error } },   // 内存管理
        { "Renderer",          TagDetails{ true, Level::Info  } },   // 渲染系统
        { "Audio",             TagDetails{ true, Level::Info  } },   // 音频系统
        { "Physics",           TagDetails{ true, Level::Warn  } },   // 物理系统
        { "Scripting",         TagDetails{ true, Level::Warn  } },   // 脚本系统
        { "Asset",             TagDetails{ true, Level::Warn  } },   // 资产管理
        { "Scene",             TagDetails{ true, Level::Info  } },   // 场景管理
        { "Animation",         TagDetails{ true, Level::Warn  } },   // 动画系统
        { "Input",             TagDetails{ true, Level::Info  } },   // 输入系统
        { "Network",           TagDetails{ true, Level::Warn  } },   // 网络系统
        { "Platform",          TagDetails{ true, Level::Warn  } },   // 平台相关
        { "Timer",             TagDetails{ false, Level::Trace } },  // 计时器（默认禁用）
        { "ThirdParty",        TagDetails{ true, Level::Error } },   // 第三方库
    };

    /**
     * @brief 简单的日志实现类
     * 
     * 提供基本的控制台和文件输出功能
     */
    struct SimpleLoggerImpl
    {
        std::ofstream mCoreLogFile;
        std::ofstream mClientLogFile;
        bool mInitialized = false;
        
        SimpleLoggerImpl()
        {
            try
            {
                mCoreLogFile.open("logs/ORANGE_CORE.log", std::ios::app);
                mClientLogFile.open("logs/ORANGE_CLIENT.log", std::ios::app);
                mInitialized = mCoreLogFile.is_open() && mClientLogFile.is_open();
                
                if (mInitialized)
                {
                    Log(Logger::Type::Core, Logger::Level::Info, "Logger system initialized successfully");
                }
            }
            catch (const std::exception& e)
            {
                std::cerr << "Failed to initialize logger: " << e.what() << std::endl;
                mInitialized = false;
            }
        }

        ~SimpleLoggerImpl()
        {
            if (mInitialized)
            {
                Log(Logger::Type::Core, Logger::Level::Info, "Logger system shutting down");
            }
            
            if (mCoreLogFile.is_open())
                mCoreLogFile.close();
            if (mClientLogFile.is_open())
                mClientLogFile.close();
        }

        void Log(Logger::Type type, Logger::Level level, const std::string& message)
        {
            if (!mInitialized)
            {
                std::cout << "[FALLBACK] " << message << std::endl;
                return;
            }

            std::string levelStr = LevelToString(level);
            std::string typeStr = (type == Logger::Type::Core) ? "CORE" : "CLIENT";
            
            // 输出到控制台
            std::cout << "[" << typeStr << "] " << levelStr << ": " << message << std::endl;
            
            // 输出到文件
            std::ofstream& logFile = (type == Logger::Type::Core) ? mCoreLogFile : mClientLogFile;
            if (logFile.is_open())
            {
                logFile << "[" << typeStr << "] " << levelStr << ": " << message << std::endl;
                logFile.flush();
            }
        }

        static const char* LevelToString(Logger::Level level)
        {
            switch (level)
            {
                case Logger::Level::Trace:    return "TRACE";
                case Logger::Level::Debug:    return "DEBUG";
                case Logger::Level::Info:     return "INFO";
                case Logger::Level::Warn:     return "WARN";
                case Logger::Level::Error:    return "ERROR";
                case Logger::Level::Critical: return "CRITICAL";
            }
            return "UNKNOWN";
        }
    };

    // 全局日志实现实例
    static std::unique_ptr<SimpleLoggerImpl> s_LoggerImpl = nullptr;

    void Logger::Init()
    {
        if (s_LoggerImpl)
        {
            return; // 防止重复初始化
        }

        s_LoggerImpl = std::make_unique<SimpleLoggerImpl>();
        SetDefaultTagSettings();
    }

    void Logger::Shutdown()
    {
        s_LoggerImpl.reset();
    }

    std::shared_ptr<spdlog::logger>& Logger::GetCoreLogger()
    {
        // 返回空指针，因为我们使用简单实现
        static std::shared_ptr<spdlog::logger> dummy;
        return dummy;
    }

    std::shared_ptr<spdlog::logger>& Logger::GetClientLogger()
    {
        // 返回空指针，因为我们使用简单实现
        static std::shared_ptr<spdlog::logger> dummy;
        return dummy;
    }

    bool Logger::HasTag(const std::string& tag)
    {
        return s_EnabledTags.find(tag) != s_EnabledTags.end();
    }

    std::map<std::string, Logger::TagDetails>& Logger::EnabledTags()
    {
        return s_EnabledTags;
    }

    void Logger::SetDefaultTagSettings()
    {
        s_EnabledTags = s_DefaultTagDetails;
    }

    void Logger::PrintMessage(Type type, Level level, const std::string& message)
    {
        if (!s_LoggerImpl)
        {
            std::cout << "[UNINITIALIZED] " << message << std::endl;
            return;
        }

        auto detail = s_EnabledTags[""];
        if (detail.mEnabled && detail.mLevelFilter <= level)
        {
            s_LoggerImpl->Log(type, level, message);
        }
    }

    void Logger::PrintMessageTag(Type type, Level level, const std::string& tag, const std::string& message)
    {
        if (!s_LoggerImpl)
        {
            std::cout << "[UNINITIALIZED][" << tag << "] " << message << std::endl;
            return;
        }

        auto it = s_EnabledTags.find(tag);
        TagDetails detail;
        
        if (it != s_EnabledTags.end())
        {
            detail = it->second;
        }
        else
        {
            // 如果标签不存在，使用默认设置
            detail = s_EnabledTags[""];
        }

        if (detail.mEnabled && detail.mLevelFilter <= level)
        {
            std::string taggedMessage = "[" + tag + "] " + message;
            s_LoggerImpl->Log(type, level, taggedMessage);
        }
    }

    void Logger::PrintAssertMessage(Type type, const std::string& prefix, const std::string& message)
    {
        std::string fullMessage;
        if (!message.empty())
        {
            fullMessage = prefix + ": " + message;
        }
        else
        {
            fullMessage = prefix;
        }
        
        if (s_LoggerImpl)
        {
            s_LoggerImpl->Log(type, Logger::Level::Error, fullMessage);
        }
        else
        {
            std::cerr << "[ASSERT] " << fullMessage << std::endl;
        }

        // Windows平台消息框支持
#if defined(ORG_PLATFORM_WINDOWS) && !defined(ORG_DIST)
        #include <Windows.h>
        std::string displayMessage = message.empty() ? "No message :(" : fullMessage;
        MessageBoxA(nullptr, displayMessage.c_str(), "Orange Assert", MB_OK | MB_ICONERROR);
#endif
    }

    const char* Logger::LevelToString(Level level)
    {
        return SimpleLoggerImpl::LevelToString(level);
    }

    Logger::Level Logger::LevelFromString(const std::string& string)
    {
        if (string == "Trace" || string == "TRACE")    return Level::Trace;
        if (string == "Debug" || string == "DEBUG")    return Level::Debug;
        if (string == "Info" || string == "INFO")      return Level::Info;
        if (string == "Warn" || string == "WARN")      return Level::Warn;
        if (string == "Error" || string == "ERROR")    return Level::Error;
        if (string == "Critical" || string == "CRITICAL") return Level::Critical;

        return Level::Trace; // 默认返回Trace级别
    }
}

