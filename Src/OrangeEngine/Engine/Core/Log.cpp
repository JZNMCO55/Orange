#include "../pch.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "Log.h"

namespace Orange
{
    std::shared_ptr<spdlog::logger> Log::msOrangeLogger;
    std::shared_ptr<spdlog::logger> Log::msClientLogger;
    Log::Log()
    {
    }

    Log::~Log()
    {

    }

    void Log::Init()
    {
        spdlog::set_pattern("%^[%T] %n: %v%$");

        msOrangeLogger = spdlog::stdout_color_mt("Orange");
        msOrangeLogger->set_level(spdlog::level::trace);

        msClientLogger = spdlog::stdout_color_mt("Client");
        msClientLogger->set_level(spdlog::level::trace);
    }

    inline std::shared_ptr<spdlog::logger>& Log::GetOrangeLogger()
    {
        return msOrangeLogger;
    }
    inline std::shared_ptr<spdlog::logger>& Log::GetClientLogger()
    {
        return msClientLogger;
    }
}