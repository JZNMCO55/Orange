#include <gtest/gtest.h>
#include "Layer1/Core/Debug/Logger.h"

namespace Orange
{
    TEST(LoggerTest, BasicLogging)
    {
        Logger::Init();
        EXPECT_NO_THROW(ORG_LOG_INFO("Log Info"));
        EXPECT_NO_THROW(ORG_LOG_WARN("Log Warn"));
        EXPECT_NO_THROW(ORG_LOG_ERROR("Log Error"));
        EXPECT_NO_THROW(ORG_LOG_DEBUG("Log Debug"));
        EXPECT_NO_THROW(ORG_LOG_TRACE("Log Trace"));
        EXPECT_NO_THROW(ORG_LOG_CRITICAL("Log Critical"));
    }

    TEST(LoggerTest, FormattedLogging)
    {
        Logger::Init();
        int seed = 42;
        std::string name = "TestApp";
        double value = 3.14159;

        EXPECT_NO_THROW(ORG_LOG_INFO("Random number generator initialized with seed: {}", seed));
        EXPECT_NO_THROW(ORG_LOG_INFO("Application {} started with value: {:.2f}", name, value));
        EXPECT_NO_THROW(ORG_LOG_ERROR("Failed to process request {} with error code: {}", 123, 404));
    }
}
