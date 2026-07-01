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

namespace Orange::Engine::Physics
{

// 单条 raycast 命中结果。未命中时 hit=false、body 为 Invalid、其余为零。
struct RaycastHit
{
    BodyHandle body{};                 // 命中的 body（未命中时 Invalid）
    glm::vec2  point{0.0f, 0.0f};      // 命中点（世界坐标）
    glm::vec2  normal{0.0f, 0.0f};     // 命中处表面法线（世界坐标，单位向量）
    float      fraction{0.0f};         // 沿射线归一化距离 [0,1]，× maxDistance = 实际距离
    bool       hit{false};             // 是否命中
};

// 单个接触点。约定 normal 从被查询 body 指向 other（世界坐标单位向量）。
struct ContactPoint
{
    BodyHandle other{};                // 接触到的另一个 body
    glm::vec2  point{0.0f, 0.0f};      // 接触点（世界坐标）
    glm::vec2  normal{0.0f, 0.0f};     // 接触法线（从被查询 body 指向 other，世界坐标单位向量）
};

}  // namespace Orange::Engine::Physics

#endif  // ORANGE_ENGINE_PHYSICS_PHYSICS_QUERY_H
