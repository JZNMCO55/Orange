#include "Core/Log.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "SceneHierachyPanel.h"
#include "imgui/imgui.h"

namespace Orange
{
    SceneHierachyPanel::SceneHierachyPanel(const Ref<Scene>& scene)
    {
        SetContext(scene);
    }

    SceneHierachyPanel::~SceneHierachyPanel()
    {
    }

    void SceneHierachyPanel::SetContext(const Ref<Scene>& scene)
    {
        mpContext = scene;
    }

    void SceneHierachyPanel::OnImGuiRender()
    {
        ImGui::Begin("Scene Hierachy");

        mpContext->GetRegistry().view<entt::entity>().each([&](auto entityID)
            {
                Entity entity = { entityID, mpContext };
                DrawEntityNode(entity);
            });

        if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
        {
            mSelectionContext = {};
        }

        ImGui::End();

        ImGui::Begin("Properties");

        if (mSelectionContext)
        {
            DrawComponents(mSelectionContext);
        }

        ImGui::End();
    }

    void SceneHierachyPanel::DrawEntityNode(Entity entity)
    {
        auto& tag = entity.GetComponent<TagComponent>().Tag;

        ImGuiTreeNodeFlags flags = ((mSelectionContext == entity) ? 
            ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
        bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, tag.c_str());

        if (ImGui::IsItemClicked())
        {
            mSelectionContext = entity;
        }

        if (opened)
        {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;;
            bool bOpend = ImGui::TreeNodeEx((void*)9817239, flags, tag.c_str());
            if (opened)
                ImGui::TreePop();
            ImGui::TreePop();
        }
    }

    void SceneHierachyPanel::DrawComponents(Entity entity)
    {
        if (entity.HasComponent<TransformComponent>())
        {
            auto& tag = entity.GetComponent<TagComponent>().Tag;

            char buffer[256];
            memset(buffer, 0, sizeof(buffer));
            strcpy_s(buffer, tag.c_str());
            if (ImGui::InputText("Tag", buffer, sizeof(buffer)))
            {
                tag = std::string(buffer);
            }
        }

        if (entity.HasComponent<TransformComponent>())
        {
            if (ImGui::TreeNodeEx((void*)typeid(TransformComponent).hash_code()
                , ImGuiTreeNodeFlags_DefaultOpen, "Transform"))
            {
                auto& transform = entity.GetComponent<TransformComponent>().Transform;
                ImGui::DragFloat3("Postion", glm::value_ptr(transform[3]), 0.1f);

                ImGui::TreePop();
            }
        }

        if (entity.HasComponent<CameraComponent>())
        {
            if (ImGui::TreeNodeEx((void*)typeid(CameraComponent).hash_code()
                , ImGuiTreeNodeFlags_DefaultOpen, "Camera"))
            {
                auto& cameraComponent = entity.GetComponent<CameraComponent>();
                auto& camera = cameraComponent.Camera;
                
                ImGui::Checkbox("Primary", &cameraComponent.Primary);

                const char* projectionTypeStrings[] = { "Perspective", "Orthographic" };
                const char* currentProjectionTypeString = projectionTypeStrings[(int)camera.GetProjectionType()];
                if (ImGui::BeginCombo("Projection", currentProjectionTypeString))
                {
                    for (int i = 0; i < 2; i++)
                    {
                        bool isSelected = currentProjectionTypeString == projectionTypeStrings[i];
                        if (ImGui::Selectable(projectionTypeStrings[i], isSelected))
                        {
                            currentProjectionTypeString = projectionTypeStrings[i];
                            camera.SetProjectionType((SceneCamera::ProjectionType)i);
                        }
                        if (isSelected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                if (camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective)
                {
                    float verticalFov = glm::degrees(camera.GetPerspectiveFOV());
                    if (ImGui::DragFloat("Vertical FOV", &verticalFov))
                    {
                        camera.SetPerspectiveFOV(glm::radians(verticalFov));
                    }

                    float perspectiveNear = camera.GetPerspectiveNear();
                    if (ImGui::DragFloat("Near", &perspectiveNear))
                    {
                        camera.SetPerspectiveNear(perspectiveNear);
                    }

                    float perspectiveFar = camera.GetPerspectiveFar();
                    if (ImGui::DragFloat("Far", &perspectiveFar))
                    {
                        camera.SetPerspectiveFar(perspectiveFar);
                    }
                }
                else if (camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic)
                {
                    float orthoSize = camera.GetOrthographicSize();
                    if (ImGui::DragFloat("Size", &orthoSize))
                    {
                        camera.SetOrthographicSize(orthoSize);
                    }

                    float orthoNear = camera.GetOrthographicNear();
                    if (ImGui::DragFloat("Near", &orthoNear))
                    {
                        camera.SetOrthographicNear(orthoNear);
                    }

                    float orthoFar = camera.GetOrthographicFar();
                    if (ImGui::DragFloat("Far", &orthoFar))
                    {
                        camera.SetOrthographicFar(orthoFar);
                    }

                    ImGui::Checkbox("Fixed Aspect Ratio", &cameraComponent.FixedAspectRatio);
                }
            }
            ImGui::TreePop();
        }

        if (entity.HasComponent<SpriteRendererComponent>())
        {
            if (ImGui::TreeNodeEx((void*)typeid(SpriteRendererComponent).hash_code()
                , ImGuiTreeNodeFlags_DefaultOpen, "Sprite Renderer"))
            {
                auto& spriteRenderer = entity.GetComponent<SpriteRendererComponent>();
                ImGui::ColorEdit4("Color", glm::value_ptr(spriteRenderer.Color));
                ImGui::TreePop();
            }
        }
    }
}