#include "SpotLightGizmoPlugin.h"

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

#include <array>
#include <cmath>
#include <optional>
#include <string_view>

namespace Orange::Editor::Plugin
{

namespace
{

namespace GM = OrangeEditor::Internal::GizmoMath;

constexpr float kApexRadiusPx     = 6.0f;
constexpr ImU32 kSpotLightColor   = IM_COL32(255, 200, 60, 255);   // 黄色，光的工业惯例色
constexpr ImU32 kConeWireColor    = IM_COL32(255, 200, 60, 110);   // 半透同色
constexpr int   kBaseRingSegments = 32;

// 投影一个世界点到屏幕；失败返回 nullopt。
std::optional<ImVec2> ProjectPt(const glm::vec3& world, const GizmoContext& ctx)
{
    const auto p = GM::ProjectWorldToScreen(world, ctx.viewProj,
                                            ctx.imageOrigin, ctx.imageSize);
    if (!p.has_value()) { return std::nullopt; }
    return ImVec2(p->screen.x, p->screen.y);
}

}  // anonymous namespace

bool SpotLightGizmoPlugin::CanHandle(
    const Orange::Editor::Schema::ComponentSchema& schema) const
{
    if (schema.typeName == nullptr) { return false; }
    return std::string_view(schema.typeName) == std::string_view("SpotLight");
}

void SpotLightGizmoPlugin::Draw(
    EditorHost&                                            host,
    Orange::Engine::Entity                                 entity,
    const Orange::Editor::Schema::ComponentSchema&         schema,
    void*                                                  component,
    const GizmoContext&                                    ctx)
{
    (void)schema;

    using SL = Orange::Engine::Render::SpotLight;
    using TC = Orange::Engine::Scene::TransformComponent;

    auto* pSL = static_cast<SL*>(component);
    if (pSL == nullptr || ctx.drawList == nullptr) { return; }

    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr) { return; }
    auto* pTC = pWorld->GetComponent<TC>(entity);
    if (pTC == nullptr) { return; }

    // 累积后 world 位置 + 锥光向（ADR-016 / A1.1 step 2，与 Pipeline 光源消费
    // 同源）；parented 聚光的锥体 gizmo 对齐世界位姿。cache 缺失退回 local。
    glm::vec3 apex = pTC->position;
    glm::vec3 dir  = Orange::Engine::Render::ComputeSpotLightWorldDir(pTC->rotation);
    if (const auto* wtc =
            pWorld->GetComponent<Orange::Engine::Scene::WorldTransformComponent>(entity))
    {
        apex = glm::vec3(wtc->world[3]);
        dir  = glm::normalize(glm::vec3(
            wtc->world *
            glm::vec4(Orange::Engine::Render::kSpotLightLocalForward, 0.0f)));
    }

    // 画 apex "灯泡" 小填充圆。
    const auto projApex = ProjectPt(apex, ctx);
    if (projApex.has_value())
    {
        ctx.drawList->AddCircleFilled(*projApex, kApexRadiusPx, kSpotLightColor);
    }

    if (pSL->range <= 0.0f) { return; }

    // 与锥轴正交的两个基向量 —— dir 接近 ±Y 时用 Z 轴避奇异，否则用 Y 轴。
    const glm::vec3 ref = (std::abs(dir.y) > 0.99f)
                              ? glm::vec3(0.0f, 0.0f, 1.0f)
                              : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 u = glm::normalize(glm::cross(ref, dir));
    const glm::vec3 v = glm::cross(dir, u);

    // base 圆心 = apex + dir × range；半径 = range × tan(outerConeAngle)。
    const glm::vec3 baseCenter = apex + dir * pSL->range;
    const float     baseRadius = pSL->range * std::tan(pSL->outerConeAngle);

    const float twoPi = 6.28318530717958647692f;

    // base ring：N 段连续 stroke，camera 后方的 sample 断开线段。
    ImVec2 prev{};
    bool   prevValid = false;
    std::array<std::optional<ImVec2>, 4> cardinals{};  // θ = 0/90/180/270 供 apex→base 射线复用
    for (int i = 0; i <= kBaseRingSegments; ++i)
    {
        const float t     = static_cast<float>(i) / static_cast<float>(kBaseRingSegments);
        const float theta = t * twoPi;
        const glm::vec3 worldPt = baseCenter +
            (std::cos(theta) * u + std::sin(theta) * v) * baseRadius;
        const auto proj = ProjectPt(worldPt, ctx);

        if (i < kBaseRingSegments)
        {
            if ((i % (kBaseRingSegments / 4)) == 0)
            {
                cardinals[static_cast<std::size_t>(i / (kBaseRingSegments / 4))] = proj;
            }
        }

        if (!proj.has_value()) { prevValid = false; continue; }
        if (prevValid)
        {
            ctx.drawList->AddLine(prev, *proj, kConeWireColor, 1.5f);
        }
        prev      = *proj;
        prevValid = true;
    }

    // 4 条 apex → base 射线（勾勒锥侧面）。
    if (projApex.has_value())
    {
        for (const auto& c : cardinals)
        {
            if (c.has_value())
            {
                ctx.drawList->AddLine(*projApex, *c, kConeWireColor, 1.5f);
            }
        }
    }
}

}  // namespace Orange::Editor::Plugin
