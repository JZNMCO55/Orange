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
}