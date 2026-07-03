// LayerVisibilitySync 实现：World × WorldPartition × PhysicsWorld 三方
// 拼装。本 .cpp 不直接消费 Box2D；走 PhysicsWorld 公共面（SetBodyEnabled）
// 即可——保留 "<box2d/...> 只出现在 src/physics/box2d/** + PhysicsWorld.cpp"
// 的不变量。

#include "orange/engine/physics/LayerVisibilitySync.h"

#include "orange/engine/physics/PhysicsWorld.h"
#include "orange/engine/physics/RigidBodyComponent.h"
#include "orange/engine/scene/World.h"
#include "orange/engine/scene/WorldPartition.h"

#include <entt/entt.hpp>

namespace Orange::Engine::Physics
{

    void ApplyLayerVisibility(const Orange::Engine::World&                 world,
                              const Orange::Engine::Scene::WorldPartition& partition,
                              PhysicsWorld&                                physics)
    {
        auto& registry = const_cast<Orange::Engine::World&>(world).Registry();
        auto  view     = registry.view<RigidBodyComponent>();
        for (auto e : view)
        {
            const auto& rb = view.get<RigidBodyComponent>(e);
            if (!rb.handle.IsValid())
            {
                continue;
            }
            const auto entity  = Orange::Engine::World::FromEntt(e);
            const bool visible = partition.IsEntityVisible(world, entity);
            physics.SetBodyEnabled(rb.handle, visible);
        }
    }

} // namespace Orange::Engine::Physics
