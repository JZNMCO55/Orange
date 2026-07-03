#include "PointLightGizmoPlugin.h"

#include "../EditorGizmoMath.h"
#include "../EditorHost.h"
#include "../schema/ComponentSchema.h"
#include "GizmoContext.h"

#include <orange/engine/render/LightComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/WorldTransformComponent.h>
#include <orange/engine/scene/World.h>

#include <imgui.h>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <string_view>

namespace Orange::Editor::Plugin
{

    namespace
    {

        namespace GM = OrangeEditor::Internal::GizmoMath;

        constexpr float kIconRadiusPx      = 6.0f;
        constexpr ImU32 kPointLightColor   = IM_COL32(255, 200, 60, 255); // 黄色，光的工业惯例色
        constexpr ImU32 kRangeRingColor    = IM_COL32(255, 200, 60, 100); // 半透同色
        constexpr int   kRangeRingSegments = 36;

    } // anonymous namespace

    bool PointLightGizmoPlugin::CanHandle(
        const Orange::Editor::Schema::ComponentSchema& schema) const
    {
        if (schema.typeName == nullptr)
        {
            return false;
        }
        return std::string_view(schema.typeName) == std::string_view("PointLight");
    }

    void PointLightGizmoPlugin::Draw(
        EditorHost&                                    host,
        Orange::Engine::Entity                         entity,
        const Orange::Editor::Schema::ComponentSchema& schema,
        void*                                          component,
        const GizmoContext&                            ctx)
    {
        (void)schema;

        using PL = Orange::Engine::Render::PointLight;
        using TC = Orange::Engine::Scene::TransformComponent;

        auto* pPL = static_cast<PL*>(component);
        if (pPL == nullptr || ctx.drawList == nullptr)
        {
            return;
        }

        auto* pWorld = host.scene.pWorld.get();
        if (pWorld == nullptr)
        {
            return;
        }
        auto* pTC = pWorld->GetComponent<TC>(entity);
        if (pTC == nullptr)
        {
            return;
        }
        // 累积后 world 位置（ADR-016 / A1.1 step 2，与 Pipeline 光源消费同源）；
        // parented 点光的 gizmo 圆环对齐世界位置。cache 缺失退回 local。
        glm::vec3 origin = pTC->position;
        if (const auto* wtc =
                pWorld->GetComponent<Orange::Engine::Scene::WorldTransformComponent>(entity))
        {
            origin = glm::vec3(wtc->world[3]);
        }

        // 投影 entity 中心 + range 边界（沿 +X / +Y / +Z 三轴各取一点 → 在屏幕
        // 上画一个简化的 "range 球轮廓" 用三段近似 ellipse arc 可能过重；改用
        // 36 段近似圆环：在世界 XZ 平面取 36 个 sample point，投到屏幕画 line
        // strip。XZ 平面是地平面（多数 2.5D / 3D 场景的"水平面"），与 grid 一
        // 致），让用户能看到光"在地面上覆盖多远"——比 wire sphere 视觉负担小、
        // 屏幕拥挤度低。
        const auto projOrigin = GM::ProjectWorldToScreen(origin, ctx.viewProj,
                                                         ctx.imageOrigin, ctx.imageSize);
        if (!projOrigin.has_value())
        {
            return;
        }

        // 画中心 "灯泡" 小填充圆
        ctx.drawList->AddCircleFilled(
            ImVec2(projOrigin->screen.x, projOrigin->screen.y),
            kIconRadiusPx, kPointLightColor);

        // range <= 0 时不画环（无影响范围）
        if (pPL->range <= 0.0f)
        {
            return;
        }

        // XZ 平面上 36 sample 形成 range 圆环 —— 每个 sample 单独投影；camera
        // 后方的 sample 跳过对应线段。简化：用 ImGui PathLineTo 连续 stroke，
        // 单一颜色 100 alpha 不夺主视线。
        const float twoPi = 6.28318530717958647692f;
        ImVec2      prev{};
        bool        prevValid = false;
        for (int i = 0; i <= kRangeRingSegments; ++i)
        {
            const float     t       = static_cast<float>(i) / static_cast<float>(kRangeRingSegments);
            const float     theta   = t * twoPi;
            const glm::vec3 worldPt = origin +
                                      glm::vec3(std::cos(theta) * pPL->range, 0.0f, std::sin(theta) * pPL->range);
            const auto proj = GM::ProjectWorldToScreen(worldPt, ctx.viewProj,
                                                       ctx.imageOrigin, ctx.imageSize);
            if (!proj.has_value())
            {
                prevValid = false;
                continue;
            }
            const ImVec2 cur{proj->screen.x, proj->screen.y};
            if (prevValid)
            {
                ctx.drawList->AddLine(prev, cur, kRangeRingColor, 1.5f);
            }
            prev      = cur;
            prevValid = true;
        }
    }

} // namespace Orange::Editor::Plugin
