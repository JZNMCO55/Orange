#ifndef ORANGE_ENGINE_CORE_RESULT_H
#define ORANGE_ENGINE_CORE_RESULT_H

// ---------------------------------------------------------------------------
// Core::Result —— 通用的成功 / 错误返回类型。
//
// 所有可能失败的引擎 API（asset load、schema 校验、physics fixture 替换……）
// 统一通过 Result<T, E> 返回。设计刻意保持极简：不提供 monadic chaining，
// 也不和 C++ exception 互通。调用方一律以 IsOk() / IsErr() 分支判断。
//
// ResultCode 是引擎默认错误枚举。模块如有需要可以定义自己的更丰富的 E
// 类型；但 ResultCode 应保持 coarse-grained——细粒度诊断信息走日志，
// 不要塞进枚举里。
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
                  "Result<T, E>: T 与 E 必须是不同类型，否则 variant 无法消歧。");

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
