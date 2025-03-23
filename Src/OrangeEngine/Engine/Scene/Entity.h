#ifndef ENTITY_H
#define ENTITY_H

#include "OrangeExport.h"
namespace Orange
{
    class UUID;
    class Scene;
    class ORANGE_API Entity
    {
    public:
        Entity() = default;
        Entity(entt::entity handle, std::weak_ptr<Scene> scene);
        Entity(const Entity& other) = default;
    
        template<typename T, typename... Args>
        T& AddComponent(Args&&... args)
        {
            ORANGE_CORE_ASSERT(!HasComponent<T>(), "Entity already has component!");
            T& component = mpScene.lock()->GetRegistry().emplace<T>(mEntityHandle, std::forward<Args>(args)...);
            mpScene.lock()->OnComponentAdded<T>(*this, component);

            return component;
        }

        template<typename T>
        T& GetComponent()
        {
            ORANGE_CORE_ASSERT(HasComponent<T>(), "Entity does not have component!");
            return mpScene.lock()->GetRegistry().get<T>(mEntityHandle);
        }

        template<typename T>
        bool HasComponent()
        {
            return mpScene.lock()->GetRegistry().all_of<T>(mEntityHandle);
        }

        template<typename T>
        void RemoveComponent()
        {
            ORANGE_CORE_ASSERT(HasComponent<T>(), "Entity does not have component!");
            mpScene.lock()->GetRegistry().remove<T>(mEntityHandle);
        }
        
        template<typename T, typename... Args>
        T& AddOrgReplaceComponenet(Args&&... args)
        {
            T& component = mpScene.lock()->GetRegistry().replace<T>(mEntityHandle, std::forward<Args>(args)...);
            mpScene.lock()->OnComponentAdded<T>(*this, component);
            return component;
        }

        operator bool() const { return mEntityHandle != entt::null; }

        operator entt::entity() const { return mEntityHandle; };

        operator uint32_t() const { return (uint32_t)mEntityHandle; }
        
        UUID GetUUID();

        const std::string GetName();

        bool operator==(const Entity& other) const
        {
            return mEntityHandle == other.mEntityHandle && mpScene.lock() == other.mpScene.lock(); 
        }

        bool operator!=(const Entity& other) const
        {
            return !(*this == other);
        }
        
    private:
        entt::entity mEntityHandle = entt::null;
        std::weak_ptr<Scene> mpScene;
    };
}
#endif // ENTITY_H