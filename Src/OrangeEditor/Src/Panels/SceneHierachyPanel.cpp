#include "Core/Log.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "SceneHierachyPanel.h"
#include <imgui/imgui_internal.h>
#include <imgui/imgui.h>

namespace Orange
{
    static void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.0f)
    {
        ImGuiIO& io = ImGui::GetIO();
        auto boldFont = io.Fonts->Fonts[1];

        ImGui::PushID(label.c_str());

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);
        ImGui::Text(label.c_str());
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });

        float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
        ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.9f, 0.2f, 0.2f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
        ImGui::PushFont(boldFont);
        if (ImGui::Button("X", buttonSize))
        {
            values.x = resetValue;
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.3f, 0.8f, 0.3f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
        ImGui::PushFont(boldFont);
        if (ImGui::Button("Y", buttonSize))
        {
            values.y = resetValue;
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.35f, 0.9f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
        ImGui::PushFont(boldFont);
        if (ImGui::Button("Z", buttonSize))
        {
            values.z = resetValue;
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();

        ImGui::PopStyleVar();

        ImGui::Columns(1);

        ImGui::PopID();
    }

    template<typename T, typename UIFunction>
    static void DrawComponent(const std::string& name, Entity entity, UIFunction uiFunction)
    {
        const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;
        if (entity.HasComponent<T>())
        {
            auto& component = entity.GetComponent<T>();
            ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
            float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
            ImGui::Separator();
            bool open = ImGui::TreeNodeEx((void*)typeid(T).hash_code(), treeNodeFlags, name.c_str());
            ImGui::PopStyleVar(
            );
            ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.5f);
            if (ImGui::Button("+", ImVec2{ lineHeight, lineHeight }))
            {
                ImGui::OpenPopup("ComponentSettings");
            }

            bool removeComponent = false;
            if (ImGui::BeginPopup("ComponentSettings"))
            {
                if (ImGui::MenuItem("Remove component"))
                    removeComponent = true;

                ImGui::EndPopup();
            }

            if (open)
            {
                uiFunction(component);
                ImGui::TreePop();
            }

            if (removeComponent)
                entity.RemoveComponent<T>();
        }
    }
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

        if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Create Entity"))
            {
                mpContext->CreateEntity("Empty Entity");
            }

            ImGui::EndPopup();
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
        
        bool bEntityDeleted = false;
        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Delete Entity"))
            {
                bEntityDeleted = true;
            }

            ImGui::EndPopup();
        }

        if (opened)
        {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;;
            bool bOpend = ImGui::TreeNodeEx((void*)9817239, flags, tag.c_str());
            if (bOpend)
                ImGui::TreePop();
            ImGui::TreePop();
        }

        if (bEntityDeleted)
        {
            mpContext->DestroyEntity(entity);
            if (mSelectionContext == entity)
            {
                mSelectionContext = {};
            }
        }
    }

    void SceneHierachyPanel::DrawComponents(Entity entity)
    {
        if (entity.HasComponent<TagComponent>())
        {
            auto& tag = entity.GetComponent<TagComponent>().Tag;

            char buffer[256];
            memset(buffer, 0, sizeof(buffer));
            strcpy_s(buffer, tag.c_str());
            if (ImGui::InputText("##Tag", buffer, sizeof(buffer)))
            {
                tag = std::string(buffer);
            }
        }

        ImGui::SameLine();
        ImGui::PushItemWidth(-1);

        // Todo : Move to panel end
        if (ImGui::Button("Add Component"))
        {
            ImGui::OpenPopup("AddComponent");
        }

        if (ImGui::BeginPopup("AddComponent"))
        {
            if (ImGui::MenuItem("Camera"))
            {
                mSelectionContext.AddComponent<CameraComponent>();
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Sprite Renderer"))
            {
                mSelectionContext.AddComponent<SpriteRendererComponent>();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::PopItemWidth();

        // Transform Component
        DrawComponent<TransformComponent>("Transform", entity, [](auto& transformComponent)
            {
                DrawVec3Control("Position", transformComponent.Translation);
                glm::vec3 rotation = glm::degrees(transformComponent.Rotation);
                DrawVec3Control("Rotation", rotation);
                transformComponent.Rotation = glm::radians(rotation);
                DrawVec3Control("Scale", transformComponent.Scale, 1.0f);
            });

        // Camera Component
        DrawComponent<CameraComponent>("Camera", entity, [](auto& cameraComponent)
            {
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
            });

        // Sprite Renderer Component
        DrawComponent<SpriteRendererComponent>("Sprite Renderer", entity, [](auto& spriteRendererComponent)
            {
                ImGui::ColorEdit4("Color", glm::value_ptr(spriteRendererComponent.Color));
            });
        
        
    }
}