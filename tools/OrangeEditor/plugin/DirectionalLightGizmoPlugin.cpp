#include "DirectionalLightGizmoPlugin.h"

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

    // entity world 位置作为箭头起点；entity 没挂 Transform 时不画——方向
    // 也由 Transform.rotation 派生，没 Transform 就没有"方向"概念可视化。
    auto* pTC = pWorld->GetComponent<TC>(entity);
    if (pTC == nullptr) { return; }

    // origin + 方向取 TransformSystem 累积的 world matrix（与 Pipeline 光源消费
    // 同一份，ADR-016 / A1.1 step 2）—— parented 灯的箭头也对齐世界位置/方向，
    // 与 shading 方向一致。cache 缺失（首帧/未渲染）退回 entity local 派生兜底。
    glm::vec3 origin = pTC->position;
    glm::vec3 dirN =
        Orange::Engine::Render::ComputeDirectionalLightWorldDir(pTC->rotation);
    if (const auto* wtc =
            pWorld->GetComponent<Orange::Engine::Scene::WorldTransformComponent>(entity))
    {
        origin = glm::vec3(wtc->world[3]);
        dirN   = glm::normalize(glm::vec3(
            wtc->world *
            glm::vec4(Orange::Engine::Render::kDirectionalLightLocalForward, 0.0f)));
    }

    // v1.0.1 c10：屏幕空间钉死箭头长度（== kHandleScreenLengthPx），与
    // entity rotation / dir 方向解耦。
    //
    // 旧路径（c10 前）：投 origin 与 origin+1*X 算 px-per-world-unit，然后
    //   tipWorld = origin + dirN * worldUnitsPerHandle。问题：perspective
    //   下"沿 +X 1 单位的 px"≠"沿 dirN 1 单位的 px"，dirN 朝相机时投影几乎
    //   为 0，箭头屏幕长度随 dir 方向波动 —— entity 旋转时 gizmo 长度肉眼
    //   可见变化。
    //
    // 新路径：projDir = project(origin + dirN)；屏幕方向 = normalize(projDir
    //   - projOrigin)；tipScreen = projOrigin + 屏幕方向 * 固定 px 长度。
    //   dir 朝/背相机时投影差近似 0 → 退化为不画箭头（提示该方向"指向相机"，
    //   后续可补一个 "⊙" / "⊗" icon 表示进/出屏，本 patch 范围外）。
    const auto projOrigin = GM::ProjectWorldToScreen(origin, ctx.viewProj,
                                                     ctx.imageOrigin, ctx.imageSize);
    if (!projOrigin.has_value()) { return; }  // entity 在相机后方

    const auto projDirEnd = GM::ProjectWorldToScreen(
        origin + dirN, ctx.viewProj, ctx.imageOrigin, ctx.imageSize);
    if (!projDirEnd.has_value()) { return; }

    const glm::vec2 dir2D = projDirEnd->screen - projOrigin->screen;
    const float     len2D = glm::length(dir2D);
    if (len2D < 1e-3f) { return; }  // dir 朝相机投影退化，不画
    const glm::vec2 dirN2D = dir2D / len2D;
    const glm::vec2 tipScreen = projOrigin->screen + dirN2D * kHandleScreenLengthPx;

    // ---- 画箭杆 + 箭头三角 ----
    const ImVec2 a{projOrigin->screen.x, projOrigin->screen.y};
    const ImVec2 b{tipScreen.x,          tipScreen.y};
    ctx.drawList->AddLine(a, b, kLightGizmoColor, 3.0f);

    const glm::vec2 perpN(-dirN2D.y, dirN2D.x);
    const glm::vec2 baseCtr = tipScreen - dirN2D * kArrowHeadLengthPx;
    const glm::vec2 baseL   = baseCtr + perpN * kArrowHeadHalfWidthPx;
    const glm::vec2 baseR   = baseCtr - perpN * kArrowHeadHalfWidthPx;
    ctx.drawList->AddTriangleFilled(ImVec2(tipScreen.x, tipScreen.y),
                                    ImVec2(baseL.x, baseL.y),
                                    ImVec2(baseR.x, baseR.y),
                                    kLightGizmoColor);
}

}  // namespace Orange::Editor::Plugin
