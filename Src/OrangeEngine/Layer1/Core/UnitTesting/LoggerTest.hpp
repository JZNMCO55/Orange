#ifndef LOGGER_TEST_HPP
#define LOGGER_TEST_HPP

#include "Core/Debug/Logger.h"

namespace Orange
{
    class LoggerTest
    {
    public:
        static void Test()
        {
            Logger::Init();
            ORG_LOG_INFO("Log Info");
            ORG_LOG_WARN("Log Warn");
            ORG_LOG_ERROR("Log Error");
            ORG_LOG_DEBUG("Log Debug");
            ORG_LOG_TRACE("Log Trace");
            ORG_LOG_CRITICAL("Log Critical");
        }
    };
}
#endif