#ifndef ORANGE_ENGINE_CORE_HANDLE_H
#define ORANGE_ENGINE_CORE_HANDLE_H

// ---------------------------------------------------------------------------
// Phase 1 / Task 04 — Core::TypedHandle
//
// Strongly-typed 64-bit handle. The `Tag` template parameter is a phantom
// type — its only role is to make `TypedHandle<MeshTag>` and
// `TypedHandle<TextureTag>` mutually incompatible at the type system, so
// the compiler refuses to silently swap a mesh handle for a texture one.
//
// 64 bits is split convention-wise into 32 bits index + 32 bits generation
// by the storage layer (e.g. AssetRegistry), but Core deliberately keeps
// the representation opaque — only the storage that minted a handle knows
// how to crack it. Consumers MUST treat the value as an opaque token.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>

namespace Orange::Engine
{

template <typename Tag>
class TypedHandle
{
public:
    using ValueType = std::uint64_t;
    static constexpr ValueType kInvalidValue = std::numeric_limits<ValueType>::max();

    constexpr TypedHandle() noexcept = default;
    constexpr explicit TypedHandle(ValueType value) noexcept : mValue(value) {}

    constexpr ValueType Value() const noexcept { return mValue; }
    constexpr bool IsValid() const noexcept { return mValue != kInvalidValue; }
    constexpr explicit operator bool() const noexcept { return IsValid(); }

    static constexpr TypedHandle Invalid() noexcept { return TypedHandle{kInvalidValue}; }

    friend constexpr bool operator==(TypedHandle a, TypedHandle b) noexcept { return a.mValue == b.mValue; }
    friend constexpr bool operator!=(TypedHandle a, TypedHandle b) noexcept { return a.mValue != b.mValue; }
    friend constexpr bool operator<(TypedHandle a, TypedHandle b) noexcept { return a.mValue < b.mValue; }

private:
    ValueType mValue{kInvalidValue};
};

}  // namespace Orange::Engine

namespace std
{

template <typename Tag>
struct hash<::Orange::Engine::TypedHandle<Tag>>
{
    std::size_t operator()(const ::Orange::Engine::TypedHandle<Tag>& handle) const noexcept
    {
        return std::hash<typename ::Orange::Engine::TypedHandle<Tag>::ValueType>{}(handle.Value());
    }
};

}  // namespace std

#endif  // ORANGE_ENGINE_CORE_HANDLE_H
