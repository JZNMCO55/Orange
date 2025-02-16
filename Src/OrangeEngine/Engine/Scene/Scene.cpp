#include "pch.h "
#include "Scene.h"
#include "Components.h"
#include "Renderer/Renderer2D.h"

namespace Orange
{
   Scene::Scene()
   {
#ifdef ORANGE_EXAMPLE
		entt::entity entity = mRegistry.create();
		mRegistry.emplace<TransformComponent>(entity, glm::mat4(1.0f));

		mRegistry.on_construct<TransformComponent>().connect<&OnTransformConstruct>();


		if (mRegistry.has<TransformComponent>(entity))
			TransformComponent& transform = mRegistry.get<TransformComponent>(entity);


		auto view = mRegistry.view<TransformComponent>();
		for (auto entity : view)
		{
			TransformComponent& transform = view.get<TransformComponent>(entity);
		} 

		auto group = mRegistry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
		for (auto entity : group)
		{
			auto&[transform, sprite] = group.get<TransformComponent, SpriteRendererComponent>(entity);
		}
#endif
   }

   Scene::~Scene()
   {
   }

   entt::entity Scene::CreateEntity()
   {
    return mRegistry.create();
   }

   void Scene::OnUpdate(Timestep ts)
   {
    auto group = mRegistry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
    for (auto entity : group)
    {
      auto&[transform, sprite] = group.get<TransformComponent, SpriteRendererComponent>(entity);
      
      Renderer2D::DrawQuad(transform.Transform, sprite.Color);
    } 
   }
}