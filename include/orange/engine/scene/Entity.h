#ifndef ORANGE_ENGINE_SCENE_ENTITY_H
#define ORANGE_ENGINE_SCENE_ENTITY_H

// ---------------------------------------------------------------------------
// Entity —— ECS 中"实体"的对外身份。
//
// 公共面只暴露一枚 64-bit opaque 值；高低位的拆分（index + generation
// 还是其它）由 World 后端自由决定，调用方不应做位运算解读。这一层与
// Core::TypedHandle 的精神一致——id-as-value，不带语义。
//
// 为什么不直接 `using Entity = TypedHandle<EntityTag>`？
//   * Entity 是引擎里使用频率最高的 domain type 之一；nominal class 比
//     alias 在调试器、报错信息、IDE 跳转里读起来更清晰。
//   * 未来若需要给 Entity 加 sub-state（例如 deferred-destroy 标志位、
//     world generation 指针等），扩展 nominal class 不破调用点；alias
//     就要全仓改。
//
// std::hash 在文件末尾给出特化，便于 World 内部及调用方直接拿 Entity
// 当 unordered_map key。
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>

namespace Orange::Engine
{

class Entity
{
public:
    using ValueType = std::uint64_t;
    static constexpr ValueType kInvalidValue = std::numeric_limits<ValueType>::max();

    constexpr Entity() noexcept = default;
    constexpr explicit Entity(ValueType value) noexcept : mValue(value) {}

    constexpr ValueType Value() const noexcept { return mValue; }
    constexpr bool      IsValid() const noexcept { return mValue != kInvalidValue; }
    constexpr explicit  operator bool() const noexcept { return IsValid(); }

    static constexpr Entity Invalid() noexcept { return Entity{kInvalidValue}; }

    friend constexpr bool operator==(Entity a, Entity b) noexcept { return a.mValue == b.mValue; }
    friend constexpr bool operator!=(Entity a, Entity b) noexcept { return a.mValue != b.mValue; }
    friend constexpr bool operator< (Entity a, Entity b) noexcept { return a.mValue <  b.mValue; }

private:
    ValueType mValue{kInvalidValue};
};

}  // namespace Orange::Engine

namespace std
{

template <>
struct hash<::Orange::Engine::Entity>
{
    std::size_t operator()(const ::Orange::Engine::Entity& e) const noexcept
    {
        return std::hash<::Orange::Engine::Entity::ValueType>{}(e.Value());
    }
};

}  // namespace std

#endif  // ORANGE_ENGINE_SCENE_ENTITY_H
