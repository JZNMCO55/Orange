#ifndef ORANGE_ENGINE_CORE_RESULT_H
#define ORANGE_ENGINE_CORE_RESULT_H

// ---------------------------------------------------------------------------
// Phase 1 / Task 04 — Core::Result
//
// Discriminated success/error return type. Used by every fallible engine API
// (asset load, schema validation, physics fixture replacement, ...). The
// design choice is intentionally minimal: no monadic chaining helpers in
// Phase 1, no exception interop. Callers branch on `IsOk()` / `IsErr()`.
//
// `ResultCode` is the canonical engine-wide error enum. Modules MAY define
// their own richer `E` types, but `ResultCode` is the default — keep new
// codes coarse-grained; granular diagnostics belong in attached log lines,
// not in the enum.
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstdint>
#include <type_traits>
#include <utility>
#include <variant>

namespace Orange::Engine
{

enum class ResultCode : std::uint32_t
{
    Ok = 0,
    Unknown,
    InvalidArgument,
    OutOfRange,
    NotFound,
    AlreadyExists,
    IoError,
    PermissionDenied,
    Unsupported,
    SchemaMismatch,
    OutOfMemory,
    NotInitialized,
    AlreadyInitialized,
    InternalError,
};

ORANGE_ENGINE_API const char* ToString(ResultCode code) noexcept;

template <typename T, typename E = ResultCode>
class Result
{
public:
    using ValueType = T;
    using ErrorType = E;

    static_assert(!std::is_same_v<T, E>,
                  "Result<T, E>: T and E must be distinct types so the variant can disambiguate.");

    constexpr Result(const T& value) : mStorage(std::in_place_index<0>, value) {}
    constexpr Result(T&& value) : mStorage(std::in_place_index<0>, std::move(value)) {}
    constexpr Result(const E& error) : mStorage(std::in_place_index<1>, error) {}
    constexpr Result(E&& error) : mStorage(std::in_place_index<1>, std::move(error)) {}

    constexpr bool IsOk() const noexcept { return mStorage.index() == 0; }
    constexpr bool IsErr() const noexcept { return mStorage.index() == 1; }
    constexpr explicit operator bool() const noexcept { return IsOk(); }

    constexpr T& Value() & { return std::get<0>(mStorage); }
    constexpr const T& Value() const& { return std::get<0>(mStorage); }
    constexpr T&& Value() && { return std::get<0>(std::move(mStorage)); }

    constexpr E& Error() & { return std::get<1>(mStorage); }
    constexpr const E& Error() const& { return std::get<1>(mStorage); }

    constexpr T ValueOr(T fallback) const&
    {
        return IsOk() ? std::get<0>(mStorage) : std::move(fallback);
    }

private:
    std::variant<T, E> mStorage;
};

template <typename E>
class Result<void, E>
{
public:
    using ErrorType = E;

    constexpr Result() noexcept = default;
    constexpr Result(const E& error) : mError(error), mHasError(true) {}
    constexpr Result(E&& error) : mError(std::move(error)), mHasError(true) {}

    constexpr bool IsOk() const noexcept { return !mHasError; }
    constexpr bool IsErr() const noexcept { return mHasError; }
    constexpr explicit operator bool() const noexcept { return IsOk(); }

    constexpr const E& Error() const noexcept { return mError; }

private:
    E mError{};
    bool mHasError{false};
};

}  // namespace Orange::Engine

#endif  // ORANGE_ENGINE_CORE_RESULT_H
