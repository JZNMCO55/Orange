#ifndef ORANGE_ENGINE_CORE_HASH_H
#define ORANGE_ENGINE_CORE_HASH_H

// ---------------------------------------------------------------------------
// Core::Hash —— 编译期 FNV-1a (64-bit)，以及构筑其上的 StringId / Id。
//
// 设计要点：
// * 选 FNV-1a 是因为它对 constexpr 友好，并不为了 collision strength。
//   2^64 hash 空间下，引擎规模的字符串集合发生碰撞的概率可以忽略；某子
//   系统若需要可证唯一，应自己维护查找表。
// * StringId 只存 hash，不保留原始字符串。Debug build 下的符号表
//   （后续 phase）可以作为独立机制叠加，不会影响 StringId 的 ABI。
// * 用户字面量 `_sid` 让 `"player.spawn"_sid` 写法保持 constexpr 求值。
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

    } // namespace Detail

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
        constexpr bool          IsValid() const noexcept { return mValue != 0; }
        constexpr explicit      operator bool() const noexcept { return IsValid(); }

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
        constexpr bool          IsValid() const noexcept { return mHash != 0; }
        constexpr explicit      operator bool() const noexcept { return IsValid(); }

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

    } // namespace Literals

} // namespace Orange::Engine

#endif // ORANGE_ENGINE_CORE_HASH_H
