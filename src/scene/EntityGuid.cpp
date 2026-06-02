// EntityGuid 实现 —— GuidComponent 的惰性分配与 clone 后重分配。

#include "orange/engine/scene/EntityGuid.h"

#include "orange/engine/core/Guid.h"
#include "orange/engine/scene/GuidComponent.h"
#include "orange/engine/scene/PrefabInstanceComponent.h"
#include "orange/engine/scene/World.h"

#include <entt/entt.hpp>

#include <utility>
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

std::size_t SeparateClonedIdentities(World& world, std::span<const Entity> created)
{
    // 1) per-entity 身份 GUID：直接复用 ReassignEntityGuids（对无 GuidComponent
    //    的实体走 emplace_or_replace，无副作用——见其语义）。
    ReassignEntityGuids(world, created);

    // 2) PrefabInstanceComponent.instanceId 重映射。建一张"旧 instanceId →
    //    新 instanceId"表，保证同一旧 instanceId 的所有克隆体拿到同一个新 id
    //    （保留分组），不同旧 instanceId 各自分到不同新 id。Core::Guid 没有
    //    std::hash 特化，且一次 clone 里 prefab 实例数极少（通常 0 ~ 1），用
    //    小 vector 线性查找比为它加全局 hash 特化更克制（避免侵入 Core 公共面）。
    std::vector<std::pair<Core::Guid, Core::Guid>> idRemap;
    std::size_t count = 0;
    for (const Entity entity : created)
    {
        if (!world.IsValid(entity))
        {
            continue;
        }
        auto* link = world.GetComponent<PrefabInstanceComponent>(entity);
        if (link == nullptr)
        {
            continue;
        }
        Core::Guid newId{};
        bool found = false;
        for (const auto& [oldId, mapped] : idRemap)
        {
            if (oldId == link->instanceId)
            {
                newId = mapped;
                found = true;
                break;
            }
        }
        if (!found)
        {
            newId = Core::Guid::Generate();
            idRemap.emplace_back(link->instanceId, newId);
        }
        link->instanceId = newId;
        ++count;
    }
    return count;
}

Entity FindEntityByGuid(const World& world, const Core::Guid& guid)
{
    // guid 非法（全 0 = 未分配）直接判负——否则会匹配到恰好也未分配的实体（语义错误）。
    if (!guid.IsValid())
    {
        return Entity::Invalid();
    }
    // 只扫挂了 GuidComponent 的实体（view<GuidComponent>），无 guid 的实体天然跳过。
    auto& reg     = world.Registry();
    auto  guidView = reg.view<GuidComponent>();
    for (auto e : guidView)
    {
        if (guidView.get<GuidComponent>(e).guid == guid)
        {
            return World::FromEntt(e);
        }
    }
    return Entity::Invalid();
}

}  // namespace Orange::Engine::Scene
