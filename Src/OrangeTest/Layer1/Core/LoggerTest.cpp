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
}
