#include <orange/engine/nav/NavGridBake.h>

#include <orange/engine/nav/NavGrid.h>
#include <orange/engine/physics/PhysicsQuery.h>
#include <orange/engine/physics/PhysicsWorld.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <vector>

namespace Orange::Engine::Nav
{

void BakeWalkableFromPhysics(NavGrid& grid, const Physics::PhysicsWorld& physics,
                             std::uint32_t solidMask)
{
    for (int y = 0; y < grid.height; ++y)
    {
        for (int x = 0; x < grid.width; ++x)
        {
            glm::vec2 lower{0.0f, 0.0f};
            glm::vec2 upper{0.0f, 0.0f};
            CellBoundsWorld(grid, x, y, lower, upper);

            // categoryBits 全通 + maskBits = solidMask：只有 collider 的
            // categoryBits 命中 solidMask 的 body 才算"实心阻挡"。命中任一 →
            // 该 cell 阻挡。宽相过报即保守多标墙，符合导航烘焙语义。
            const std::vector<Physics::BodyHandle> hits =
                physics.OverlapAABB(lower, upper,
                                    Physics::QueryFilter{0xFFFFFFFFu, solidMask});
            SetWalkable(grid, x, y, hits.empty());
        }
    }
}

}  // namespace Orange::Engine::Nav
