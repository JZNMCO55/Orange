#ifndef ORANGE_ENGINE_ASSERT_H_
#define ORANGE_ENGINE_ASSERT_H_

#include "Base.h"
#include "Logger.h"

/**
 * @file Assert.h
 * @brief Orange引擎断言系统
 *
 * 提供基础的断言和验证机制：
 * - 调试断点支持
 * - 条件编译优化
 * - 核心与客户端分离的断言
 *
 * 使用示例：
 * @code
 * ORG_CORE_ASSERT(ptr != nullptr, "指针不能为空");
 * ORG_ASSERT(index < size, "索引越界");
 * ORG_CORE_VERIFY(result == SUCCESS, "操作失败");
 * @endcode
 */

// =================================================================
// 调试断点宏定义
// =================================================================

/**
 * @brief 平台相关的调试断点实现
 * 平台宏由CMake定义：ORG_PLATFORM_WINDOWS, ORG_COMPILER_CLANG等
 */
#ifdef ORG_PLATFORM_WINDOWS
#define ORG_DEBUG_BREAK __debugbreak()
#elif defined(ORG_COMPILER_CLANG)
#define ORG_DEBUG_BREAK __builtin_debugtrap()
#else
#define ORG_DEBUG_BREAK
#endif

// =================================================================
// 断言启用控制
// =================================================================

/**
 * @brief 断言启用控制
 * 构建模式宏由CMake定义：ORG_DEBUG, ORG_RELEASE, ORG_DIST
 */
#ifdef ORG_DEBUG
#define ORG_ENABLE_ASSERTS
#endif

/**
 * @brief 始终启用验证（即使在发布版本中）
 */
#define ORG_ENABLE_VERIFY

// =================================================================
// 断言实现宏
// =================================================================

#ifdef ORG_ENABLE_ASSERTS
/**
 * @brief 内部断言消息宏
 */
#define ORG_CORE_ASSERT_MESSAGE_INTERNAL(...) ::Orange::Logger::PrintAssertMessage(::Orange::Logger::Type::Core, "Assertion Failed", ##__VA_ARGS__)
#define ORG_ASSERT_MESSAGE_INTERNAL(...) ::Orange::Logger::PrintAssertMessage(::Orange::Logger::Type::Client, "Assertion Failed", ##__VA_ARGS__)

/**
 * @brief 核心断言宏
 * @param condition 断言条件，为false时触发断言
 * @param ... 可选的断言消息
 */
#define ORG_CORE_ASSERT(condition, ...)                    \
    {                                                      \
        if (!(condition))                                  \
        {                                                  \
            ORG_CORE_ASSERT_MESSAGE_INTERNAL(__VA_ARGS__); \
            ORG_DEBUG_BREAK;                               \
        }                                                  \
    }

/**
 * @brief 客户端断言宏
 * @param condition 断言条件，为false时触发断言
 * @param ... 可选的断言消息
 */
#define ORG_ASSERT(condition, ...)                    \
    {                                                 \
        if (!(condition))                             \
        {                                             \
            ORG_ASSERT_MESSAGE_INTERNAL(__VA_ARGS__); \
            ORG_DEBUG_BREAK;                          \
        }                                             \
    }

#else
/**
 * @brief 断言禁用时的空宏定义
 */
#define ORG_CORE_ASSERT(condition, ...)
#define ORG_ASSERT(condition, ...)
#endif

// =================================================================
// 验证实现宏
// =================================================================

#ifdef ORG_ENABLE_VERIFY
/**
 * @brief 内部验证消息宏
 */
#define ORG_CORE_VERIFY_MESSAGE_INTERNAL(...) ::Orange::Logger::PrintAssertMessage(::Orange::Logger::Type::Core, "Verify Failed", ##__VA_ARGS__)
#define ORG_VERIFY_MESSAGE_INTERNAL(...) ::Orange::Logger::PrintAssertMessage(::Orange::Logger::Type::Client, "Verify Failed", ##__VA_ARGS__)

/**
 * @brief 核心验证宏
 * @param condition 验证条件，为false时触发验证失败
 * @param ... 可选的验证失败消息
 *
 * 验证与断言的区别：验证在发布版本中仍然有效
 */
#define ORG_CORE_VERIFY(condition, ...)                    \
    {                                                      \
        if (!(condition))                                  \
        {                                                  \
            ORG_CORE_VERIFY_MESSAGE_INTERNAL(__VA_ARGS__); \
            ORG_DEBUG_BREAK;                               \
        }                                                  \
    }

/**
 * @brief 客户端验证宏
 * @param condition 验证条件，为false时触发验证失败
 * @param ... 可选的验证失败消息
 */
#define ORG_VERIFY(condition, ...)                    \
    {                                                 \
        if (!(condition))                             \
        {                                             \
            ORG_VERIFY_MESSAGE_INTERNAL(__VA_ARGS__); \
            ORG_DEBUG_BREAK;                          \
        }                                             \
    }

#else
/**
 * @brief 验证禁用时的空宏定义
 */
#define ORG_CORE_VERIFY(condition, ...)
#define ORG_VERIFY(condition, ...)
#endif

#endif // ORANGE_ENGINE_ASSERT_H_