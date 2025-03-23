#include "pch.h"
#include "UUID.h"
#include "Scene/Scene.h"
#include "Components.h"
#include "Entity.h"

namespace Orange
{
    Entity::Entity(entt::entity handle, std::weak_ptr<Scene> scene)
        : mEntityHandle(handle), mpScene(scene)
    {
    }
    UUID Entity::GetUUID()
    {
        return GetComponent<IDComponent>().ID;
    }
    const std::string Entity::GetName()
    {
        return GetComponent<TagComponent>().Tag;
    }
}