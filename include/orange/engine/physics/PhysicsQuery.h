#ifndef ORANGE_ENGINE_PHYSICS_PHYSICS_QUERY_H
#define ORANGE_ENGINE_PHYSICS_PHYSICS_QUERY_H

// ---------------------------------------------------------------------------
// PhysicsQuery —— PhysicsWorld 空间查询（raycast / overlap / point / contact）
// 返回的中性 POD 结果类型。
//
// 与 BodyTransform 一样，这些是不含任何后端（Box2D）类型的纯数据结构——
// 公共面**不**含 b2 类型（CLAUDE.md "Header isolation" 不变量：`<box2d/...>`
// 只允许出现在 src/physics/box2d/**）。坐标全部为世界坐标（world space）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/physics/BodyHandle.h>

#include <glm/vec2.hpp>

#include <cstdint>

namespace Orange::Engine::Physics
{

    // 空间查询过滤器：只报告与之匹配的 collider。默认全通过（match all）。
    // 命中规则（对齐 Box2D）：(filter.maskBits & collider.categoryBits) && (collider.maskBits & filter.categoryBits)。
    // 用 std::uint32_t 与 ColliderComponent 的 category/mask 位对齐，扩宽到 Box2D
    // 的 uint64 b2QueryFilter 由 PhysicsWorld 内部隐式完成。
    struct QueryFilter
    {
        std::uint32_t categoryBits{0xFFFFFFFFu}; // 本次查询“代表”的类
        std::uint32_t maskBits{0xFFFFFFFFu};     // 本次查询愿命中哪些类
    };

    // 单条 raycast 命中结果。未命中时 hit=false、body 为 Invalid、其余为零。
    struct RaycastHit
    {
        BodyHandle body{};             // 命中的 body（未命中时 Invalid）
        glm::vec2  point{0.0f, 0.0f};  // 命中点（世界坐标）
        glm::vec2  normal{0.0f, 0.0f}; // 命中处表面法线（世界坐标，单位向量）
        float      fraction{0.0f};     // 沿射线归一化距离 [0,1]，× maxDistance = 实际距离
        bool       hit{false};         // 是否命中
    };

    // 单个接触点。约定 normal 从被查询 body 指向 other（世界坐标单位向量）。
    struct ContactPoint
    {
        BodyHandle other{};            // 接触到的另一个 body
        glm::vec2  point{0.0f, 0.0f};  // 接触点（世界坐标）
        glm::vec2  normal{0.0f, 0.0f}; // 接触法线（从被查询 body 指向 other，世界坐标单位向量）
    };

    // sensor 触发事件：visitor 进入 / 离开 sensor。begin 与 end 共用此结构。
    struct SensorEvent
    {
        BodyHandle sensor{};  // sensor body
        BodyHandle visitor{}; // 进入 / 离开 sensor 的 body（end 事件中 visitor 可能已销毁 → Invalid）
    };

    // contact 开始事件：两 body 开始接触，带初始接触点 / 法线（法线从 bodyA 指向 bodyB）。
    struct ContactBeginEvent
    {
        BodyHandle bodyA{};
        BodyHandle bodyB{};
        glm::vec2  point{0.0f, 0.0f};  // 初始接触点（世界坐标；取 manifold 首点，无点则零）
        glm::vec2  normal{0.0f, 0.0f}; // 接触法线（world，从 bodyA 指向 bodyB；无点则零）
    };

    // contact 结束事件：两 body 停止接触（可能因某 body 销毁 → 对应 handle 为 Invalid）。
    struct ContactEndEvent
    {
        BodyHandle bodyA{};
        BodyHandle bodyB{};
    };

} // namespace Orange::Engine::Physics

#endif // ORANGE_ENGINE_PHYSICS_PHYSICS_QUERY_H
