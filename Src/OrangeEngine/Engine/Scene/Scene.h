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

      entt::registry& GetRegistry() { return mRegistry; }

      void OnUpdate(Timestep ts);

      void OnViewportResize(uint32_t width, uint32_t height);
   private:
      entt::registry mRegistry;
      uint32_t mViewportWidth = 0, mViewportHeight = 0;
   }; 
}

#endif // SCENE_H 