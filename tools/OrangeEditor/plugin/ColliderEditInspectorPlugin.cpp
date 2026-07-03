#include "ColliderEditInspectorPlugin.h"

#include "../EditorHost.h"
#include "../schema/ComponentSchema.h"

#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/ColliderDesc.h>

#include <imgui.h>

#include <string_view>
#include <variant>

namespace Orange::Editor::Plugin
{

    bool ColliderEditInspectorPlugin::CanHandle(
        const Orange::Editor::Schema::ComponentSchema& schema) const
    {
        if (schema.typeName == nullptr)
        {
            return false;
        }
        return std::string_view(schema.typeName) == std::string_view("Collider");
    }

    void ColliderEditInspectorPlugin::ParseEnd(
        EditorHost&                                    host,
        Orange::Engine::Entity                         entity,
        const Orange::Editor::Schema::ComponentSchema& schema,
        void*                                          component)
    {
        (void)schema;

        using CC  = Orange::Engine::Physics::ColliderComponent;
        auto* pCc = static_cast<CC*>(component);
        if (pCc == nullptr)
        {
            return;
        }

        const bool isPolygon =
            std::holds_alternative<Orange::Engine::Physics::PolygonDesc>(pCc->shape);
        const bool isEdge =
            std::holds_alternative<Orange::Engine::Physics::EdgeChainDesc>(pCc->shape);

        ImGui::Spacing();
        ImGui::Separator();

        if (!isPolygon && !isEdge)
        {
            // Circle / Box 无可拖拽顶点表 —— 用 Shape Type Combo 切到 Polygon /
            // Edge Chain 后本入口才有意义。
            ImGui::TextDisabled("Vertex editing: Polygon / Edge Chain only");
            return;
        }

        auto&      cs          = host.colliderEdit;
        const bool editingThis = cs.active && cs.entity == entity;

        if (editingThis)
        {
            if (ImGui::Button("Exit Vertex Edit##collider_edit"))
            {
                cs.Reset();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(LMB drag · click empty=add · double-click=remove · Esc)");
        }
        else
        {
            if (ImGui::Button("Edit Vertices in Viewport##collider_edit"))
            {
                // 进入子模式：锁定当前 entity。Reset 先清残留拖拽状态再激活。
                cs.Reset();
                cs.active = true;
                cs.entity = entity;
            }
        }
    }

} // namespace Orange::Editor::Plugin
