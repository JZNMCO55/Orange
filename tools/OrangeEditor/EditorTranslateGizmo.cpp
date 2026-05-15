#include "EditorTranslateGizmo.h"

#include "EditorCameraControl.h"
#include "command/SetFieldValueCommand.h"

#include <orange/engine/render/Camera.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>

namespace
{

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

// world → 屏幕投影。返回 nullopt = 点在相机后方（pos_clip.w <= 0）或除法
// 退化，caller 应跳过绘制 / hit-test。
struct ScreenProjection { glm::vec2 screen; float clipW; };

std::optional<ScreenProjection>
ProjectWorldToScreen(const glm::vec3&  worldPos,
                     const glm::mat4&  viewProj,
                     glm::vec2         imageOrigin,
                     glm::vec2         imageSize) noexcept
{
    const glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 1e-4f) { return std::nullopt; }  // 相机后方 / 视锥外
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    // Vulkan NDC y-down 与 ImGui 屏幕 y-down 同向（与 EditorPicking 反演路径
    // 对偶），不额外翻转 y。
    const float sx = imageOrigin.x + (ndcX * 0.5f + 0.5f) * imageSize.x;
    const float sy = imageOrigin.y + (ndcY * 0.5f + 0.5f) * imageSize.y;
    return ScreenProjection{glm::vec2(sx, sy), clip.w};
}

// 2D 点到线段最短距离。
float
PointSegmentDistance2D(glm::vec2 p, glm::vec2 a, glm::vec2 b) noexcept
{
    const glm::vec2 ab = b - a;
    const float len2 = glm::dot(ab, ab);
    if (len2 < 1e-6f) { return glm::length(p - a); }
    float t = glm::dot(p - a, ab) / len2;
    t = std::clamp(t, 0.0f, 1.0f);
    const glm::vec2 closest = a + ab * t;
    return glm::length(p - closest);
}

// 屏幕坐标 → world ray（origin + dir）。复用 EditorPicking 同款反投影路径。
struct WorldRay { glm::vec3 origin; glm::vec3 dir; };

std::optional<WorldRay>
ScreenToWorldRay(glm::vec2 mouseScreen,
                 glm::vec2 imageOrigin,
                 glm::vec2 imageSize,
                 const glm::mat4& invViewProj) noexcept
{
    if (imageSize.x <= 0.0f || imageSize.y <= 0.0f) { return std::nullopt; }
    const float ndcX = ((mouseScreen.x - imageOrigin.x) / imageSize.x) * 2.0f - 1.0f;
    const float ndcY = ((mouseScreen.y - imageOrigin.y) / imageSize.y) * 2.0f - 1.0f;
    const glm::vec4 nearH = invViewProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    const glm::vec4 farH  = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    if (std::abs(nearH.w) < 1e-9f || std::abs(farH.w) < 1e-9f) { return std::nullopt; }
    const glm::vec3 origin = glm::vec3(nearH) / nearH.w;
    const glm::vec3 farPt  = glm::vec3(farH)  / farH.w;
    const glm::vec3 diff   = farPt - origin;
    const float len = glm::length(diff);
    if (len < 1e-6f) { return std::nullopt; }
    return WorldRay{origin, diff / len};
}

// 两条直线（mouse ray + entity axis line）的最近点（在 axis 上的那一点）。
// 公式：
//   w = O - P, b = dot(D,A), d = dot(D,w), e = dot(A,w)
//   denom = 1 - b*b
//   t = (e - b*d) / denom    （axis 上最近点的参数）
// 退化（denom 接近 0 = 射线与轴近似平行）返回 nullopt。
std::optional<glm::vec3>
ClosestPointOnAxisToRay(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                        const glm::vec3& axisOrigin, const glm::vec3& axisDir) noexcept
{
    const glm::vec3 w = rayOrigin - axisOrigin;
    const float b = glm::dot(rayDir, axisDir);
    const float denom = 1.0f - b * b;
    if (std::abs(denom) < 1e-5f) { return std::nullopt; }
    const float d = glm::dot(rayDir, w);
    const float e = glm::dot(axisDir, w);
    const float t = (e - b * d) / denom;
    return axisOrigin + axisDir * t;
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

    // ---- 早退：Play Mode / 无 World / 无选中实体 / 实体无 Transform ----
    if (host.scene.playState != PlayState::Edit)
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
    //      屏幕约 kHandleScreenLengthPx 像素 ----
    // 取沿 +X 偏 1 unit 的 world 点投影，与 entity 投影的屏幕距离作为
    // "1 unit world = ? px"。处于相机后方时退化用一个安全常数（避免 0
    // 长度 handle），但绝大多数时不会撞到。
    const auto projOrigin = ProjectWorldToScreen(entityWorldPos, viewProj,
                                                 viewportImageOriginScreen,
                                                 viewportImageSize);
    if (!projOrigin.has_value())
    {
        // entity 在相机后方 / clip 外 → 不绘制，不响应输入；拖动中则中止
        AbortDragIfNeeded(host);
        host.gizmo.hoveredAxis = Axis::None;
        return false;
    }
    const auto projOriginPlusX = ProjectWorldToScreen(entityWorldPos + glm::vec3(1.0f, 0.0f, 0.0f),
                                                     viewProj,
                                                     viewportImageOriginScreen,
                                                     viewportImageSize);
    float worldUnitsPerHandle = 1.0f;
    if (projOriginPlusX.has_value())
    {
        const float pxPerUnit = glm::length(projOriginPlusX->screen - projOrigin->screen);
        if (pxPerUnit > 1e-3f)
        {
            worldUnitsPerHandle = kHandleScreenLengthPx / pxPerUnit;
        }
    }

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
        const auto      tipProj  = ProjectWorldToScreen(tipWorld, viewProj,
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
            const float d = PointSegmentDistance2D(mousePos, projOrigin->screen, ap.tipScreen);
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

    // 按下：仅在 viewport image 上 + hover 某轴时启动拖动（不要被
    // ScenePanel 之外的全局 LMB 误触发）
    if (!host.gizmo.IsDragging()
        && imageHovered
        && host.gizmo.IsHovered()
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const auto mouseRay = ScreenToWorldRay(mousePos,
                                               viewportImageOriginScreen,
                                               viewportImageSize,
                                               invViewProj);
        if (mouseRay.has_value())
        {
            const glm::vec3 axisDir = AxisDir(host.gizmo.hoveredAxis);
            const auto hit = ClosestPointOnAxisToRay(mouseRay->origin, mouseRay->dir,
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

    // 拖动中：每帧计算新位置 + Push SetFieldValueCommand（intra-group
    // coalesce 合并多帧）
    if (host.gizmo.IsDragging())
    {
        // LMB 已不再 down（典型：focus 失焦 / 系统级抢占）→ 安全终止
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (host.cmdStack.InGroup()) { host.cmdStack.EndGroup(); }
            host.gizmo.draggingAxis = Axis::None;
        }
        else
        {
            const auto mouseRay = ScreenToWorldRay(mousePos,
                                                   viewportImageOriginScreen,
                                                   viewportImageSize,
                                                   invViewProj);
            if (mouseRay.has_value())
            {
                const glm::vec3 axisDir = AxisDir(host.gizmo.draggingAxis);
                const auto hit = ClosestPointOnAxisToRay(mouseRay->origin, mouseRay->dir,
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

            // LMB 在本帧释放 → 收尾
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                if (host.cmdStack.InGroup()) { host.cmdStack.EndGroup(); }
                host.gizmo.draggingAxis = Axis::None;
            }
        }
    }

    // ---- 绘制（在 ImGui::Image 之上 overlay）----
    // 拿当前 window 的 ImDrawList（ScenePanel 的 ImGui::Begin/End 范围内）。
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (drawList != nullptr)
    {
        for (const auto& ap : axes)
        {
            if (!ap.tipVisible) { continue; }
            const bool   highlight = (host.gizmo.hoveredAxis == ap.axis)
                                  || (host.gizmo.draggingAxis == ap.axis);
            const ImU32  col       = AxisColor(ap.axis, highlight);
            const float  thickness = highlight ? 4.0f : 2.5f;
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

    // 返回 true：viewport image 上 hover 着 handle，或正在拖动 —— ScenePanel
    // 应跳过本帧 picking 触发（避免 LMB 释放时既拖完 gizmo 又触发 picking）。
    return host.gizmo.IsDragging() || (imageHovered && host.gizmo.IsHovered());
}
