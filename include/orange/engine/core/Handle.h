#ifndef ORANGE_ENGINE_CORE_HANDLE_H
#define ORANGE_ENGINE_CORE_HANDLE_H

// ---------------------------------------------------------------------------
// Core::TypedHandle —— 强类型 64-bit handle。
//
// 模板参数 `Tag` 是 phantom type：唯一作用是让
// `TypedHandle<MeshTag>` 与 `TypedHandle<TextureTag>` 在类型系统层面互不
// 兼容，编译器拒绝把一个 mesh handle 悄悄当作 texture handle 传递。
//
// 64 位的 layout 习惯切成 32 位 index + 32 位 generation，由具体的存储
// 层（如 AssetRegistry）来约定；Core 这一层刻意保持不透明——只有铸出
// handle 的存储自己知道怎么解读它。消费者必须把这个值视为 opaque token。
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
        using ValueType                          = std::uint64_t;
        static constexpr ValueType kInvalidValue = std::numeric_limits<ValueType>::max();

        constexpr TypedHandle() noexcept = default;
        constexpr explicit TypedHandle(ValueType value) noexcept : mValue(value) {}

        constexpr ValueType Value() const noexcept { return mValue; }
        constexpr bool      IsValid() const noexcept { return mValue != kInvalidValue; }
        constexpr explicit  operator bool() const noexcept { return IsValid(); }

        static constexpr TypedHandle Invalid() noexcept { return TypedHandle{kInvalidValue}; }

        friend constexpr bool operator==(TypedHandle a, TypedHandle b) noexcept { return a.mValue == b.mValue; }
        friend constexpr bool operator!=(TypedHandle a, TypedHandle b) noexcept { return a.mValue != b.mValue; }
        friend constexpr bool operator<(TypedHandle a, TypedHandle b) noexcept { return a.mValue < b.mValue; }

    private:
        ValueType mValue{kInvalidValue};
    };

} // namespace Orange::Engine

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

} // namespace std

#endif // ORANGE_ENGINE_CORE_HANDLE_H
