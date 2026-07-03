// Core::Result 实现
//
// 唯一脱内联到这里的部分是 `ToString(ResultCode)`。单独放一个 TU 是为
// 了：之后新增错误码时只动这一个文件；并且让 Result.h 的消费者每次
// include 时不必牵扯字符串表。

#include "orange/engine/core/Result.h"

namespace Orange::Engine
{

    const char* ToString(ResultCode code) noexcept
    {
        switch (code)
        {
            case ResultCode::Ok:
                return "Ok";
            case ResultCode::Unknown:
                return "Unknown";
            case ResultCode::InvalidArgument:
                return "InvalidArgument";
            case ResultCode::OutOfRange:
                return "OutOfRange";
            case ResultCode::NotFound:
                return "NotFound";
            case ResultCode::AlreadyExists:
                return "AlreadyExists";
            case ResultCode::IoError:
                return "IoError";
            case ResultCode::PermissionDenied:
                return "PermissionDenied";
            case ResultCode::Unsupported:
                return "Unsupported";
            case ResultCode::SchemaMismatch:
                return "SchemaMismatch";
            case ResultCode::OutOfMemory:
                return "OutOfMemory";
            case ResultCode::NotInitialized:
                return "NotInitialized";
            case ResultCode::AlreadyInitialized:
                return "AlreadyInitialized";
            case ResultCode::InternalError:
                return "InternalError";
        }
        return "ResultCode(?)";
    }

} // namespace Orange::Engine
