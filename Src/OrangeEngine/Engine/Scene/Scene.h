#ifndef SCENE_H
#define SCENE_H

#include "OrangeExport.h"
#include "Timestep.h"

class b2WorldId;

namespace Orange
{
   class Entity;
   class EditorCamera;
   class ORANGE_API Scene : public std::enable_shared_from_this<Scene>
   {
   public:
      Scene();
      ~Scene();

      Entity CreateEntity(const std::string& tag = std::string());
      void DestroyEntity(Entity entity);

      void OnRuntimeStart();
      void OnRuntimeStop();

      entt::registry& GetRegistry() { return mRegistry; }

      void OnUpdateRuntime(Timestep ts);
      void OnUpdateEditor(Timestep ts, const Ref<EditorCamera>& camera);
      
      void OnViewportResize(uint32_t width, uint32_t height);

      Entity GetPrimaryCameraEntity();
   private:
       template<typename T>
       void OnComponentAdded(Entity entity, T& component);
   private:
      entt::registry mRegistry;
      uint32_t mViewportWidth = 0, mViewportHeight = 0;
      
      b2WorldId* mpPhysicsWorldID = nullptr;

      friend class Entity;
   }; 
}

#endif // SCENE_H 