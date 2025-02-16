#ifndef SCENE_H
#define SCENE_H

#include "entt/entt.hpp"
#include "Timestep.h"
namespace Orange
{
   class ORANGE_API Scene
   {
   public:
      Scene();
      ~Scene();

      entt::entity CreateEntity();

      entt::registry& GetRegistry() { return mRegistry; }

      void OnUpdate(Timestep ts);

   private:
      entt::registry mRegistry;
   }; 
}

#endif // SCENE_H 