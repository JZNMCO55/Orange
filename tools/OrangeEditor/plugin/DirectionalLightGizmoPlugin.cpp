#include "DirectionalLightGizmoPlugin.h"

#include "../EditorGizmoMath.h"
#include "../EditorHost.h"
#include "../schema/ComponentSchema.h"
#include "GizmoContext.h"

#include <orange/engine/render/LightComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <imgui.h>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <string_view>

namespace Orange::Editor::Plugin
{

namespace
{

namespace GM = OrangeEditor::Internal::GizmoMath;

constexpr float kHandleScreenLengthPx  = 100.0f;  // 箭头屏幕长度（略长于 Translate gizmo，凸显方向感）
constexpr float kArrowHeadLengthPx     = 16.0f;
constexpr float kArrowHeadHalfWidthPx  = 7.0f;
constexpr ImU32 kLightGizmoColor       = IM_COL32(250, 230, 80, 255);  // 黄色——光的工业惯例色

}  // anonymous namespace

bool DirectionalLightGizmoPlugin::CanHandle(
    const Orange::Editor::Schema::ComponentSchema& schema) const
{
    if (schema.typeName == nullptr) { return false; }
    return std::string_view(schema.typeName) == std::string_view("DirectionalLight");
}

void DirectionalLightGizmoPlugin::Draw(
    EditorHost&                                            host,
    Orange::Engine::Entity                                 entity,
    const Orange::Editor::Schema::ComponentSchema&         schema,
    void*                                                  component,
    const GizmoContext&                                    ctx)
{
    (void)schema;

    using DL = Orange::Engine::Render::DirectionalLight;
    using TC = Orange::Engine::Scene::TransformComponent;

    auto* pDL = static_cast<DL*>(component);
    if (pDL == nullptr || ctx.drawList == nullptr) { return; }

    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr) { return; }

    // entity world 位置作为箭头起点；entity 没挂 Transform 时退到原点（不画
    // 没意义的远端箭头）。
    auto* pTC = pWorld->GetComponent<TC>(entity);
    if (pTC == nullptr) { return; }
    const glm::vec3 origin = pTC->position;

    // direction 约定为光的传播方向（与 LightComponent.h 注释一致）。规范化
    // 防御零向量 / 已 normalize 仍除以 length 是廉价稳健操作。
    const float dirLen = glm::length(pDL->direction);
    if (dirLen < 1e-5f) { return; }  // 退化方向不画
    const glm::vec3 dirN = pDL->direction / dirLen;

    // 屏幕长度自适应：投影 origin 与 origin+1*X 的屏幕距离换算 world-units-
    // per-handle（与 EditorTranslateGizmo 同款 heuristic）。
    const auto projOrigin = GM::ProjectWorldToScreen(origin, ctx.viewProj,
                                                     ctx.imageOrigin, ctx.imageSize);
    if (!projOrigin.has_value()) { return; }  // entity 在相机后方
    const auto projOriginPlusX = GM::ProjectWorldToScreen(
        origin + glm::vec3(1.0f, 0.0f, 0.0f),
        ctx.viewProj, ctx.imageOrigin, ctx.imageSize);
    float worldUnitsPerHandle = 1.0f;
    if (projOriginPlusX.has_value())
    {
        const float pxPerUnit = glm::length(projOriginPlusX->screen - projOrigin->screen);
        if (pxPerUnit > 1e-3f)
        {
            worldUnitsPerHandle = kHandleScreenLengthPx / pxPerUnit;
        }
    }
    const glm::vec3 tipWorld = origin + dirN * worldUnitsPerHandle;
    const auto      projTip  = GM::ProjectWorldToScreen(tipWorld, ctx.viewProj,
                                                        ctx.imageOrigin, ctx.imageSize);
    if (!projTip.has_value()) { return; }  // tip 跑到相机后方

    // ---- 画箭杆 + 箭头三角 ----
    const ImVec2 a{projOrigin->screen.x, projOrigin->screen.y};
    const ImVec2 b{projTip->screen.x,    projTip->screen.y};
    ctx.drawList->AddLine(a, b, kLightGizmoColor, 3.0f);

    const glm::vec2 dir2D = projTip->screen - projOrigin->screen;
    const float     len2D = glm::length(dir2D);
    if (len2D < 1e-3f) { return; }
    const glm::vec2 dirN2D(dir2D.x / len2D, dir2D.y / len2D);
    const glm::vec2 perpN(-dirN2D.y, dirN2D.x);
    const glm::vec2 baseCtr = projTip->screen - dirN2D * kArrowHeadLengthPx;
    const glm::vec2 baseL   = baseCtr + perpN * kArrowHeadHalfWidthPx;
    const glm::vec2 baseR   = baseCtr - perpN * kArrowHeadHalfWidthPx;
    ctx.drawList->AddTriangleFilled(ImVec2(projTip->screen.x, projTip->screen.y),
                                    ImVec2(baseL.x, baseL.y),
                                    ImVec2(baseR.x, baseR.y),
                                    kLightGizmoColor);
}

}  // namespace Orange::Editor::Plugin
