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

   private:
      entt::registry mRegistry;
   }; 
}

#endif // SCENE_H 