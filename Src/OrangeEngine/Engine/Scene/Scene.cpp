#include "pch.h"
#include "Scene.h"
#include "Components.h"
#include "Renderer/Renderer2D.h"
#include "Entity.h"

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

   Entity Scene::CreateEntity(const std::string& tag)
   {
        Entity entity = { mRegistry.create(), shared_from_this() };
        entity.AddComponent<TransformComponent>();
		    auto& tagComponent = entity.AddComponent<TagComponent>();
		    tagComponent.Tag = tag.empty() ? "Entity" : tag;
        return entity;
   }

   void Scene::OnUpdate(Timestep ts)
   {
        // Update scripts
        auto group = mRegistry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
        for (auto entity : group)
        {
          auto&[transform, sprite] = group.get<TransformComponent, SpriteRendererComponent>(entity);
          
          Renderer2D::DrawQuad(transform.Transform, sprite.Color);
        } 
   }
}