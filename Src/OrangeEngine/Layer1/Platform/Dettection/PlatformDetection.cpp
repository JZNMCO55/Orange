#include "PlatformDetection.h"

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#include <intrin.h>
#elif defined(__linux__)
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/utsname.h>
#endif

namespace Orange
{
    namespace Platform
    {

        OperatingSystem PlatformDetection::GetOperatingSystem()
        {
#ifdef _WIN32
            return OperatingSystem::Windows;
#elif defined(__linux__)
            return OperatingSystem::Linux;
#elif defined(__APPLE__)
            return OperatingSystem::MacOS;
#else
            return OperatingSystem::Unknown;
#endif
        }

        Architecture PlatformDetection::GetArchitecture()
        {
#ifdef _WIN32
            SYSTEM_INFO sysInfo;
            GetNativeSystemInfo(&sysInfo);
            switch (sysInfo.wProcessorArchitecture)
            {
            case PROCESSOR_ARCHITECTURE_AMD64:
                return Architecture::x64;
            case PROCESSOR_ARCHITECTURE_INTEL:
                return Architecture::x86;
            case PROCESSOR_ARCHITECTURE_ARM:
                return Architecture::ARM;
            case PROCESSOR_ARCHITECTURE_ARM64:
                return Architecture::ARM64;
            default:
                return Architecture::Unknown;
            }
#elif defined(__linux__) || defined(__APPLE__)
#if defined(__x86_64__)
            return Architecture::x64;
#elif defined(__i386__)
            return Architecture::x86;
#elif defined(__aarch64__)
            return Architecture::ARM64;
#elif defined(__arm__)
            return Architecture::ARM;
#else
            return Architecture::Unknown;
#endif
#else
            return Architecture::Unknown;
#endif
        }

        std::string PlatformDetection::GetOSVersion()
        {
#ifdef _WIN32
            OSVERSIONINFOEXW osvi;
            ZeroMemory(&osvi, sizeof(OSVERSIONINFOEXW));
            osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);

            if (GetVersionExW((LPOSVERSIONINFOW)&osvi))
            {
                return std::to_string(osvi.dwMajorVersion) + "." +
                       std::to_string(osvi.dwMinorVersion) + "." +
                       std::to_string(osvi.dwBuildNumber);
            }
            return "Unknown";
#elif defined(__linux__) || defined(__APPLE__)
            struct utsname unameData;
            if (uname(&unameData) == 0)
            {
                return std::string(unameData.release);
            }
            return "Unknown";
#else
            return "Unknown";
#endif
        }

        std::string PlatformDetection::GetCPUInfo()
        {
            std::string info;
#ifdef _WIN32
            char brand[0x40];
            unsigned int CPUInfo[4] = {0};
            __cpuid((int *)CPUInfo, 0x80000002);
            memcpy(brand, CPUInfo, sizeof(CPUInfo));
            __cpuid((int *)CPUInfo, 0x80000003);
            memcpy(brand + 16, CPUInfo, sizeof(CPUInfo));
            __cpuid((int *)CPUInfo, 0x80000004);
            memcpy(brand + 32, CPUInfo, sizeof(CPUInfo));
            brand[63] = '\0';
            info = brand;
#elif defined(__linux__)
            FILE *file = fopen("/proc/cpuinfo", "r");
            if (file)
            {
                char line[256];
                while (fgets(line, sizeof(line), file))
                {
                    if (strncmp(line, "model name", 10) == 0)
                    {
                        char *colon = strchr(line, ':');
                        if (colon)
                        {
                            info = colon + 2;
                            break;
                        }
                    }
                }
                fclose(file);
            }
#elif defined(__APPLE__)
            char brand[256];
            size_t size = sizeof(brand);
            if (sysctlbyname("machdep.cpu.brand_string", &brand, &size, nullptr, 0) == 0)
            {
                info = brand;
            }
#endif
            return info;
        }

        size_t PlatformDetection::GetTotalSystemMemory()
        {
#ifdef _WIN32
            MEMORYSTATUSEX memInfo;
            memInfo.dwLength = sizeof(MEMORYSTATUSEX);
            GlobalMemoryStatusEx(&memInfo);
            return memInfo.ullTotalPhys;
#elif defined(__linux__)
            struct sysinfo si;
            if (sysinfo(&si) == 0)
            {
                return si.totalram * si.mem_unit;
            }
#elif defined(__APPLE__)
            int mib[2] = {CTL_HW, HW_MEMSIZE};
            int64_t memsize;
            size_t len = sizeof(memsize);
            if (sysctl(mib, 2, &memsize, &len, nullptr, 0) == 0)
            {
                return memsize;
            }
#endif
            return 0;
        }

        bool PlatformDetection::IsFeatureSupported(const std::string &featureName)
        {
            // TODO: 实现特定功能检测
            return false;
        }

        char PlatformDetection::GetPathSeparator()
        {
#ifdef _WIN32
            return '\\';
#else
            return '/';
#endif
        }

        const char *PlatformDetection::GetLineEnding()
        {
#ifdef _WIN32
            return "\r\n";
#else
            return "\n";
#endif
        }

        std::string PlatformDetection::GetTempDirectory()
        {
#ifdef _WIN32
            char tempPath[MAX_PATH];
            GetTempPathA(MAX_PATH, tempPath);
            return std::string(tempPath);
#elif defined(__linux__)
            const char *temp = getenv("TMPDIR");
            if (!temp)
                temp = getenv("TMP");
            if (!temp)
                temp = getenv("TEMP");
            if (!temp)
                temp = "/tmp";
            return std::string(temp);
#elif defined(__APPLE__)
            return "/tmp";
#else
            return "";
#endif
        }

        std::string PlatformDetection::GetUserDirectory()
        {
#ifdef _WIN32
            char userPath[MAX_PATH]{};
            if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, userPath)))
            {
                return std::string(userPath);
            }
#elif defined(__linux__) || defined(__APPLE__)
            const char *home = getenv("HOME");
            if (home)
            {
                return std::string(home);
            }
#endif
            return "";
        }

        std::string PlatformDetection::GetAppDataDirectory()
        {
            std::string appDataPath;
#ifdef _WIN32
            char winAppDataPath[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, winAppDataPath)))
            {
                appDataPath = std::string(winAppDataPath);
            }
#elif defined(__linux__)
            const char *xdg = getenv("XDG_DATA_HOME");
            if (xdg)
            {
                appDataPath = std::string(xdg);
            }
            else
            {
                appDataPath = GetUserDirectory() + "/.local/share";
            }
#elif defined(__APPLE__)
            appDataPath = GetUserDirectory() + "/Library/Application Support";
#else
            appDataPath = "";
#endif
            return appDataPath;
        }

        std::string PlatformDetection::GetExecutableExtension()
        {
#ifdef _WIN32
            return ".exe";
#else
            return "";
#endif
        }

        std::string PlatformDetection::GetDynamicLibraryExtension()
        {
#ifdef _WIN32
            return ".dll";
#elif defined(__linux__)
            return ".so";
#elif defined(__APPLE__)
            return ".dylib";
#else
            return "";
#endif
        }

        std::string PlatformDetection::GetStaticLibraryExtension()
        {
#ifdef _WIN32
            return ".lib";
#else
            return ".a";
#endif
        }

    } // namespace Platform
} // namespace OrangeEngine