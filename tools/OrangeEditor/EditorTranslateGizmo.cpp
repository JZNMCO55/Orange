#include "EditorTranslateGizmo.h"

#include "EditorCameraControl.h"
#include "EditorGizmoMath.h"
#include "command/SetFieldValueCommand.h"

#include <orange/engine/render/Camera.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <memory>
#include <string>

namespace
{

namespace GM = OrangeEditor::Internal::GizmoMath;

// 设计常数（屏幕像素 / 启发式）。Lumix 走 absolute pixel 阈值 + handle 长
// 度按相机距离自适应，这里取同款。
constexpr float kHandleScreenLengthPx = 90.0f;  // axis line 屏幕长度（像素）
constexpr float kHitThresholdPx       = 8.0f;   // 2D 点-线段距离阈值（像素）
constexpr float kArrowHeadLengthPx    = 14.0f;  // arrow head 三角高度
constexpr float kArrowHeadHalfWidthPx = 6.0f;   // arrow head 三角底半宽

// ---- 颜色 -----------------------------------------------------------------
// X / Y / Z 三色 + hovered / dragging 加亮变体。Lumix / Unity / Godot 同款
// "红绿蓝" + "黄色 highlight" 工业惯例。
constexpr ImU32 kColX        = IM_COL32(220, 60,  60,  255);
constexpr ImU32 kColXBright  = IM_COL32(255, 180, 120, 255);
constexpr ImU32 kColY        = IM_COL32(60,  200, 60,  255);
constexpr ImU32 kColYBright  = IM_COL32(180, 255, 120, 255);
constexpr ImU32 kColZ        = IM_COL32(60,  120, 240, 255);
constexpr ImU32 kColZBright  = IM_COL32(140, 200, 255, 255);

ImU32 AxisColor(EditorGizmoState::Axis axis, bool highlight) noexcept
{
    switch (axis)
    {
        case EditorGizmoState::Axis::X: return highlight ? kColXBright : kColX;
        case EditorGizmoState::Axis::Y: return highlight ? kColYBright : kColY;
        case EditorGizmoState::Axis::Z: return highlight ? kColZBright : kColZ;
        default:                        return IM_COL32(255, 255, 255, 255);
    }
}

glm::vec3 AxisDir(EditorGizmoState::Axis axis) noexcept
{
    switch (axis)
    {
        case EditorGizmoState::Axis::X: return glm::vec3(1.0f, 0.0f, 0.0f);
        case EditorGizmoState::Axis::Y: return glm::vec3(0.0f, 1.0f, 0.0f);
        case EditorGizmoState::Axis::Z: return glm::vec3(0.0f, 0.0f, 1.0f);
        default:                        return glm::vec3(0.0f);
    }
}

// ---- Transform 写回 apply lambda（捕获 EditorHost*，c14 风格弱引用解 World）
auto MakeTransformPositionApply(EditorHost* pHost, Orange::Engine::Entity entity)
{
    return [pHost, entity](const glm::vec3& value)
    {
        if (pHost == nullptr) { return; }
        auto* pWorld = pHost->scene.pWorld.get();
        if (pWorld == nullptr) { return; }  // 漏 Clear 安全降级
        auto* pTC = pWorld->GetComponent<Orange::Engine::Scene::TransformComponent>(entity);
        if (pTC == nullptr) { return; }     // Transform 被 Remove → no-op
        pTC->position = value;
    };
}

// ---- 取消 / 安全终止拖动 -------------------------------------------------
// 任何"拖动期间撞上 entity 失效 / playState 切换 / Transform 被 Remove /
// LMB 已不再 down"的情况：立即 EndGroup + 重置 state，避免 pending group
// 卡死（v0.2.5 c13 BeginGroup 嵌套约束注释明确"漏调 EndGroup 会让后续
// BeginGroup 覆盖前组" —— 这里主动闭环）。
void AbortDragIfNeeded(EditorHost& host)
{
    if (!host.gizmo.IsDragging()) { return; }
    if (host.cmdStack.InGroup()) { host.cmdStack.EndGroup(); }
    host.gizmo.draggingAxis = EditorGizmoState::Axis::None;
}

}  // anonymous namespace

bool DrawAndHandleTranslateGizmo(EditorHost& host,
                                 glm::vec2   viewportImageOriginScreen,
                                 glm::vec2   viewportImageSize,
                                 float       aspect)
{
    using Axis = EditorGizmoState::Axis;
    using Orange::Engine::Scene::TransformComponent;

    // ---- 早退：Gizmo 总开关关闭 / Play Mode / 无 World / 无选中实体 / 实体无 Transform ----
    if (!host.gizmo.visible || host.scene.playState != PlayState::Edit)
    {
        AbortDragIfNeeded(host);
        host.gizmo.hoveredAxis = Axis::None;
        return false;
    }
    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr || !host.selection.selectedEntity.IsValid())
    {
        AbortDragIfNeeded(host);
        host.gizmo.hoveredAxis = Axis::None;
        return false;
    }
    const Orange::Engine::Entity entity = host.selection.selectedEntity;
    auto* pTC = pWorld->GetComponent<TransformComponent>(entity);
    if (pTC == nullptr)
    {
        AbortDragIfNeeded(host);
        host.gizmo.hoveredAxis = Axis::None;
        return false;
    }

    // ---- camera + viewProj ----
    const auto      cam        = BuildEditorCamera(host.camera, aspect);
    const glm::mat4 viewProj   = cam.projection * cam.view;
    const glm::mat4 invViewProj = glm::inverse(viewProj);

    const glm::vec3 entityWorldPos = pTC->position;

    // ---- gizmo 屏幕尺寸自适应：选取 world-space handle 长度，使其投影到
    //      屏幕约 kHandleScreenLengthPx 像素。探针用相机右向量而非 world X
    //      轴 —— 见 GizmoMath::ComputeWorldUnitsForScreenLength 注释（防止
    //      orbit 相机时 handle 整体伸缩）。----
    const auto projOrigin = GM::ProjectWorldToScreen(entityWorldPos, viewProj,
                                                     viewportImageOriginScreen,
                                                     viewportImageSize);
    if (!projOrigin.has_value())
    {
        AbortDragIfNeeded(host);
        host.gizmo.hoveredAxis = Axis::None;
        return false;
    }
    const auto handleWorldLen = GM::ComputeWorldUnitsForScreenLength(
        entityWorldPos, cam.view, viewProj,
        viewportImageOriginScreen, viewportImageSize, kHandleScreenLengthPx);
    const float worldUnitsPerHandle = handleWorldLen.value_or(1.0f);

    // ---- 每轴端点屏幕投影（用于绘制 + 2D hit-test）----
    struct AxisProjected
    {
        Axis      axis;
        glm::vec3 dir;
        glm::vec2 tipScreen;
        bool      tipVisible;
    };
    std::array<AxisProjected, 3> axes{{
        {Axis::X, AxisDir(Axis::X), {}, false},
        {Axis::Y, AxisDir(Axis::Y), {}, false},
        {Axis::Z, AxisDir(Axis::Z), {}, false},
    }};
    for (auto& ap : axes)
    {
        const glm::vec3 tipWorld = entityWorldPos + ap.dir * worldUnitsPerHandle;
        const auto      tipProj  = GM::ProjectWorldToScreen(tipWorld, viewProj,
                                                            viewportImageOriginScreen,
                                                            viewportImageSize);
        if (tipProj.has_value())
        {
            ap.tipScreen  = tipProj->screen;
            ap.tipVisible = true;
        }
    }

    // ---- 2D hit-test（拖动期间跳过；强制 hoveredAxis = draggingAxis）-----
    const ImVec2 mousePosIm = ImGui::GetMousePos();
    const glm::vec2 mousePos(mousePosIm.x, mousePosIm.y);

    if (!host.gizmo.IsDragging())
    {
        Axis  bestAxis = Axis::None;
        float bestDist = kHitThresholdPx;
        for (const auto& ap : axes)
        {
            if (!ap.tipVisible) { continue; }
            const float d = GM::PointSegmentDistance2D(mousePos, projOrigin->screen, ap.tipScreen);
            if (d < bestDist)
            {
                bestDist = d;
                bestAxis = ap.axis;
            }
        }
        host.gizmo.hoveredAxis = bestAxis;
    }
    else
    {
        host.gizmo.hoveredAxis = host.gizmo.draggingAxis;
    }

    // ---- 输入：按下 / 拖动 / 释放 ----
    const bool imageHovered = ImGui::IsItemHovered();

    if (!host.gizmo.IsDragging()
        && imageHovered
        && host.gizmo.IsHovered()
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const auto mouseRay = GM::ScreenToWorldRay(mousePos,
                                                   viewportImageOriginScreen,
                                                   viewportImageSize,
                                                   invViewProj);
        if (mouseRay.has_value())
        {
            const glm::vec3 axisDir = AxisDir(host.gizmo.hoveredAxis);
            const auto hit = GM::ClosestPointOnAxisToRay(mouseRay->origin, mouseRay->dir,
                                                        entityWorldPos, axisDir);
            if (hit.has_value())
            {
                host.gizmo.draggingAxis       = host.gizmo.hoveredAxis;
                host.gizmo.dragStartEntityPos = entityWorldPos;
                host.gizmo.dragStartHitOnAxis = *hit;
                host.cmdStack.BeginGroup("Translate Drag", MergeMode::Ends);
            }
        }
    }

    if (host.gizmo.IsDragging())
    {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (host.cmdStack.InGroup()) { host.cmdStack.EndGroup(); }
            host.gizmo.draggingAxis = Axis::None;
        }
        else
        {
            const auto mouseRay = GM::ScreenToWorldRay(mousePos,
                                                      viewportImageOriginScreen,
                                                      viewportImageSize,
                                                      invViewProj);
            if (mouseRay.has_value())
            {
                const glm::vec3 axisDir = AxisDir(host.gizmo.draggingAxis);
                const auto hit = GM::ClosestPointOnAxisToRay(mouseRay->origin, mouseRay->dir,
                                                            host.gizmo.dragStartEntityPos,
                                                            axisDir);
                if (hit.has_value())
                {
                    const glm::vec3 delta   = *hit - host.gizmo.dragStartHitOnAxis;
                    const glm::vec3 newPos  = host.gizmo.dragStartEntityPos
                                            + axisDir * glm::dot(delta, axisDir);
                    const glm::vec3 oldPos  = pTC->position;
                    if (newPos != oldPos)
                    {
                        pTC->position = newPos;
                        // fieldKey 与 SchemaInspector 的 Transform.position
                        // 路径同字符串拼接（"Transform" + "." + "position"）；
                        // intra-group coalesce 按 fieldKey + entity 匹配。
                        host.cmdStack.Push(std::make_unique<SetFieldValueCommand<glm::vec3>>(
                            entity,
                            std::string("Transform.position"),
                            host.gizmo.dragStartEntityPos,  // oldVal 锁定到拖动起点
                            newPos,
                            MakeTransformPositionApply(&host, entity)));
                    }
                }
            }

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                if (host.cmdStack.InGroup()) { host.cmdStack.EndGroup(); }
                host.gizmo.draggingAxis = Axis::None;
            }
        }
    }

    // ---- 绘制（在 ImGui::Image 之上 overlay）----
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (drawList != nullptr)
    {
        for (const auto& ap : axes)
        {
            if (!ap.tipVisible) { continue; }
            const bool   highlight = (host.gizmo.hoveredAxis == ap.axis)
                                  || (host.gizmo.draggingAxis == ap.axis);
            const ImU32  col       = AxisColor(ap.axis, highlight);
            const float  thickness = highlight ? 5.5f : 3.5f;
            const ImVec2 a{projOrigin->screen.x, projOrigin->screen.y};
            const ImVec2 b{ap.tipScreen.x,       ap.tipScreen.y};
            drawList->AddLine(a, b, col, thickness);

            // arrow head 三角：朝向 axis 屏幕方向，回缩 kArrowHeadLengthPx
            const glm::vec2 dir2D    = ap.tipScreen - projOrigin->screen;
            const float     len2D    = glm::length(dir2D);
            if (len2D < 1e-3f) { continue; }
            const glm::vec2 dirN     = dir2D / len2D;
            const glm::vec2 perpN(-dirN.y, dirN.x);
            const glm::vec2 baseCtr  = ap.tipScreen - dirN * kArrowHeadLengthPx;
            const glm::vec2 baseL    = baseCtr + perpN * kArrowHeadHalfWidthPx;
            const glm::vec2 baseR    = baseCtr - perpN * kArrowHeadHalfWidthPx;
            drawList->AddTriangleFilled(ImVec2(ap.tipScreen.x, ap.tipScreen.y),
                                        ImVec2(baseL.x, baseL.y),
                                        ImVec2(baseR.x, baseR.y),
                                        col);
        }
    }

    return host.gizmo.IsDragging() || (imageHovered && host.gizmo.IsHovered());
}
