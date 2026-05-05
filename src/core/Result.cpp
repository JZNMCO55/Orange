// ---------------------------------------------------------------------------
// Phase 1 / Task 04 — Core::Result implementation
//
// Only out-of-line piece is `ToString(ResultCode)`. Kept in its own TU so
// subsequent error-code additions are a single-file edit and so consumers
// of Result.h don't pull in a string table at every include site.
// ---------------------------------------------------------------------------

#include "orange/engine/core/Result.h"

namespace Orange::Engine
{

const char* ToString(ResultCode code) noexcept
{
    switch (code)
    {
        case ResultCode::Ok:                  return "Ok";
        case ResultCode::Unknown:             return "Unknown";
        case ResultCode::InvalidArgument:     return "InvalidArgument";
        case ResultCode::OutOfRange:          return "OutOfRange";
        case ResultCode::NotFound:            return "NotFound";
        case ResultCode::AlreadyExists:       return "AlreadyExists";
        case ResultCode::IoError:             return "IoError";
        case ResultCode::PermissionDenied:    return "PermissionDenied";
        case ResultCode::Unsupported:         return "Unsupported";
        case ResultCode::SchemaMismatch:      return "SchemaMismatch";
        case ResultCode::OutOfMemory:         return "OutOfMemory";
        case ResultCode::NotInitialized:      return "NotInitialized";
        case ResultCode::AlreadyInitialized:  return "AlreadyInitialized";
        case ResultCode::InternalError:       return "InternalError";
    }
    return "ResultCode(?)";
}

}  // namespace Orange::Engine
