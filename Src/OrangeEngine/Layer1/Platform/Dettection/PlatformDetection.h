#ifndef PLATFORM_DETECTION_H
#define PLATFORM_DETECTION_H

#include <string>

namespace Orange
{
    namespace Platform
    {
        // 操作系统类型枚举
        enum class OperatingSystem
        {
            Unknown,
            Windows,
            Linux,
            MacOS
        };

        // 硬件架构类型枚举
        enum class Architecture
        {
            Unknown,
            x86,
            x64,
            ARM,
            ARM64
        };

        // 平台检测类
        class PlatformDetection
        {
        public:
            // 获取当前操作系统类型
            static OperatingSystem GetOperatingSystem();

            // 获取当前硬件架构
            static Architecture GetArchitecture();

            // 获取操作系统版本信息
            static std::string GetOSVersion();

            // 获取CPU信息
            static std::string GetCPUInfo();

            // 获取内存信息
            static size_t GetTotalSystemMemory();

            // 检查是否支持特定功能
            static bool IsFeatureSupported(const std::string &featureName);

            // 获取平台特定的路径分隔符
            static char GetPathSeparator();

            // 获取平台特定的换行符
            static const char *GetLineEnding();

            // 获取平台特定的临时目录
            static std::string GetTempDirectory();

            // 获取平台特定的用户目录
            static std::string GetUserDirectory();

            // 获取平台特定的应用程序数据目录
            static std::string GetAppDataDirectory();

            // 获取平台特定的可执行文件扩展名
            static std::string GetExecutableExtension();

            // 获取平台特定的动态库扩展名
            static std::string GetDynamicLibraryExtension();

            // 获取平台特定的静态库扩展名
            static std::string GetStaticLibraryExtension();
        };

    } // namespace Platform
} // namespace Orange
#endif // PLATFORM_DETECTION_H