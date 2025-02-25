#ifndef SCENE_H
#define SCENE_H

#include "OrangeExport.h"
#include "Timestep.h"

namespace Orange
{
   class Entity;
   class ORANGE_API Scene : public std::enable_shared_from_this<Scene>
   {
   public:
      Scene();
      ~Scene();

      Entity CreateEntity(const std::string& tag = std::string());
      void DestroyEntity(Entity entity);

      entt::registry& GetRegistry() { return mRegistry; }

      void OnUpdate(Timestep ts);

      void OnViewportResize(uint32_t width, uint32_t height);
   private:
       template<typename T>
       void OnComponentAdded(Entity entity, T& component);
   private:
      entt::registry mRegistry;
      uint32_t mViewportWidth = 0, mViewportHeight = 0;
      friend class Entity;
   }; 
}

#endif // SCENE_H 