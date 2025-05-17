#include "test.h"
#include "PlatformDetectionTest.hpp"
#include "LoggerTest.hpp"

namespace Orange
{
    void Test::test()
    {
        // 打印平台信息
        PrintPlatformInfo();

        // 测试Logger
        LoggerTest::Test();
    }
}
