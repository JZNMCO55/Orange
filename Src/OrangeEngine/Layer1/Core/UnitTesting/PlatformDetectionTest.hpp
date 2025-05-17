#include "Layer1/Platform/Dettection/PlatformDetection.h"
#include <iostream>

static bool PrintPlatformInfo()
{
    using namespace Orange::Platform;

    std::cout << "Platform Detection Test\n";
    std::cout << "======================\n\n";

    // 操作系统信息
    std::cout << "Operating System: ";
    switch (PlatformDetection::GetOperatingSystem())
    {
    case OperatingSystem::Windows:
        std::cout << "Windows";
        break;
    case OperatingSystem::Linux:
        std::cout << "Linux";
        break;
    case OperatingSystem::MacOS:
        std::cout << "MacOS";
        break;
    default:
        std::cout << "Unknown";
    }
    std::cout << "\n";

    // 操作系统版本
    std::cout << "OS Version: " << PlatformDetection::GetOSVersion() << "\n";

    // 硬件架构
    std::cout << "Architecture: ";
    switch (PlatformDetection::GetArchitecture())
    {
    case Architecture::x86:
        std::cout << "x86";
        break;
    case Architecture::x64:
        std::cout << "x64";
        break;
    case Architecture::ARM:
        std::cout << "ARM";
        break;
    case Architecture::ARM64:
        std::cout << "ARM64";
        break;
    default:
        std::cout << "Unknown";
    }
    std::cout << "\n";

    // CPU信息
    std::cout << "CPU Info: " << PlatformDetection::GetCPUInfo() << "\n";

    // 内存信息
    size_t totalMemory = PlatformDetection::GetTotalSystemMemory();
    std::cout << "Total System Memory: " << (totalMemory / (1024 * 1024 * 1024)) << " GB\n";

    // 路径分隔符
    std::cout << "Path Separator: " << PlatformDetection::GetPathSeparator() << "\n";

    // 换行符
    std::cout << "Line Ending: " << (PlatformDetection::GetLineEnding() == "\r\n" ? "CRLF" : "LF") << "\n";

    // 临时目录
    std::cout << "Temp Directory: " << PlatformDetection::GetTempDirectory() << "\n";

    // 用户目录
    std::cout << "User Directory: " << PlatformDetection::GetUserDirectory() << "\n";

    // 应用程序数据目录
    std::cout << "App Data Directory: " << PlatformDetection::GetAppDataDirectory() << "\n";

    // 文件扩展名
    std::cout << "Executable Extension: " << PlatformDetection::GetExecutableExtension() << "\n";
    std::cout << "Dynamic Library Extension: " << PlatformDetection::GetDynamicLibraryExtension() << "\n";
    std::cout << "Static Library Extension: " << PlatformDetection::GetStaticLibraryExtension() << "\n";
    return true;
}