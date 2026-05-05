#ifndef ORANGE_ENGINE_CORE_HASH_H
#define ORANGE_ENGINE_CORE_HASH_H

// ---------------------------------------------------------------------------
// Phase 1 / Task 04 — Core::Hash
//
// Compile-time FNV-1a (64-bit) and the StringId / Id wrappers built on top.
//
// Design notes:
// * FNV-1a is chosen for its constexpr-friendliness, not collision strength.
//   Collisions in 2^64 with engine-scale string sets are negligible; if a
//   subsystem needs guaranteed uniqueness it must keep its own table.
// * StringId stores ONLY the hash, never the original string. Debug-build
//   symbol tables (Phase 6+) can be reintroduced as a separate facility
//   without changing StringId's ABI.
// * The user-defined literal `_sid` enables `"player.spawn"_sid` ergonomics
//   while preserving constexpr evaluation.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Orange::Engine
{

namespace Detail
{

inline constexpr std::uint64_t kFnv1aOffset64 = 0xcbf29ce484222325ULL;
inline constexpr std::uint64_t kFnv1aPrime64  = 0x00000100000001b3ULL;

}  // namespace Detail

constexpr std::uint64_t Fnv1a64(const char* data, std::size_t length) noexcept
{
    std::uint64_t hash = Detail::kFnv1aOffset64;
    for (std::size_t i = 0; i < length; ++i)
    {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(data[i]));
        hash *= Detail::kFnv1aPrime64;
    }
    return hash;
}

constexpr std::uint64_t Fnv1a64(std::string_view text) noexcept
{
    return Fnv1a64(text.data(), text.size());
}

class Id
{
public:
    constexpr Id() noexcept = default;
    constexpr explicit Id(std::uint64_t value) noexcept : mValue(value) {}

    constexpr std::uint64_t Value() const noexcept { return mValue; }
    constexpr bool IsValid() const noexcept { return mValue != 0; }
    constexpr explicit operator bool() const noexcept { return IsValid(); }

    friend constexpr bool operator==(Id a, Id b) noexcept { return a.mValue == b.mValue; }
    friend constexpr bool operator!=(Id a, Id b) noexcept { return a.mValue != b.mValue; }
    friend constexpr bool operator<(Id a, Id b) noexcept { return a.mValue < b.mValue; }

private:
    std::uint64_t mValue{0};
};

class StringId
{
public:
    constexpr StringId() noexcept = default;
    constexpr explicit StringId(std::string_view text) noexcept : mHash(Fnv1a64(text)) {}
    constexpr explicit StringId(std::uint64_t precomputedHash) noexcept : mHash(precomputedHash) {}

    constexpr std::uint64_t Value() const noexcept { return mHash; }
    constexpr bool IsValid() const noexcept { return mHash != 0; }
    constexpr explicit operator bool() const noexcept { return IsValid(); }

    friend constexpr bool operator==(StringId a, StringId b) noexcept { return a.mHash == b.mHash; }
    friend constexpr bool operator!=(StringId a, StringId b) noexcept { return a.mHash != b.mHash; }
    friend constexpr bool operator<(StringId a, StringId b) noexcept { return a.mHash < b.mHash; }

private:
    std::uint64_t mHash{0};
};

namespace Literals
{

constexpr StringId operator""_sid(const char* str, std::size_t length) noexcept
{
    return StringId{std::string_view{str, length}};
}

}  // namespace Literals

}  // namespace Orange::Engine

#endif  // ORANGE_ENGINE_CORE_HASH_H
