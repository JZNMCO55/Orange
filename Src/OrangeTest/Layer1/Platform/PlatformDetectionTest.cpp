#include <gtest/gtest.h>
#include "Layer1/Platform/Detection/PlatformDetection.h"

namespace Orange
{
    TEST(PlatformDetectionTest, OperatingSystem)
    {
        EXPECT_NE(Platform::PlatformDetection::GetOperatingSystem(), Platform::OperatingSystem::Unknown);
    }

    TEST(PlatformDetectionTest, Architecture)
    {
        EXPECT_NE(Platform::PlatformDetection::GetArchitecture(), Platform::Architecture::Unknown);
    }

    TEST(PlatformDetectionTest, OSVersion)
    {
        EXPECT_FALSE(Platform::PlatformDetection::GetOSVersion().empty());
    }

    TEST(PlatformDetectionTest, CPUInfo)
    {
        EXPECT_FALSE(Platform::PlatformDetection::GetCPUInfo().empty());
    }

    TEST(PlatformDetectionTest, TotalSystemMemory)
    {
        EXPECT_GT(Platform::PlatformDetection::GetTotalSystemMemory(), 0);
    }

    TEST(PlatformDetectionTest, PathSeparator)
    {
        EXPECT_EQ(Platform::PlatformDetection::GetPathSeparator(), '\\');
    }

    TEST(PlatformDetectionTest, LineEnding)
    {
        std::string lineEnding = Platform::PlatformDetection::GetLineEnding();
        EXPECT_STREQ(lineEnding.c_str(), "\r\n");
    }

    TEST(PlatformDetectionTest, TempDirectory)
    {
        EXPECT_FALSE(Platform::PlatformDetection::GetTempDirectory().empty());
    }

    TEST(PlatformDetectionTest, UserDirectory)
    {
        EXPECT_FALSE(Platform::PlatformDetection::GetUserDirectory().empty());
    }

    TEST(PlatformDetectionTest, AppDataDirectory)
    {
        EXPECT_FALSE(Platform::PlatformDetection::GetAppDataDirectory().empty());
    }

    TEST(PlatformDetectionTest, ExecutableExtension)
    {
        EXPECT_EQ(Platform::PlatformDetection::GetExecutableExtension(), ".exe");
    }

    TEST(PlatformDetectionTest, DynamicLibraryExtension)
    {
        EXPECT_EQ(Platform::PlatformDetection::GetDynamicLibraryExtension(), ".dll");
    }

    TEST(PlatformDetectionTest, StaticLibraryExtension)
    {
        EXPECT_EQ(Platform::PlatformDetection::GetStaticLibraryExtension(), ".lib");
    }

    TEST(PlatformDetectionTest, ExecutablePath)
    {
        EXPECT_FALSE(Platform::PlatformDetection::GetExecutablePath().empty());
    }

    TEST(PlatformDetectionTest, ExecutableDirectory)
    {
        EXPECT_FALSE(Platform::PlatformDetection::GetExecutableDirectory().empty());
    }
}
