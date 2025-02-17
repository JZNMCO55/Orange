#include "pch.h"
#include "Entity.h"

namespace Orange
{
    Entity::Entity(entt::entity handle, std::weak_ptr<Scene> scene)
        : mEntityHandle(handle), mpScene(scene)
    {
    }
}