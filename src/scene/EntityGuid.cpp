// EntityGuid 实现 —— GuidComponent 的惰性分配与 clone 后重分配。

#include "orange/engine/scene/EntityGuid.h"

#include "orange/engine/core/Guid.h"
#include "orange/engine/scene/GuidComponent.h"
#include "orange/engine/scene/World.h"

#include <entt/entt.hpp>

#include <vector>

namespace Orange::Engine::Scene
{

std::size_t EnsureEntityGuids(World& world)
{
    // 先收集需要补的实体再写入——避免在遍历 view 的同时 emplace 改动存储。
    // （emplace 的是新 component 类型，对 entt::entity view 的 packed array
    // 理论上无碍，但先收集后写更稳，且实体数不大。）
    std::vector<Entity> missing;
    for (auto e : world.Registry().view<entt::entity>())
    {
        const Entity entity = World::FromEntt(e);
        if (!world.HasComponent<GuidComponent>(entity))
        {
            missing.push_back(entity);
        }
    }

    for (const Entity entity : missing)
    {
        world.AddComponent(entity, GuidComponent{Core::Guid::Generate()});
    }

    return missing.size();
}

std::size_t ReassignEntityGuids(World& world, std::span<const Entity> entities)
{
    std::size_t count = 0;
    for (const Entity entity : entities)
    {
        if (!world.IsValid(entity))
        {
            continue;
        }
        // emplace_or_replace 语义：无论原来有没有 GuidComponent 都换成全新 GUID。
        world.AddComponent(entity, GuidComponent{Core::Guid::Generate()});
        ++count;
    }
    return count;
}

}  // namespace Orange::Engine::Scene
