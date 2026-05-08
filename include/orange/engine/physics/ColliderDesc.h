#ifndef ORANGE_ENGINE_PHYSICS_COLLIDER_DESC_H
#define ORANGE_ENGINE_PHYSICS_COLLIDER_DESC_H

// ---------------------------------------------------------------------------
// ColliderDesc —— 几何形状描述符（Circle / Box / Polygon / EdgeChain）。
//
// 全部 trivially-copyable POD，让 ColliderComponent 持 std::variant<...>
// 仍然 trivially-copyable——EnTT archetype 行迁移时可走 memcpy 路径，
// 与其它 trivially-copyable component 同代价。
//
// 关于 Polygon / EdgeChain 的 fixed-size 选型：Box2D 3.x 的
// `b2_maxPolygonVertices` 是 8，与 PolygonDesc::kMaxVertices 对齐。EdgeChain
// 实际能更长（场景级折线），本期定 16 顶点上限——足够覆盖大多数 2D 平台
// 跳跃场景的 ground polyline；超长折线由调用方自己拆段。如果未来撞上
// 真实需要更长链的场景，再加 heap-allocated chain 类型（会让 variant 退化
// 到 non-trivially-copyable，届时再权衡）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>

namespace Orange::Engine::Physics
{

struct CircleDesc
{
    float     radius{1.0f};
    glm::vec2 center{0.0f, 0.0f};  // local offset (本地坐标系中心点)
};

struct BoxDesc
{
    glm::vec2 halfExtents{0.5f, 0.5f};  // 半宽 / 半高
    glm::vec2 center{0.0f, 0.0f};
};

struct PolygonDesc
{
    // Box2D 3.x 单 polygon shape 上限是 8 顶点（b2_maxPolygonVertices）。
    static constexpr std::uint32_t kMaxVertices = 8;

    std::array<glm::vec2, kMaxVertices> vertices{};
    std::uint32_t                       count{0};  // 实际使用的顶点数
};

struct EdgeChainDesc
{
    // 多段折线的顶点上限。超出由调用方拆分多个 ColliderComponent 处理。
    static constexpr std::uint32_t kMaxVertices = 16;

    std::array<glm::vec2, kMaxVertices> vertices{};
    std::uint32_t                       count{0};
    bool                                isLoop{false};  // true = 闭环（首尾自动连）
};

}  // namespace Orange::Engine::Physics

#endif  // ORANGE_ENGINE_PHYSICS_COLLIDER_DESC_H
