#include "PostProcessVolumeGizmoPlugin.h"

#include "../EditorGizmoMath.h"
#include "../EditorHost.h"
#include "../schema/ComponentSchema.h"
#include "GizmoContext.h"

#include <orange/engine/render/PostProcessComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/WorldTransformComponent.h>
#include <orange/engine/scene/World.h>

#include <imgui.h>

#include <glm/vec3.hpp>

#include <array>
#include <string_view>

namespace Orange::Editor::Plugin
{

    namespace
    {

        namespace GM = OrangeEditor::Internal::GizmoMath;

        // 内层 = localExtent 边界（weight=1 全效）：紫色实色 220 alpha + 2px 粗
        // 外层 = (localExtent + blendDistance) 边界（smoothstep 淡出末端）：同色
        // 100 alpha + 1.5px 细，与内层对照体现 "盒中心强、边界弱" 的 V2 语义
        constexpr ImU32 kVolumeInnerColor = IM_COL32(180, 130, 220, 220);
        constexpr ImU32 kVolumeOuterColor = IM_COL32(180, 130, 220, 100);

        // 画一个 axis-aligned wireframe box 的 12 条边（与 CameraFrustumGizmoPlugin
        // near/far/connecting 12 段同款模式）。corner 索引按 near (-Z) face 0..3 +
        // far (+Z) face 4..7 排序，让 connecting 边的索引对（0-4 / 1-5 / 2-6 / 3-7）
        // 与 CameraFrustum 一致便于代码读者参照。
        void DrawWireframeBox(ImDrawList*      drawList,
                              const glm::vec3& center,
                              const glm::vec3& halfExtent,
                              const glm::mat4& viewProj,
                              const glm::vec2& imageOrigin,
                              const glm::vec2& imageSize,
                              ImU32            color,
                              float            thickness)
        {
            // 8 corner local offset（与 CameraFrustum near/far face vertex 顺序一致）：
            //   0..3 = -Z face: 左下、右下、右上、左上
            //   4..7 = +Z face: 同上顺序
            const std::array<glm::vec3, 8> offsets = {{
                {-halfExtent.x, -halfExtent.y, -halfExtent.z}, // 0
                {halfExtent.x, -halfExtent.y, -halfExtent.z},  // 1
                {halfExtent.x, halfExtent.y, -halfExtent.z},   // 2
                {-halfExtent.x, halfExtent.y, -halfExtent.z},  // 3
                {-halfExtent.x, -halfExtent.y, halfExtent.z},  // 4
                {halfExtent.x, -halfExtent.y, halfExtent.z},   // 5
                {halfExtent.x, halfExtent.y, halfExtent.z},    // 6
                {-halfExtent.x, halfExtent.y, halfExtent.z},   // 7
            }};

            std::array<glm::vec2, 8> screenCorners{};
            bool                     allVisible = true;
            for (int i = 0; i < 8; ++i)
            {
                const glm::vec3 worldP = center + offsets[i];
                const auto      sp     = GM::ProjectWorldToScreen(worldP, viewProj, imageOrigin, imageSize);
                if (!sp.has_value())
                {
                    allVisible = false;
                    break;
                }
                screenCorners[i] = sp->screen;
            }
            // 任一 corner 投影失败（如在 camera 后方）则整盒跳过——避免画出错位的
            // 部分线段误导用户。与 CameraFrustumGizmoPlugin 同款 fail-safe。
            if (!allVisible)
            {
                return;
            }

            auto line = [&](int a, int b)
            {
                drawList->AddLine(
                    ImVec2(screenCorners[a].x, screenCorners[a].y),
                    ImVec2(screenCorners[b].x, screenCorners[b].y),
                    color, thickness);
            };
            // -Z face rect (0..3)
            line(0, 1);
            line(1, 2);
            line(2, 3);
            line(3, 0);
            // +Z face rect (4..7)
            line(4, 5);
            line(5, 6);
            line(6, 7);
            line(7, 4);
            // connecting edges
            line(0, 4);
            line(1, 5);
            line(2, 6);
            line(3, 7);
        }

    } // anonymous namespace

    bool PostProcessVolumeGizmoPlugin::CanHandle(
        const Orange::Editor::Schema::ComponentSchema& schema) const
    {
        if (schema.typeName == nullptr)
        {
            return false;
        }
        // typeName 与 RegisterPostProcessComponentSchema 注册时的 ComponentSchema
        // Builder<PP>("PostProcess", ...) 严格一致——不是 "PostProcessComponent"。
        return std::string_view(schema.typeName) == std::string_view("PostProcess");
    }

    void PostProcessVolumeGizmoPlugin::Draw(
        EditorHost&                                    host,
        Orange::Engine::Entity                         entity,
        const Orange::Editor::Schema::ComponentSchema& schema,
        void*                                          component,
        const GizmoContext&                            ctx)
    {
        (void)schema;

        using PP = Orange::Engine::Render::PostProcessComponent;
        using TC = Orange::Engine::Scene::TransformComponent;

        auto* pPP = static_cast<PP*>(component);
        if (pPP == nullptr || ctx.drawList == nullptr)
        {
            return;
        }

        // Mode=Global 跳过：Global volume 全局生效、无几何边界可画。仅在 Local 模
        // 式下画 wireframe box——与 Inspector 字段语义对偶（Global 时 localExtent /
        // blendDistance 在 Inspector 上仍可编辑但无视觉效果，这是 V2 设计意图：
        // 切回 Local 立即恢复 gizmo 边界）。
        if (pPP->mode != PP::Mode::Local)
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
        // box 中心取累积后 world 位置（与 Pipeline 的 PostProcess volume 消费同源，
        // ADR-016 / A1.1 step 2）；cache 缺失退回 local。
        glm::vec3 center = pTC->position;
        if (const auto* wtc =
                pWorld->GetComponent<Orange::Engine::Scene::WorldTransformComponent>(entity))
        {
            center = glm::vec3(wtc->world[3]);
        }

        // 内层 box（localExtent 边界）= V2 weight=1 区，画面 100% 受该 volume 影响
        DrawWireframeBox(ctx.drawList, center, pPP->localExtent,
                         ctx.viewProj, ctx.imageOrigin, ctx.imageSize,
                         kVolumeInnerColor, 2.0f);

        // 外层 box（localExtent + blendDistance 边界）= smoothstep 淡入末端
        // blendDistance ≤ 0 时不画外层（无淡入区，硬切换）。
        if (pPP->blendDistance > 0.0f)
        {
            const glm::vec3 outerExtent = pPP->localExtent +
                                          glm::vec3(pPP->blendDistance);
            DrawWireframeBox(ctx.drawList, center, outerExtent,
                             ctx.viewProj, ctx.imageOrigin, ctx.imageSize,
                             kVolumeOuterColor, 1.5f);
        }
    }

} // namespace Orange::Editor::Plugin
