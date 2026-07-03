#include "EditorRotateGizmo.h"

#include "EditorCameraControl.h"
#include "EditorGizmoMath.h"
#include "EditorGroupTransform.h" // Util::RotateAroundPivot（多选群组公转）
#include "EditorMathUtil.h"       // Util::SnapToStep（角度 snap）
#include "command/SetFieldValueCommand.h"

#include <glm/trigonometric.hpp> // glm::radians

#include <orange/engine/render/Camera.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/scene/WorldTransformComponent.h>

#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp> // glm::inverse / glm::quat_cast
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <string>

namespace
{

    namespace GM = OrangeEditor::Internal::GizmoMath;

    // 设计常数（圆环段数不进 settings——视觉上 48 段已足够平滑，多余 budget）。
    // 其余 handle 长度 / hit threshold / 配色 / 线宽 从 host.settings 读取
    // （v0.8 EditorSettings 落地后路径，消除 L13）。
    constexpr int kRingSegments = 48;

    ImU32 SettingsToImU32(const glm::vec4& c) noexcept
    {
        auto byteOf = [](float v) -> int
        {
            if (v < 0.0f)
            {
                v = 0.0f;
            }
            if (v > 1.0f)
            {
                v = 1.0f;
            }
            return static_cast<int>(v * 255.0f + 0.5f);
        };
        return IM_COL32(byteOf(c.r), byteOf(c.g), byteOf(c.b), byteOf(c.a));
    }

    ImU32 SettingsAxisColor(const EditorSettings&  s,
                            EditorGizmoState::Axis axis,
                            bool                   highlight) noexcept
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
            case EditorGizmoState::Axis::X:
                return glm::vec3(1.0f, 0.0f, 0.0f);
            case EditorGizmoState::Axis::Y:
                return glm::vec3(0.0f, 1.0f, 0.0f);
            case EditorGizmoState::Axis::Z:
                return glm::vec3(0.0f, 0.0f, 1.0f);
            default:
                return glm::vec3(0.0f);
        }
    }

    // ---- A1 层级（world→local）辅助 ---------------------------------------
    // 从世界矩阵抽取纯旋转 quat：去掉平移 + 各列归一化（除 scale），再 quat_cast。
    // scale 近 0 的列退化为不归一化（避免除零），与 TransformMath 同款守恒。
    glm::quat RotationFromMatrix(const glm::mat4& m) noexcept
    {
        constexpr float kEps = 1e-8f;
        glm::vec3       c0   = glm::vec3(m[0]);
        glm::vec3       c1   = glm::vec3(m[1]);
        glm::vec3       c2   = glm::vec3(m[2]);
        const float     l0   = glm::length(c0);
        const float     l1   = glm::length(c1);
        const float     l2   = glm::length(c2);
        const glm::mat3 rotMat(l0 > kEps ? c0 / l0 : c0,
                               l1 > kEps ? c1 / l1 : c1,
                               l2 > kEps ? c2 / l2 : c2);
        return glm::normalize(glm::quat_cast(rotMat));
    }

    // 取实体的父世界矩阵：从 HierarchyComponent.parent 找父，读其每帧由
    // PropagateWorldTransforms 缓存的 WorldTransformComponent.world。无父 / 父无
    // cache → identity（→ world==local，零回归）。
    glm::mat4 ParentWorldMatrix(Orange::Engine::World& world, Orange::Engine::Entity entity)
    {
        using Orange::Engine::Scene::HierarchyComponent;
        using Orange::Engine::Scene::WorldTransformComponent;
        const auto* h = world.GetComponent<HierarchyComponent>(entity);
        if (h == nullptr || !h->parent.IsValid())
        {
            return glm::mat4(1.0f);
        }
        const auto* pwtc = world.GetComponent<WorldTransformComponent>(h->parent);
        return (pwtc != nullptr) ? pwtc->world : glm::mat4(1.0f);
    }

    // 取实体的父世界旋转（圆环 Local 朝向基 / world→local 反变换都需要）。无父 /
    // 原点父 → identity（零回归）。
    glm::quat ParentWorldRotation(Orange::Engine::World& world, Orange::Engine::Entity entity)
    {
        return RotationFromMatrix(ParentWorldMatrix(world, entity));
    }

    // 取实体的世界 position：优先读 WorldTransformComponent.world（gizmo 画在 mesh
    // 世界位置）；无 cache fallback 到 local position（root：world==local）。
    glm::vec3 EntityWorldPosition(Orange::Engine::World& world, Orange::Engine::Entity entity,
                                  const Orange::Engine::Scene::TransformComponent& tc)
    {
        using Orange::Engine::Scene::WorldTransformComponent;
        const auto* wtc = world.GetComponent<WorldTransformComponent>(entity);
        return (wtc != nullptr) ? glm::vec3(wtc->world[3]) : tc.position;
    }

    // 取实体的世界 rotation：优先从 WorldTransformComponent.world 抽旋转（圆环朝向 /
    // drag 基准）；无 cache fallback 到 local rotation（root：world==local）。
    glm::quat EntityWorldRotation(Orange::Engine::World& world, Orange::Engine::Entity entity,
                                  const Orange::Engine::Scene::TransformComponent& tc)
    {
        using Orange::Engine::Scene::WorldTransformComponent;
        const auto* wtc = world.GetComponent<WorldTransformComponent>(entity);
        return (wtc != nullptr) ? RotationFromMatrix(wtc->world) : tc.rotation;
    }

    // 把目标世界 rotation 转成实体的 local rotation：newLocal = inverse(parentRot)
    // * targetWorld。root / 原点父：parentRot==identity → 原样返回（零回归）。
    glm::quat WorldRotToLocal(const glm::quat& parentWorldRot, const glm::quat& targetWorldRot)
    {
        return glm::normalize(glm::inverse(parentWorldRot) * targetWorldRot);
    }

    // 选两个在 axis 平面内、彼此正交的单位向量，用于 ring 参数化（u * cos +
    // v * sin 描点）。注意：cross(axis, world_up) 退化（axis ≈ ±Y）时换用 X。
    struct PlaneBasis
    {
        glm::vec3 u;
        glm::vec3 v;
    };

    PlaneBasis MakePlaneBasis(const glm::vec3& axis) noexcept
    {
        glm::vec3 helper = (std::abs(axis.y) < 0.99f) ? glm::vec3(0, 1, 0)
                                                      : glm::vec3(1, 0, 0);
        glm::vec3 u      = glm::normalize(glm::cross(axis, helper));
        glm::vec3 v      = glm::normalize(glm::cross(axis, u));
        return {u, v};
    }

    // 在圆环上取 N 个 world 点（u/v 基 + center + radius）。
    template <std::size_t N>
    std::array<glm::vec3, N>
    SampleRingPoints(const glm::vec3& center, const glm::vec3& axisDir, float radius) noexcept
    {
        const auto               basis = MakePlaneBasis(axisDir);
        std::array<glm::vec3, N> pts;
        for (std::size_t i = 0; i < N; ++i)
        {
            const float theta = (2.0f * 3.14159265358979f * static_cast<float>(i)) / static_cast<float>(N);
            pts[i]            = center + basis.u * (radius * std::cos(theta)) + basis.v * (radius * std::sin(theta));
        }
        return pts;
    }

    // ---- Transform.rotation 写回 + invalidate Euler 缓存 -----------------------
    // 同步 invalidate `selection.transformEulerCacheEntity`，让 Inspector 下一
    // 帧 Quat case 从最新 quat 重算 Euler 显示 —— 与 SchemaInspector.cpp Quat
    // case 的 invalidate 路径完全对偶（区别仅在 caller：那边是 DragFloat3 改
    // Euler 后回算 quat 写入；本 gizmo 是直接生成新 quat，但 Euler 缓存同样
    // 必须失效，否则 Inspector 会显示旧 Euler 数字直到用户手动切实体重算）。
    auto MakeTransformRotationApply(EditorHost* pHost, Orange::Engine::Entity entity)
    {
        return [pHost, entity](const glm::quat& value)
        {
            if (pHost == nullptr)
            {
                return;
            }
            auto* pWorld = pHost->scene.pWorld.get();
            if (pWorld == nullptr)
            {
                return;
            }
            auto* pTC = pWorld->GetComponent<Orange::Engine::Scene::TransformComponent>(entity);
            if (pTC == nullptr)
            {
                return;
            }
            pTC->rotation                              = value;
            pHost->selection.transformEulerCacheEntity = Orange::Engine::Entity::Invalid();
        };
    }

    // 群组 rotate 时 follower 绕 pivot 公转会改 position —— 本 TU 需要 position
    // apply（translate TU 的同名工厂是 file-local 不可见，匿名 namespace 内部
    // 链接同名不冲突）。
    auto MakeTransformPositionApply(EditorHost* pHost, Orange::Engine::Entity entity)
    {
        return [pHost, entity](const glm::vec3& value)
        {
            if (pHost == nullptr)
            {
                return;
            }
            auto* pWorld = pHost->scene.pWorld.get();
            if (pWorld == nullptr)
            {
                return;
            }
            auto* pTC = pWorld->GetComponent<Orange::Engine::Scene::TransformComponent>(entity);
            if (pTC == nullptr)
            {
                return;
            }
            pTC->position = value;
        };
    }

    void AbortDragIfNeeded(EditorHost& host)
    {
        if (!host.gizmo.IsDragging())
        {
            return;
        }
        if (host.cmdStack.InGroup())
        {
            host.cmdStack.EndGroup();
        }
        host.gizmo.draggingAxis = EditorGizmoState::Axis::None;
    }

} // anonymous namespace

bool DrawAndHandleRotateGizmo(EditorHost& host,
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
    auto*                        pTC    = pWorld->GetComponent<TransformComponent>(entity);
    if (pTC == nullptr)
    {
        AbortDragIfNeeded(host);
        host.gizmo.hoveredAxis = Axis::None;
        return false;
    }

    // ---- camera + viewProj ----
    const auto      cam         = BuildEditorCamera(host.camera, aspect);
    const glm::mat4 viewProj    = cam.projection * cam.view;
    const glm::mat4 invViewProj = glm::inverse(viewProj);

    // A1 层级：gizmo 圆环画在实体的**世界**位置 + 朝向（读 WorldTransform
    // Component），而非把 local pos/rot 当 world。root：world==local → 零回归。
    const glm::vec3 entityPos      = EntityWorldPosition(*pWorld, entity, *pTC);
    const glm::quat entityWorldRot = EntityWorldRotation(*pWorld, entity, *pTC);

    // ---- gizmo 半径自适应（同 translate）。探针用相机右向量而非 world X
    //      轴 —— 见 GizmoMath::ComputeWorldUnitsForScreenLength 注释（防止
    //      orbit 相机时三个圆环整体伸缩）。----
    const auto projOrigin = GM::ProjectWorldToScreen(entityPos, viewProj,
                                                     viewportImageOriginScreen,
                                                     viewportImageSize);
    if (!projOrigin.has_value())
    {
        AbortDragIfNeeded(host);
        host.gizmo.hoveredAxis = Axis::None;
        return false;
    }
    const float handleScreenLengthPx = host.settings.gizmoHandleScreenLengthPx;
    const float hitThresholdPx       = host.settings.gizmoHitThresholdPx;
    const auto  ringRadiusOpt        = GM::ComputeWorldUnitsForScreenLength(
        entityPos, cam.view, viewProj,
        viewportImageOriginScreen, viewportImageSize, handleScreenLengthPx);
    const float ringRadius = ringRadiusOpt.value_or(1.0f);

    // ---- 3 个圆环：投影所有顶点到屏幕 ----
    struct RingProjected
    {
        Axis                                 axis;
        glm::vec3                            axisDir;
        std::array<glm::vec2, kRingSegments> screenPts;
        bool                                 any_visible = false;
    };
    // space-aware 轴向（gap §3 P0 Local/World）：Local 时把世界轴绕给定 rotation
    // 旋到该朝向。A1 层级：rotation 一律用**世界**旋转（draw/hit 用当前
    // entityWorldRot，环随 mesh 世界朝向；apply 用 dragStartEntityWorldRot，
    // drag 期实体世界 rot 在变，固定到起点轴避免旋转漂移）。World 模式与
    // rotation 无关 → 零回归。
    const auto axisIn = [](EditorGizmoState::Space space, const glm::quat& rot,
                           Axis a) -> glm::vec3
    {
        const glm::vec3 base = AxisDir(a);
        return (space == EditorGizmoState::Space::Local)
                   ? glm::normalize(rot * base)
                   : base;
    };
    std::array<RingProjected, 3> rings{{
        {Axis::X, axisIn(host.gizmo.space, entityWorldRot, Axis::X), {}, false},
        {Axis::Y, axisIn(host.gizmo.space, entityWorldRot, Axis::Y), {}, false},
        {Axis::Z, axisIn(host.gizmo.space, entityWorldRot, Axis::Z), {}, false},
    }};
    for (auto& rp : rings)
    {
        const auto pts       = SampleRingPoints<kRingSegments>(entityPos, rp.axisDir, ringRadius);
        bool       allBehind = true;
        for (std::size_t i = 0; i < kRingSegments; ++i)
        {
            const auto sp = GM::ProjectWorldToScreen(pts[i], viewProj,
                                                     viewportImageOriginScreen,
                                                     viewportImageSize);
            if (sp.has_value())
            {
                rp.screenPts[i] = sp->screen;
                allBehind       = false;
            }
            else
            {
                // 标记一个 sentinel（不绘制本段；hit-test 跳过）。
                rp.screenPts[i] = glm::vec2(std::numeric_limits<float>::quiet_NaN());
            }
        }
        rp.any_visible = !allBehind;
    }

    // ---- 2D hit-test（拖动期间跳过；强制 hoveredAxis = draggingAxis）----
    const ImVec2    mousePosIm = ImGui::GetMousePos();
    const glm::vec2 mousePos(mousePosIm.x, mousePosIm.y);

    if (!host.gizmo.IsDragging())
    {
        Axis  bestAxis = Axis::None;
        float bestDist = hitThresholdPx;
        for (const auto& rp : rings)
        {
            if (!rp.any_visible)
            {
                continue;
            }
            for (std::size_t i = 0; i < kRingSegments; ++i)
            {
                const std::size_t j = (i + 1) % kRingSegments;
                const glm::vec2&  a = rp.screenPts[i];
                const glm::vec2&  b = rp.screenPts[j];
                if (std::isnan(a.x) || std::isnan(b.x))
                {
                    continue;
                }
                const float d = GM::PointSegmentDistance2D(mousePos, a, b);
                if (d < bestDist)
                {
                    bestDist = d;
                    bestAxis = rp.axis;
                }
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

    // 按下：mouse ray 与 axis plane 交点 → dragStartRotateRef
    if (!host.gizmo.IsDragging() && imageHovered && host.gizmo.IsHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const auto mouseRay = GM::ScreenToWorldRay(mousePos,
                                                   viewportImageOriginScreen,
                                                   viewportImageSize,
                                                   invViewProj);
        if (mouseRay.has_value())
        {
            const glm::vec3 axisDir = axisIn(host.gizmo.space, entityWorldRot,
                                             host.gizmo.hoveredAxis);
            const auto      t       = GM::RayPlaneIntersect(mouseRay->origin, mouseRay->dir,
                                                            entityPos, axisDir);
            if (t.has_value())
            {
                const glm::vec3 hitWorld   = mouseRay->origin + mouseRay->dir * (*t);
                const glm::vec3 fromCenter = hitWorld - entityPos;
                const float     len        = glm::length(fromCenter);
                if (len > 1e-4f)
                {
                    host.gizmo.draggingAxis            = host.gizmo.hoveredAxis;
                    host.gizmo.dragStartEntityRot      = pTC->rotation;  // local 起点（命令 oldVal）
                    host.gizmo.dragStartEntityWorldRot = entityWorldRot; // world 起点（drag deltaQ 基准）
                    host.gizmo.dragStartRotateRef      = fromCenter / len;
                    // 多选群组 rotate：快照其余选中实体的 pos+rot。position 存
                    // **world**（绕 world pivot 公转），rotation 存 local（命令 oldVal）
                    // 及 world（公转基准）。pivot = primary world 位置，rotate 期不动。
                    // 单选时集合为空。root：world==local → 零回归。
                    host.gizmo.dragStartAdditional.clear();
                    for (const auto& other : host.selection.additionalSelectedEntities)
                    {
                        if (auto* pOtherTC = pWorld->GetComponent<TransformComponent>(other))
                        {
                            EditorGizmoState::GroupDragSnapshot snap;
                            snap.entity     = other;
                            snap.position   = pOtherTC->position; // local 起点（命令 oldVal）
                            snap.rotation   = pOtherTC->rotation; // local 起点（命令 oldVal）
                            snap.scale      = pOtherTC->scale;
                            snap.worldStart = EntityWorldPosition(*pWorld, other, *pOtherTC);
                            snap.worldRot   = EntityWorldRotation(*pWorld, other, *pOtherTC);
                            host.gizmo.dragStartAdditional.push_back(snap);
                        }
                    }
                    host.cmdStack.BeginGroup("Rotate Drag", MergeMode::Ends);
                }
            }
        }
    }

    if (host.gizmo.IsDragging())
    {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (host.cmdStack.InGroup())
            {
                host.cmdStack.EndGroup();
            }
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
                // A1 层级：axisDir 用拖动起点的**世界** rotation（Local 模式轴随
                // mesh 世界朝向，固定到起点避免旋转漂移）。World 模式与 rot 无关。
                const glm::vec3 axisDir = axisIn(host.gizmo.space,
                                                 host.gizmo.dragStartEntityWorldRot,
                                                 host.gizmo.draggingAxis);
                const auto      t       = GM::RayPlaneIntersect(mouseRay->origin, mouseRay->dir,
                                                                entityPos, axisDir);
                if (t.has_value())
                {
                    const glm::vec3 hitWorld   = mouseRay->origin + mouseRay->dir * (*t);
                    const glm::vec3 fromCenter = hitWorld - entityPos;
                    const float     len        = glm::length(fromCenter);
                    if (len > 1e-4f)
                    {
                        const glm::vec3 currentRef = fromCenter / len;
                        // signed angle between dragStartRotateRef → currentRef
                        // around axisDir：atan2(cross·axis, dot)。
                        const float     dotPart    = glm::dot(host.gizmo.dragStartRotateRef, currentRef);
                        const glm::vec3 cr         = glm::cross(host.gizmo.dragStartRotateRef, currentRef);
                        const float     crossPart  = glm::dot(cr, axisDir);
                        float           deltaAngle = std::atan2(crossPart, dotPart);
                        // 角度 snap（gap 报告 §3 P0）：把相对拖动起点的增量角量化到
                        // snapRotateStepDeg（增量 snap，非绝对——四元数下绝对角较难且
                        // 多数编辑器即增量 step 旋转；snapEnabled=false 不进入=零回归）。
                        // snap 开关开 或 拖拽中按住 Ctrl（Unity 标准临时吸附）。
                        if (host.settings.snapEnabled || ImGui::GetIO().KeyCtrl)
                        {
                            deltaAngle = Orange::Editor::Util::SnapToStep(
                                deltaAngle, glm::radians(host.settings.snapRotateStepDeg));
                        }

                        const glm::quat deltaQ = glm::angleAxis(deltaAngle, axisDir);
                        // A1 层级：deltaQ 在世界空间累乘到拖动起点的**世界** rot
                        // → targetWorldRot；写回 TransformComponent.rotation 前经
                        // primary 父 worldRot 逆变换转 local。root/原点父：
                        // parentWorldRot==identity → newLocal==targetWorld==
                        // deltaQ*dragStartLocalRot，与旧路径逐字节一致（零回归）。
                        const glm::quat targetWorldRot =
                            deltaQ * host.gizmo.dragStartEntityWorldRot;
                        const glm::quat parentWorldRot = ParentWorldRotation(*pWorld, entity);
                        const glm::quat newRot         = WorldRotToLocal(parentWorldRot, targetWorldRot);
                        const glm::quat oldRot         = pTC->rotation;

                        // quat 直接 != 比较有 epsilon 风险；用 dot 阈值更稳。
                        const float similarity = std::abs(glm::dot(oldRot, newRot));
                        if (similarity < 1.0f - 1e-6f)
                        {
                            pTC->rotation = newRot;
                            host.selection.transformEulerCacheEntity =
                                Orange::Engine::Entity::Invalid();
                            host.cmdStack.Push(std::make_unique<SetFieldValueCommand<glm::quat>>(
                                entity,
                                std::string("Transform.rotation"),
                                host.gizmo.dragStartEntityRot, // oldVal 锁定到拖动起点 local
                                newRot,
                                MakeTransformRotationApply(&host, entity)));

                            // 多选群组 rotate：follower 绕 primary **世界**位置（pivot）
                            // 公转 deltaQ 得新世界位姿，再各自经父 worldRot/parentWorld
                            // 转 local 写回。单选时快照空 → 不执行 = 零回归。pos/rot
                            // 各一条 SetFieldValueCommand，在 "Rotate Drag" group 内按
                            // (entity,fieldKey) coalesce。
                            for (const auto& snap : host.gizmo.dragStartAdditional)
                            {
                                if (!pWorld->IsValid(snap.entity))
                                {
                                    continue;
                                }
                                auto* pFTC = pWorld->GetComponent<TransformComponent>(snap.entity);
                                if (pFTC == nullptr)
                                {
                                    continue;
                                }
                                // 世界空间：position 绕世界 pivot 公转、世界 rot 左乘 deltaQ。
                                const glm::vec3 nWorldPos = Orange::Editor::Util::RotateAroundPivot(
                                    snap.worldStart, entityPos, deltaQ);
                                const glm::quat nWorldRot = deltaQ * snap.worldRot;
                                // 转回各 follower 自身 local（其父 worldMatrix / worldRot）。
                                const glm::mat4 fParentWorld =
                                    ParentWorldMatrix(*pWorld, snap.entity);
                                const glm::quat fParentWorldRot =
                                    ParentWorldRotation(*pWorld, snap.entity);
                                const glm::vec3 nPos = glm::vec3(
                                    glm::inverse(fParentWorld) * glm::vec4(nWorldPos, 1.0f));
                                const glm::quat nRot = WorldRotToLocal(fParentWorldRot, nWorldRot);
                                pFTC->position       = nPos;
                                pFTC->rotation       = nRot;
                                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<glm::vec3>>(
                                    snap.entity, std::string("Transform.position"),
                                    snap.position, nPos, // oldVal = local 起点
                                    MakeTransformPositionApply(&host, snap.entity)));
                                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<glm::quat>>(
                                    snap.entity, std::string("Transform.rotation"),
                                    snap.rotation, nRot, // oldVal = local 起点
                                    MakeTransformRotationApply(&host, snap.entity)));
                            }
                        }
                    }
                }
            }

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                if (host.cmdStack.InGroup())
                {
                    host.cmdStack.EndGroup();
                }
                host.gizmo.draggingAxis = Axis::None;
            }
        }
    }

    // ---- 绘制 polyline ring ----
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (drawList != nullptr)
    {
        for (const auto& rp : rings)
        {
            if (!rp.any_visible)
            {
                continue;
            }
            const bool  highlight = (host.gizmo.hoveredAxis == rp.axis) || (host.gizmo.draggingAxis == rp.axis);
            const ImU32 col       = SettingsAxisColor(host.settings, rp.axis, highlight);
            const float thickness = highlight ? host.settings.gizmoLineWidthRotateHighlight
                                              : host.settings.gizmoLineWidthRotateIdle;
            for (std::size_t i = 0; i < kRingSegments; ++i)
            {
                const std::size_t j = (i + 1) % kRingSegments;
                const glm::vec2&  a = rp.screenPts[i];
                const glm::vec2&  b = rp.screenPts[j];
                if (std::isnan(a.x) || std::isnan(b.x))
                {
                    continue;
                }
                drawList->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), col, thickness);
            }
        }
    }

    return host.gizmo.IsDragging() || (imageHovered && host.gizmo.IsHovered());
}
