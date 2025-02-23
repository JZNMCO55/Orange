#ifndef ENTITY_H
#define ENTITY_H

#include "OrangeExport.h"

namespace Orange
{
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
            return mpScene.lock()->GetRegistry().emplace<T>(mEntityHandle, std::forward<Args>(args)...);
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
            ORG_CORE_ASSERT(HasComponent<T>(), "Entity does not have component!");
            mpScene.lock()->GetRegistry().remove<T>(mEntityHandle);
        }
        
        operator bool() const { return mEntityHandle != entt::null; }
        operator uint32_t() const { return (uint32_t)mEntityHandle; }
        
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