#include "pch.h"
#include "Scene.h"
#include "Components.h"
#include "Renderer/Renderer2D.h"
#include "Entity.h"

#include <box2d/box2d.h>

namespace Orange
{
    static b2WorldId mPhysicsWorldId;
    static b2BodyType Rigibody2DTypeToBox2DBody(Rigidbody2DComponent::BodyType bodytype)
    {
        switch (bodytype)
        {
            case Rigidbody2DComponent::BodyType::Static:    return b2_staticBody;
            case Rigidbody2DComponent::BodyType::Dynamic:   return b2_dynamicBody;
            case Rigidbody2DComponent::BodyType::Kinematic: return b2_kinematicBody;
            default:
        }

        ORANGE_CORE_ASSERT(false, "Unknow body type");
    }

   Scene::Scene()
   {

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

   void Scene::DestroyEntity(Entity entity)
   {
       mRegistry.destroy(entity);
   }

   void Scene::OnRuntimeStart()
   {
       // 物理世界初始化（新版）
       b2WorldDef worldDef = b2DefaultWorldDef();
       worldDef.gravity = { 0.0f, -9.8f }; // 设置重力
       mPhysicsWorldId = b2CreateWorld(&worldDef); // 返回b2WorldId

       auto view = mRegistry.view<Rigidbody2DComponent>();
       for (auto e : view)
       {
           Entity entity = { e,shared_from_this() };
           auto& transform = entity.GetComponent<TransformComponent>();
           auto& rb2d = entity.GetComponent<Rigidbody2DComponent>();

           // 配置刚体定义
           b2BodyDef bodyDef = b2DefaultBodyDef();
           bodyDef.type = Rigibody2DTypeToBox2DBody(rb2d.Type);
           bodyDef.position = { transform.Translation.x, transform.Translation.y };
           bodyDef.rotation = b2MakeRot(transform.Rotation.z);
       }
   }

   void Scene::OnRuntimeStop()
   {
       // 销毁物理世界（新版）
       b2DestroyWorld(mPhysicsWorldId);
   }

   void Scene::OnUpdateRuntime(Timestep ts)
   {
        // Update scripts

        {
            mRegistry.view<NativeScriptComponent>().each([=](auto entity, auto& nsc)
            {
                if (!nsc.Instance)
                {
                    nsc.Instance = nsc.InstantiateScript();
                    nsc.Instance->mEntity = Entity{ entity, shared_from_this()};
                    nsc.Instance->OnCreate();
                }
                nsc.Instance->OnUpdate(ts);
            });
          }
        // Render 2D
        Ref<Camera> mainCamera = nullptr;
        glm::mat4 cameraTransform;
        {
            auto view = mRegistry.view<TransformComponent, CameraComponent>();
            for (auto entity : view)
            {
                auto [transform, camera] = view.get<TransformComponent, CameraComponent>(entity);
                if(camera.Primary)
                {
                    mainCamera = CreateRef<Camera>(camera.Camera);
                    cameraTransform = transform.GetTransform();
                    break;
                }
            }  
        }

        if(mainCamera)
        {
            Renderer2D::BeginScene(mainCamera, cameraTransform);
            auto group = mRegistry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
            for (auto entity : group)
            {
                auto [transform, sprite] = group.get<TransformComponent, SpriteRendererComponent>(entity);
                Renderer2D::DrawSprite(transform.GetTransform(), sprite, int(entity));
            }
            Renderer2D::EndScene();
        }
   }

   void Scene::OnViewportResize(uint32_t width, uint32_t height)
   {
      mViewportWidth = width;
      mViewportHeight = height;

      auto view = mRegistry.view<CameraComponent>();  
      for (auto entity : view)
      {
         auto& cameraComponent = view.get<CameraComponent>(entity);
         if(!cameraComponent.FixedAspectRatio)
         {
            cameraComponent.Camera.SetViewportSize(width, height);
         }
      }
   }

   void Scene::OnUpdateEditor(Timestep ts, const Ref<EditorCamera>& camera)
   {
       Renderer2D::BeginScene(camera);

       auto group = mRegistry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
       for (auto entity : group)
       {
           auto [transform, sprite] = group.get<TransformComponent, SpriteRendererComponent>(entity);
           Renderer2D::DrawSprite(transform.GetTransform(), sprite, int(entity));
       }

       Renderer2D::EndScene();
   }

   Entity Scene::GetPrimaryCameraEntity()
   {
       auto view = mRegistry.view<CameraComponent>();
       for (auto entity : view)
       {
           const auto& cameraComponent = view.get<CameraComponent>(entity);
           if (cameraComponent.Primary)
           {
               return Entity{ entity, shared_from_this() };
           }
       }
       return {};
   }

   template<typename T>
   void Scene::OnComponentAdded(Entity entity, T& component)
   {
       static_assert(false);
   }

   template<>
   void Scene::OnComponentAdded<TransformComponent>(Entity entity, TransformComponent& component)
   {
       // Do nothing for now
   }

   template<>
   void Scene::OnComponentAdded<CameraComponent>(Entity entity, CameraComponent& component)
   {
       if (mViewportWidth > 0 && mViewportHeight > 0)
       {
            component.Camera.SetViewportSize(mViewportWidth, mViewportHeight);
       }
   }

   template<>
   void Scene::OnComponentAdded<SpriteRendererComponent>(Entity entity, SpriteRendererComponent& component)
   {
       // Do nothing for now
   }

   template<>
   void Scene::OnComponentAdded<TagComponent>(Entity entity, TagComponent& component)
   {
       // Do nothing for now
   }

   template<>
   void Scene::OnComponentAdded<NativeScriptComponent>(Entity entity, NativeScriptComponent& component)
   {
       // Do nothing for now
   }

}