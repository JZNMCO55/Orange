#include "ParticleEmitterGizmoPlugin.h"

#include "../EditorGizmoMath.h"
#include "../EditorHost.h"
#include "../schema/ComponentSchema.h"
#include "GizmoContext.h"

#include <orange/engine/render/ParticleEmitterComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/scene/WorldTransformComponent.h>

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

        constexpr float kVelocityScreenLengthPx = 80.0f; // 速度箭头屏幕长度
        constexpr float kArrowHeadLengthPx      = 12.0f;
        constexpr float kArrowHeadHalfWidthPx   = 5.5f;
        constexpr ImU32 kBoxColor               = IM_COL32(80, 220, 220, 220);  // 青色 spawn box（半透 alpha 220）
        constexpr ImU32 kBoxFillColor           = IM_COL32(80, 220, 220, 32);   // 极淡 fill 提示是个区域
        constexpr ImU32 kVelocityColor          = IM_COL32(140, 250, 240, 255); // 浅青色速度箭头

    } // anonymous namespace

    bool ParticleEmitterGizmoPlugin::CanHandle(
        const Orange::Editor::Schema::ComponentSchema& schema) const
    {
        if (schema.typeName == nullptr)
        {
            return false;
        }
        return std::string_view(schema.typeName) == std::string_view("ParticleEmitter");
    }

    void ParticleEmitterGizmoPlugin::Draw(
        EditorHost&                                    host,
        Orange::Engine::Entity                         entity,
        const Orange::Editor::Schema::ComponentSchema& schema,
        void*                                          component,
        const GizmoContext&                            ctx)
    {
        (void)schema;

        using PEC = Orange::Engine::Render::ParticleEmitterComponent;
        using TC  = Orange::Engine::Scene::TransformComponent;

        auto* pPE = static_cast<PEC*>(component);
        if (pPE == nullptr || ctx.drawList == nullptr)
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
        // spawn box 中心取累积后 world 位置（ADR-016 / A1.1 step 2）；emitter parent
        // 到移动物体时 spawn box 对齐世界位置。cache 缺失退回 local。
        glm::vec3 origin = pTC->position;
        if (const auto* wtc =
                pWorld->GetComponent<Orange::Engine::Scene::WorldTransformComponent>(entity))
        {
            origin = glm::vec3(wtc->world[3]);
        }

        // ---- 1. Spawn box（entity world XY 平面内 4 角）----
        // VfxSystem 当前 spawn 时仅 entity.position.xy + offset（component 注释
        // "z 维度暂不参与 sim"），所以 spawn box 也按 XY 平面绘制——entity
        // rotation 当前不参与，box 边线始终轴对齐世界 XY。
        const glm::vec2& mn              = pPE->desc.spawnOffsetMin;
        const glm::vec2& mx              = pPE->desc.spawnOffsetMax;
        const glm::vec3  cornersWorld[4] = {
            origin + glm::vec3(mn.x, mn.y, 0.0f),
            origin + glm::vec3(mx.x, mn.y, 0.0f),
            origin + glm::vec3(mx.x, mx.y, 0.0f),
            origin + glm::vec3(mn.x, mx.y, 0.0f),
        };
        glm::vec2 cornersScreen[4];
        bool      allCornersVisible = true;
        for (int i = 0; i < 4; ++i)
        {
            const auto p = GM::ProjectWorldToScreen(cornersWorld[i], ctx.viewProj,
                                                    ctx.imageOrigin, ctx.imageSize);
            if (!p.has_value())
            {
                allCornersVisible = false;
                break;
            }
            cornersScreen[i] = p->screen;
        }
        // spawn box 退化（min == max）= 点状 emitter，跳过 box 绘制（避免画一
        // 个 0 面积的菱形看起来像 bug）。
        const bool boxDegenerate = (std::abs(mx.x - mn.x) < 1e-5f && std::abs(mx.y - mn.y) < 1e-5f);
        if (allCornersVisible && !boxDegenerate)
        {
            // 极淡 fill 提示 spawn 区域（quad），按 corner 顺序填充
            ctx.drawList->AddQuadFilled(
                ImVec2(cornersScreen[0].x, cornersScreen[0].y),
                ImVec2(cornersScreen[1].x, cornersScreen[1].y),
                ImVec2(cornersScreen[2].x, cornersScreen[2].y),
                ImVec2(cornersScreen[3].x, cornersScreen[3].y),
                kBoxFillColor);
            // 4 段边线（顺时针）
            for (int i = 0; i < 4; ++i)
            {
                const int j = (i + 1) % 4;
                ctx.drawList->AddLine(
                    ImVec2(cornersScreen[i].x, cornersScreen[i].y),
                    ImVec2(cornersScreen[j].x, cornersScreen[j].y),
                    kBoxColor, 1.8f);
            }
        }

        // ---- 2. Initial velocity 平均向量箭头（XY 平面内）----
        const glm::vec2 avgVel = (pPE->desc.initialVelocityMin + pPE->desc.initialVelocityMax) * 0.5f;
        const float     vLen   = glm::length(avgVel);
        if (vLen > 1e-4f)
        {
            const glm::vec2 dirN2 = avgVel / vLen;
            const glm::vec3 dir3  = glm::vec3(dirN2.x, dirN2.y, 0.0f);

            // v1.0.1 c10：屏幕空间钉死箭头长度（详见 DirectionalLightGizmoPlugin
            // 同节注释）。投 origin + dir3 取屏幕方向，长度 = kVelocityScreenLengthPx
            // 像素固定，与 entity rotation / dir 方向解耦。
            const auto projOrigin = GM::ProjectWorldToScreen(origin, ctx.viewProj,
                                                             ctx.imageOrigin, ctx.imageSize);
            if (!projOrigin.has_value())
            {
                return;
            }
            const auto projDirEnd = GM::ProjectWorldToScreen(
                origin + dir3, ctx.viewProj, ctx.imageOrigin, ctx.imageSize);
            if (!projDirEnd.has_value())
            {
                return;
            }

            const glm::vec2 dir2D = projDirEnd->screen - projOrigin->screen;
            const float     len2D = glm::length(dir2D);
            if (len2D < 1e-3f)
            {
                return;
            } // dir 投影退化（朝相机），不画
            const glm::vec2 dirN2D    = dir2D / len2D;
            const glm::vec2 tipScreen = projOrigin->screen + dirN2D * kVelocityScreenLengthPx;

            const ImVec2 a{projOrigin->screen.x, projOrigin->screen.y};
            const ImVec2 b{tipScreen.x, tipScreen.y};
            ctx.drawList->AddLine(a, b, kVelocityColor, 2.5f);

            const glm::vec2 perpN(-dirN2D.y, dirN2D.x);
            const glm::vec2 baseCtr = tipScreen - dirN2D * kArrowHeadLengthPx;
            const glm::vec2 baseL   = baseCtr + perpN * kArrowHeadHalfWidthPx;
            const glm::vec2 baseR   = baseCtr - perpN * kArrowHeadHalfWidthPx;
            ctx.drawList->AddTriangleFilled(ImVec2(tipScreen.x, tipScreen.y),
                                            ImVec2(baseL.x, baseL.y),
                                            ImVec2(baseR.x, baseR.y),
                                            kVelocityColor);
        }
    }

} // namespace Orange::Editor::Plugin
