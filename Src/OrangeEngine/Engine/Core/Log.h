#ifndef LOG_H
#define LOG_H

#include "OrangeExport.h"
#include "spdlog/spdlog.h"

namespace Orange 
{

class ORANGE_API Log
{
public:
    Log();
    ~Log();

    static void Init();

    inline static std::shared_ptr<spdlog::logger>& GetOrangeLogger();

    inline static std::shared_ptr<spdlog::logger>& GetClientLogger();
private:
    static std::shared_ptr<spdlog::logger> msOrangeLogger;
    static std::shared_ptr<spdlog::logger> msClientLogger;
};

    inline std::shared_ptr<spdlog::logger>& Log::GetOrangeLogger()
    {
        return msOrangeLogger;
    }
    inline std::shared_ptr<spdlog::logger>& Log::GetClientLogger()
    {
        return msClientLogger;
    }
}
#endif // LOG_H

#define ORANGE_LOG_ERROR(...)    ::Orange::Log::GetOrangeLogger()->error(__VA_ARGS__)
#define ORANGE_LOG_WARN(...)     ::Orange::Log::GetOrangeLogger()->warn(__VA_ARGS__)
#define ORANGE_LOG_INFO(...)     ::Orange::Log::GetOrangeLogger()->info(__VA_ARGS__)
#define ORANGE_LOG_TRACE(...)    ::Orange::Log::GetOrangeLogger()->trace(__VA_ARGS__)

#define CLIENT_LOG_ERROR(...)    ::Orange::Log::GetClientLogger()->error(__VA_ARGS__)
#define CLIENT_LOG_WARN(...)     ::Orange::Log::GetClientLogger()->warn(__VA_ARGS__)
#define CLIENT_LOG_INFO(...)     ::Orange::Log::GetClientLogger()->info(__VA_ARGS__)
#define CLIENT_LOG_TRACE(...)    ::Orange::Log::GetClientLogger()->trace(__VA_ARGS__)

// if distribution, define log macro with nothing
#ifdef ORANGE_DISTRIBUTION
#define ORANGE_LOG_ERROR(...)
#define ORANGE_LOG_WARN(...)
#define ORANGE_LOG_INFO(...)
#define ORANGE_LOG_TRACE(...)

#define CLIENT_LOG_ERROR(...)
#define CLIENT_LOG_WARN(...)
#define CLIENT_LOG_INFO(...)
#define CLIENT_LOG_TRACE(...)
#endif // ORANGE_DISTRIBUTION