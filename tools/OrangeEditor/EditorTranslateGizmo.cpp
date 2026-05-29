#include "EditorTranslateGizmo.h"

#include "EditorCameraControl.h"
#include "EditorGizmoMath.h"
#include "EditorMathUtil.h"  // Util::SnapToStep（gizmo 网格 snap）
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
// **v0.8 起 handleScreenLength / hitThreshold / 线宽 / 配色从 EditorSettings
// 读取**（消除 L13）；arrowHead 尺寸暂未纳入 settings，保持 hardcode。
constexpr float kArrowHeadLengthPx    = 14.0f;  // arrow head 三角高度
constexpr float kArrowHeadHalfWidthPx = 6.0f;   // arrow head 三角底半宽

// 从 settings 的 glm::vec4 配色（linear 0..1 RGBA）转 ImGui 32-bit RGBA。
// clamp 防越界后乘 255。
ImU32 SettingsToImU32(const glm::vec4& c) noexcept
{
    auto byteOf = [](float v) -> int
    {
        if (v < 0.0f) { v = 0.0f; }
        if (v > 1.0f) { v = 1.0f; }
        return static_cast<int>(v * 255.0f + 0.5f);
    };
    return IM_COL32(byteOf(c.r), byteOf(c.g), byteOf(c.b), byteOf(c.a));
}

ImU32 SettingsAxisColor(const EditorSettings& s,
                        EditorGizmoState::Axis axis,
                        bool highlight) noexcept
{
    switch (axis)
    {
        case EditorGizmoState::Axis::X:
            return SettingsToImU32(highlight ? s.gizmoColorXHighlight : s.gizmoColorXIdle);
        case EditorGizmoState::Axis::Y:
            return SettingsToImU32(highlight ? s.gizmoColorYHighlight : s.gizmoColorYIdle);
        case EditorGizmoState::Axis::Z:
            return SettingsToImU32(highlight ? s.gizmoColorZHighlight : s.gizmoColorZIdle);
        default:
            return IM_COL32(255, 255, 255, 255);
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
    //      屏幕约 host.settings.gizmoHandleScreenLengthPx 像素。探针用相机
    //      右向量而非 world X 轴 —— 见 GizmoMath::ComputeWorldUnitsForScreen
    //      Length 注释（防止 orbit 相机时 handle 整体伸缩）。----
    const float handleScreenLengthPx = host.settings.gizmoHandleScreenLengthPx;
    const float hitThresholdPx       = host.settings.gizmoHitThresholdPx;
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
        viewportImageOriginScreen, viewportImageSize, handleScreenLengthPx);
    const float worldUnitsPerHandle = handleWorldLen.value_or(1.0f);

    // ---- 每轴端点屏幕投影（用于绘制 + 2D hit-test）----
    struct AxisProjected
    {
        Axis      axis;
        glm::vec3 dir;
        glm::vec2 tipScreen;
        bool      tipVisible;
    };
    // space-aware 轴向（gap 报告 §3 P0 Local/World）：Local 时把世界轴绕实体
    // rotation 旋到 local 空间。translate 期 rotation 不变 → drag 期轴向稳定；
    // space 切换被 ScenePanel gate（!IsDragging），不会 mid-drag 变。
    const auto axisDirSpace = [&](Axis a) -> glm::vec3 {
        const glm::vec3 base = AxisDir(a);
        return (host.gizmo.space == EditorGizmoState::Space::Local)
                   ? glm::normalize(pTC->rotation * base)
                   : base;
    };
    std::array<AxisProjected, 3> axes{{
        {Axis::X, axisDirSpace(Axis::X), {}, false},
        {Axis::Y, axisDirSpace(Axis::Y), {}, false},
        {Axis::Z, axisDirSpace(Axis::Z), {}, false},
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
        float bestDist = hitThresholdPx;
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
            const glm::vec3 axisDir = axisDirSpace(host.gizmo.hoveredAxis);
            const auto hit = GM::ClosestPointOnAxisToRay(mouseRay->origin, mouseRay->dir,
                                                        entityWorldPos, axisDir);
            if (hit.has_value())
            {
                host.gizmo.draggingAxis       = host.gizmo.hoveredAxis;
                host.gizmo.dragStartEntityPos = entityWorldPos;
                host.gizmo.dragStartHitOnAxis = *hit;
                // 多选群组 translate：快照其余选中实体的起点 position（与
                // primary 同样直接取 component.position，保持 gizmo 既有
                // "position 当 world" 的一致语义）。单选时集合为空。
                host.gizmo.dragStartAdditional.clear();
                for (const auto& other : host.selection.additionalSelectedEntities)
                {
                    if (auto* pOtherTC = pWorld->GetComponent<TransformComponent>(other))
                    {
                        host.gizmo.dragStartAdditional.push_back(
                            {other, pOtherTC->position, pOtherTC->rotation, pOtherTC->scale});
                    }
                }
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
                const glm::vec3 axisDir = axisDirSpace(host.gizmo.draggingAxis);
                const auto hit = GM::ClosestPointOnAxisToRay(mouseRay->origin, mouseRay->dir,
                                                            host.gizmo.dragStartEntityPos,
                                                            axisDir);
                if (hit.has_value())
                {
                    const glm::vec3 delta   = *hit - host.gizmo.dragStartHitOnAxis;
                    glm::vec3       newPos  = host.gizmo.dragStartEntityPos
                                            + axisDir * glm::dot(delta, axisDir);
                    // 网格 snap（gap 报告 §3 P0）：把沿拖动轴的世界分量量化到
                    // snapTranslateStep。snapEnabled=false 时不进入=零回归。axisDir
                    // 是单位世界轴 → 量化其分量即把该轴世界坐标对齐到网格。
                    if (host.settings.snapEnabled)
                    {
                        const float comp    = glm::dot(newPos, axisDir);
                        const float snapped = Orange::Editor::Util::SnapToStep(
                            comp, host.settings.snapTranslateStep);
                        newPos += axisDir * (snapped - comp);
                    }
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

                    // 多选群组 translate：其余选中实体随 primary 刚体平移。
                    // groupDelta = primary 当前 newPos - primary 拖动起点（含
                    // snap）；各 follower = 自身起点 + groupDelta。单选时
                    // dragStartAdditional 空 → 整个循环不执行 = 零回归。每个
                    // follower 一条 SetFieldValueCommand，按 (entity,fieldKey)
                    // 在 "Translate Drag" group 内各自 coalesce；EndGroup 把整组
                    // 包成一次 Undo → 撤销时所有实体一起回退。
                    const glm::vec3 groupDelta = newPos - host.gizmo.dragStartEntityPos;
                    for (const auto& snap : host.gizmo.dragStartAdditional)
                    {
                        if (!pWorld->IsValid(snap.entity)) { continue; }
                        auto* pOtherTC = pWorld->GetComponent<TransformComponent>(snap.entity);
                        if (pOtherTC == nullptr) { continue; }
                        const glm::vec3 otherNew = snap.position + groupDelta;
                        if (otherNew != pOtherTC->position)
                        {
                            pOtherTC->position = otherNew;
                            host.cmdStack.Push(std::make_unique<SetFieldValueCommand<glm::vec3>>(
                                snap.entity,
                                std::string("Transform.position"),
                                snap.position,
                                otherNew,
                                MakeTransformPositionApply(&host, snap.entity)));
                        }
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
            const ImU32  col       = SettingsAxisColor(host.settings, ap.axis, highlight);
            const float  thickness = highlight ? host.settings.gizmoLineWidthTranslateHighlight
                                               : host.settings.gizmoLineWidthTranslateIdle;
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
