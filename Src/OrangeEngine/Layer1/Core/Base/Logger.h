#ifndef ORANGE_ENGINE_LOGGER_H_
#define ORANGE_ENGINE_LOGGER_H_

#include <memory>
#include <string>
#include <map>

// Forward declarations
namespace spdlog
{
    class logger;
}

namespace Orange
{
    /**
     * @brief 高性能日志系统，基于spdlog实现
     * 
     * Logger类提供了分层次的日志记录功能，支持：
     * - 多种日志级别（Trace, Debug, Info, Warn, Error, Critical）
     * - 标签化日志记录
     * - 核心与客户端分离的日志记录
     * - 条件编译优化
     * 
     * 使用示例：
     * @code
     * Orange::Logger::Init();
     * ORG_CORE_INFO("Engine initialized");
     * ORG_CORE_WARN_TAG("Renderer", "Shader compilation warning");
     * ORG_INFO("Application started");
     * @endcode
     */
    class Logger
    {
    public:
        /**
         * @brief 日志记录器类型枚举
         */
        enum class Type : uint8_t
        {
            Core = 0,   ///< 引擎核心日志
            Client = 1  ///< 客户端应用日志
        };

        /**
         * @brief 日志级别枚举
         */
        enum class Level : uint8_t
        {
            Trace = 0,  ///< 跟踪级别，最详细的调试信息
            Debug,      ///< 调试级别，调试过程中的详细信息
            Info,       ///< 信息级别，一般性操作信息
            Warn,       ///< 警告级别，潜在问题但不影响运行
            Error,      ///< 错误级别，错误情况但程序可继续
            Critical    ///< 严重级别，严重错误可能导致程序终止
        };

        /**
         * @brief 标签详细配置结构
         */
        struct TagDetails
        {
            bool mEnabled = true;           ///< 标签是否启用
            Level mLevelFilter = Level::Trace;  ///< 标签的最低日志级别过滤
        };

    public:
        /**
         * @brief 初始化日志系统
         * 
         * 必须在使用任何日志功能前调用此函数。
         * 初始化过程包括：
         * - 创建日志目录
         * - 配置控制台和文件输出
         * - 设置默认标签配置
         * 
         * @note 重复调用是安全的，不会重复初始化
         */
        static void Init();

        /**
         * @brief 关闭日志系统
         * 
         * 清理资源并刷新所有待写入的日志。
         * 通常在程序退出时调用。
         */
        static void Shutdown();

        /**
         * @brief 获取核心日志记录器
         * @return 核心日志记录器的共享指针
         */
        static std::shared_ptr<spdlog::logger>& GetCoreLogger();

        /**
         * @brief 获取客户端日志记录器
         * @return 客户端日志记录器的共享指针
         */
        static std::shared_ptr<spdlog::logger>& GetClientLogger();

        /**
         * @brief 检查标签是否存在
         * @param tag 要检查的标签名
         * @return 如果标签存在返回true，否则返回false
         */
        static bool HasTag(const std::string& tag);

        /**
         * @brief 获取所有启用的标签配置
         * @return 标签配置映射的引用
         */
        static std::map<std::string, TagDetails>& EnabledTags();

        /**
         * @brief 设置默认标签配置
         * 
         * 重置所有标签配置为默认值，包括：
         * - Core: 所有级别
         * - Memory: Error及以上
         * - Renderer: Info及以上
         * - Audio: Info及以上
         * 等等
         */
        static void SetDefaultTagSettings();

        // =================================================================
        // 日志记录接口
        // =================================================================

        /**
         * @brief 记录指定类型和级别的日志消息
         * @param type 日志记录器类型
         * @param level 日志级别
         * @param message 日志消息
         */
        static void PrintMessage(Type type, Level level, const std::string& message);

        /**
         * @brief 记录带标签的日志消息
         * @param type 日志记录器类型
         * @param level 日志级别
         * @param tag 日志标签
         * @param message 日志消息
         */
        static void PrintMessageTag(Type type, Level level, const std::string& tag, const std::string& message);

        /**
         * @brief 记录断言失败消息
         * @param type 日志记录器类型
         * @param prefix 断言前缀
         * @param message 错误消息
         */
        static void PrintAssertMessage(Type type, const std::string& prefix, const std::string& message = "");

        // =================================================================
        // 级别转换工具函数
        // =================================================================

        /**
         * @brief 将日志级别转换为字符串
         * @param level 日志级别
         * @return 对应的字符串表示
         */
        static const char* LevelToString(Level level);

        /**
         * @brief 从字符串转换为日志级别
         * @param string 级别字符串
         * @return 对应的日志级别，无效输入返回Trace
         */
        static Level LevelFromString(const std::string& string);

    private:
        static std::shared_ptr<spdlog::logger> s_CoreLogger;    ///< 核心日志记录器
        static std::shared_ptr<spdlog::logger> s_ClientLogger;  ///< 客户端日志记录器
        
        inline static std::map<std::string, TagDetails> s_EnabledTags;     ///< 当前启用的标签配置
        static std::map<std::string, TagDetails> s_DefaultTagDetails;      ///< 默认标签配置
    };
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 标签化日志宏定义（推荐使用）                                                                                      //
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @defgroup TaggedCoreLogs 核心标签化日志宏
 * @brief 引擎核心系统使用的标签化日志宏
 * @{
 */

/// @brief 核心跟踪级别标签日志
#define ORG_CORE_TRACE_TAG(tag, message) ::Orange::Logger::PrintMessageTag(::Orange::Logger::Type::Core, ::Orange::Logger::Level::Trace, tag, message)
/// @brief 核心调试级别标签日志
#define ORG_CORE_DEBUG_TAG(tag, message) ::Orange::Logger::PrintMessageTag(::Orange::Logger::Type::Core, ::Orange::Logger::Level::Debug, tag, message)
/// @brief 核心信息级别标签日志
#define ORG_CORE_INFO_TAG(tag, message)  ::Orange::Logger::PrintMessageTag(::Orange::Logger::Type::Core, ::Orange::Logger::Level::Info, tag, message)
/// @brief 核心警告级别标签日志
#define ORG_CORE_WARN_TAG(tag, message)  ::Orange::Logger::PrintMessageTag(::Orange::Logger::Type::Core, ::Orange::Logger::Level::Warn, tag, message)
/// @brief 核心错误级别标签日志
#define ORG_CORE_ERROR_TAG(tag, message) ::Orange::Logger::PrintMessageTag(::Orange::Logger::Type::Core, ::Orange::Logger::Level::Error, tag, message)
/// @brief 核心严重级别标签日志
#define ORG_CORE_CRITICAL_TAG(tag, message) ::Orange::Logger::PrintMessageTag(::Orange::Logger::Type::Core, ::Orange::Logger::Level::Critical, tag, message)

/** @} */

/**
 * @defgroup TaggedClientLogs 客户端标签化日志宏
 * @brief 客户端应用使用的标签化日志宏
 * @{
 */

/// @brief 客户端跟踪级别标签日志
#define ORG_TRACE_TAG(tag, message) ::Orange::Logger::PrintMessageTag(::Orange::Logger::Type::Client, ::Orange::Logger::Level::Trace, tag, message)
/// @brief 客户端调试级别标签日志
#define ORG_DEBUG_TAG(tag, message) ::Orange::Logger::PrintMessageTag(::Orange::Logger::Type::Client, ::Orange::Logger::Level::Debug, tag, message)
/// @brief 客户端信息级别标签日志
#define ORG_INFO_TAG(tag, message)  ::Orange::Logger::PrintMessageTag(::Orange::Logger::Type::Client, ::Orange::Logger::Level::Info, tag, message)
/// @brief 客户端警告级别标签日志
#define ORG_WARN_TAG(tag, message)  ::Orange::Logger::PrintMessageTag(::Orange::Logger::Type::Client, ::Orange::Logger::Level::Warn, tag, message)
/// @brief 客户端错误级别标签日志
#define ORG_ERROR_TAG(tag, message) ::Orange::Logger::PrintMessageTag(::Orange::Logger::Type::Client, ::Orange::Logger::Level::Error, tag, message)
/// @brief 客户端严重级别标签日志
#define ORG_CRITICAL_TAG(tag, message) ::Orange::Logger::PrintMessageTag(::Orange::Logger::Type::Client, ::Orange::Logger::Level::Critical, tag, message)

/** @} */

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 通用日志宏定义                                                                                                   //
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @defgroup CoreLogs 核心日志宏
 * @brief 引擎核心系统使用的通用日志宏
 * @{
 */

/// @brief 核心跟踪级别日志
#define ORG_CORE_TRACE(message)  ::Orange::Logger::PrintMessage(::Orange::Logger::Type::Core, ::Orange::Logger::Level::Trace, message)
/// @brief 核心调试级别日志
#define ORG_CORE_DEBUG(message)  ::Orange::Logger::PrintMessage(::Orange::Logger::Type::Core, ::Orange::Logger::Level::Debug, message)
/// @brief 核心信息级别日志
#define ORG_CORE_INFO(message)   ::Orange::Logger::PrintMessage(::Orange::Logger::Type::Core, ::Orange::Logger::Level::Info, message)
/// @brief 核心警告级别日志
#define ORG_CORE_WARN(message)   ::Orange::Logger::PrintMessage(::Orange::Logger::Type::Core, ::Orange::Logger::Level::Warn, message)
/// @brief 核心错误级别日志
#define ORG_CORE_ERROR(message)  ::Orange::Logger::PrintMessage(::Orange::Logger::Type::Core, ::Orange::Logger::Level::Error, message)
/// @brief 核心严重级别日志
#define ORG_CORE_CRITICAL(message)  ::Orange::Logger::PrintMessage(::Orange::Logger::Type::Core, ::Orange::Logger::Level::Critical, message)

/** @} */

/**
 * @defgroup ClientLogs 客户端日志宏
 * @brief 客户端应用使用的通用日志宏
 * @{
 */

/// @brief 客户端跟踪级别日志
#define ORG_TRACE(message)   ::Orange::Logger::PrintMessage(::Orange::Logger::Type::Client, ::Orange::Logger::Level::Trace, message)
/// @brief 客户端调试级别日志
#define ORG_DEBUG(message)   ::Orange::Logger::PrintMessage(::Orange::Logger::Type::Client, ::Orange::Logger::Level::Debug, message)
/// @brief 客户端信息级别日志
#define ORG_INFO(message)    ::Orange::Logger::PrintMessage(::Orange::Logger::Type::Client, ::Orange::Logger::Level::Info, message)
/// @brief 客户端警告级别日志
#define ORG_WARN(message)    ::Orange::Logger::PrintMessage(::Orange::Logger::Type::Client, ::Orange::Logger::Level::Warn, message)
/// @brief 客户端错误级别日志
#define ORG_ERROR(message)   ::Orange::Logger::PrintMessage(::Orange::Logger::Type::Client, ::Orange::Logger::Level::Error, message)
/// @brief 客户端严重级别日志
#define ORG_CRITICAL(message)   ::Orange::Logger::PrintMessage(::Orange::Logger::Type::Client, ::Orange::Logger::Level::Critical, message)

/** @} */

// 条件编译优化：根据构建配置启用/禁用不同级别的日志
#if defined(ORG_DEBUG)
    // Debug模式：启用所有日志级别
    // 所有宏保持原定义

#elif defined(ORG_RELEASE)
    // Release模式：禁用Trace和Debug级别
    #undef ORG_CORE_TRACE
    #undef ORG_CORE_DEBUG
    #undef ORG_TRACE
    #undef ORG_DEBUG
    #undef ORG_CORE_TRACE_TAG
    #undef ORG_CORE_DEBUG_TAG
    #undef ORG_TRACE_TAG
    #undef ORG_DEBUG_TAG
    
    #define ORG_CORE_TRACE(message)
    #define ORG_CORE_DEBUG(message)
    #define ORG_TRACE(message)
    #define ORG_DEBUG(message)
    #define ORG_CORE_TRACE_TAG(tag, message)
    #define ORG_CORE_DEBUG_TAG(tag, message)
    #define ORG_TRACE_TAG(tag, message)
    #define ORG_DEBUG_TAG(tag, message)

#elif defined(ORG_DIST)
    // Distribution模式：只保留Critical级别
    #undef ORG_CORE_TRACE
    #undef ORG_CORE_DEBUG
    #undef ORG_CORE_INFO
    #undef ORG_CORE_WARN
    #undef ORG_CORE_ERROR
    #undef ORG_TRACE
    #undef ORG_DEBUG
    #undef ORG_INFO
    #undef ORG_WARN
    #undef ORG_ERROR
    #undef ORG_CORE_TRACE_TAG
    #undef ORG_CORE_DEBUG_TAG
    #undef ORG_CORE_INFO_TAG
    #undef ORG_CORE_WARN_TAG
    #undef ORG_CORE_ERROR_TAG
    #undef ORG_TRACE_TAG
    #undef ORG_DEBUG_TAG
    #undef ORG_INFO_TAG
    #undef ORG_WARN_TAG
    #undef ORG_ERROR_TAG
    
    #define ORG_CORE_TRACE(message)
    #define ORG_CORE_DEBUG(message)
    #define ORG_CORE_INFO(message)
    #define ORG_CORE_WARN(message)
    #define ORG_CORE_ERROR(message)
    #define ORG_TRACE(message)
    #define ORG_DEBUG(message)
    #define ORG_INFO(message)
    #define ORG_WARN(message)
    #define ORG_ERROR(message)
    #define ORG_CORE_TRACE_TAG(tag, message)
    #define ORG_CORE_DEBUG_TAG(tag, message)
    #define ORG_CORE_INFO_TAG(tag, message)
    #define ORG_CORE_WARN_TAG(tag, message)
    #define ORG_CORE_ERROR_TAG(tag, message)
    #define ORG_TRACE_TAG(tag, message)
    #define ORG_DEBUG_TAG(tag, message)
    #define ORG_INFO_TAG(tag, message)
    #define ORG_WARN_TAG(tag, message)
    #define ORG_ERROR_TAG(tag, message)
#endif

#endif // ORANGE_ENGINE_LOGGER_H_