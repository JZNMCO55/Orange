#ifndef ORANGE_ENGINE_NAV_NAV_GRID_BAKE_H
#define ORANGE_ENGINE_NAV_NAV_GRID_BAKE_H

// ---------------------------------------------------------------------------
// NavGridBake —— 从物理世界烘焙 NavGrid 的可行走性。
//
// 单独成头，让核心 NavGrid / Pathfinding 不强依赖 Physics —— 只有需要"从
// 碰撞体生成导航网格"的调用方才 include 本文件。Physics 公共头本身零后端
// 类型（无 box2d），故引它不破坏 header isolation。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/nav/NavGrid.h>
#include <orange/engine/physics/PhysicsWorld.h>

#include <cstdint>

namespace Orange::Engine::Nav
{

    // 从物理世界烘焙网格可行走性：对每个 cell，用其世界 AABB 查
    // PhysicsWorld::OverlapAABB；命中任一（solidMask 过滤的）body → 标该 cell
    // 阻挡，否则可走。OverlapAABB 是宽相（broad-phase）查询，可能过报 —— 对导航
    // 烘焙即"保守阻挡"（宁可多标墙，不漏标可穿），可接受。
    ORANGE_ENGINE_API void BakeWalkableFromPhysics(NavGrid& grid, const Physics::PhysicsWorld& physics,
                                                   std::uint32_t solidMask = 0xFFFFFFFFu);

} // namespace Orange::Engine::Nav

#endif // ORANGE_ENGINE_NAV_NAV_GRID_BAKE_H
