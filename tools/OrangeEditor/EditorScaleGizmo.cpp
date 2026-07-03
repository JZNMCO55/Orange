#include "EditorScaleGizmo.h"

#include "EditorCameraControl.h"
#include "EditorGizmoMath.h"
#include "EditorGroupTransform.h" // Util::ScaleAroundPivot（多选群组缩放）
#include "EditorMathUtil.h"       // Util::SnapToStep（比例 snap）
#include "command/SetFieldValueCommand.h"

#include <orange/engine/render/Camera.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/scene/WorldTransformComponent.h>

#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp> // glm::quat_cast / glm::inverse
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <string>

namespace
{

    namespace GM = OrangeEditor::Internal::GizmoMath;

    // handle 长度 / hit threshold / 配色 / 线宽 从 host.settings 读取（v0.8 之后）。
    // cube / center 尺寸 / factor clamp 暂保留 hardcode（不属于 v0.8 首批纳入
    // settings 的视觉常量列表，未来扩展再纳入）。
    constexpr float kCubeHalfSizePx    = 6.0f;  // axis tip 立方体（屏幕方块）边长一半
    constexpr float kCenterHalfSizePx  = 7.0f;  // 中心 uniform cube 边长一半
    constexpr float kCenterHitRadiusPx = 10.0f; // 中心 handle hit-test 范围
    constexpr float kFactorMin         = 0.01f;
    constexpr float kFactorMax         = 100.0f;

    // 中心 handle 颜色（uniform scale 用，无对应轴色）暂保留 hardcode——
    // settings 仅覆盖 X/Y/Z 三轴 + idle/highlight 6 色。
    constexpr ImU32 kColCenter       = IM_COL32(220, 220, 220, 255);
    constexpr ImU32 kColCenterBright = IM_COL32(255, 240, 140, 255);

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
            case EditorGizmoState::Axis::Center:
                return highlight ? kColCenterBright : kColCenter;
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
    // 从世界矩阵抽取纯旋转 quat：去平移 + 各列归一化（除 scale），再 quat_cast。
    // scale 近 0 的列退化为不归一化（避免除零）。
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

    // 取实体的世界 position：优先读 WorldTransformComponent.world（gizmo 画在 mesh
    // 世界位置）；无 cache fallback 到 local position（root：world==local）。
    glm::vec3 EntityWorldPosition(Orange::Engine::World& world, Orange::Engine::Entity entity,
                                  const Orange::Engine::Scene::TransformComponent& tc)
    {
        using Orange::Engine::Scene::WorldTransformComponent;
        const auto* wtc = world.GetComponent<WorldTransformComponent>(entity);
        return (wtc != nullptr) ? glm::vec3(wtc->world[3]) : tc.position;
    }

    // 取实体的父世界矩阵（群组 follower 的世界 position 转 local 用）。无父 / 父
    // 无 cache → identity（→ world==local，零回归）。
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

    // 取实体的世界 rotation：优先从 WorldTransformComponent.world 抽旋转（handle 轴
    // 朝向画在 mesh 世界朝向）；无 cache fallback 到 local rotation（root：相等）。
    glm::quat EntityWorldRotation(Orange::Engine::World& world, Orange::Engine::Entity entity,
                                  const Orange::Engine::Scene::TransformComponent& tc)
    {
        using Orange::Engine::Scene::WorldTransformComponent;
        const auto* wtc = world.GetComponent<WorldTransformComponent>(entity);
        return (wtc != nullptr) ? RotationFromMatrix(wtc->world) : tc.rotation;
    }

    auto MakeTransformScaleApply(EditorHost* pHost, Orange::Engine::Entity entity)
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
            pTC->scale = value;
        };
    }

    // 群组 scale 时 follower 绕 pivot 缩放会改 position —— 本 TU 需要 position
    // apply（translate TU 的同名工厂 file-local 不可见；匿名 namespace 内部链接
    // 同名不冲突）。
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

bool DrawAndHandleScaleGizmo(EditorHost& host,
                             glm::vec2   viewportImageOriginScreen,
                             glm::vec2   viewportImageSize,
                             float       aspect)
{
    using Axis = EditorGizmoState::Axis;
    using Orange::Engine::Scene::TransformComponent;

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

    const auto      cam         = BuildEditorCamera(host.camera, aspect);
    const glm::mat4 viewProj    = cam.projection * cam.view;
    const glm::mat4 invViewProj = glm::inverse(viewProj);

    // A1 层级（保守做法，spec §一.5）：origin 改读实体的**世界**位置（handle
    // 画在 mesh 上，而非 parented 实体的 local 偏移处）。root：world==local →
    // 零回归。scale 值仍写 **local**（见下方限制说明）。
    const glm::vec3 entityPos = EntityWorldPosition(*pWorld, entity, *pTC);
    // Scale 必须 local：pTC->scale.xyz 永远表示沿实体局部轴的缩放系数。
    // 实体被 rotate 后若 gizmo 仍画 world XYZ，视觉 handle 与实际缩放方
    // 向脱钩——拖红轴看似沿水平方向拉，实体却沿"局部 X（已旋转）"伸缩。
    // 故 axis 一律 entityRot * worldAxis，绘制 + hit-test + drag math 三
    // 处保持同一基。Translate 也可同样改，但 world-axis translate 是工
    // 业惯例，不动；Rotate 同理。
    //
    // A1 层级：entityRot 改用实体的**世界**旋转（含父链旋转），让 handle 轴朝向
    // 画在 mesh 的世界朝向上（parented 到旋转父的实体，handle 跟 mesh 视觉一致）。
    // root / 父无旋转：worldRot==localRot → handle 朝向不变（零回归）。
    //
    // ⚠️ 限制（明确不支持，spec §一.5）：当父链**带旋转**且做 **non-uniform
    // scale** 时——沿世界朝向轴拖出的 factor 直接乘到 local scale 分量，是个
    // 近似。父旋转会让"沿世界轴的非均匀缩放"在子 local 空间 shear（无法用纯
    // 对角 scale 表示），故父带旋转 + non-uniform scale 的精确写回是公认难点，
    // 本实现不强行做（父无旋转时正确；uniform/center scale 任意父都正确——
    // 各分量等比，无 shear）。需要精确支持时另起 session 引入完整 world TRS
    // 分解 + 可能的 shear-aware scale 表示。
    const glm::mat3 entityRot =
        glm::mat3_cast(EntityWorldRotation(*pWorld, entity, *pTC));

    const auto projOrigin = GM::ProjectWorldToScreen(entityPos, viewProj,
                                                     viewportImageOriginScreen,
                                                     viewportImageSize);
    if (!projOrigin.has_value())
    {
        AbortDragIfNeeded(host);
        host.gizmo.hoveredAxis = Axis::None;
        return false;
    }
    // 探针用相机右向量而非 world X 轴 —— 见 GizmoMath::ComputeWorldUnits
    // ForScreenLength 注释（防止 orbit 相机时 handle 整体伸缩）。
    const float handleScreenLengthPx = host.settings.gizmoHandleScreenLengthPx;
    const float hitThresholdPx       = host.settings.gizmoHitThresholdPx;
    const auto  handleWorldLen       = GM::ComputeWorldUnitsForScreenLength(
        entityPos, cam.view, viewProj,
        viewportImageOriginScreen, viewportImageSize, handleScreenLengthPx);
    const float worldUnitsPerHandle = handleWorldLen.value_or(1.0f);

    struct AxisProjected
    {
        Axis      axis;
        glm::vec3 dir;
        glm::vec2 tipScreen;
        bool      tipVisible;
    };
    std::array<AxisProjected, 3> axes{{
        {Axis::X, entityRot * AxisDir(Axis::X), {}, false},
        {Axis::Y, entityRot * AxisDir(Axis::Y), {}, false},
        {Axis::Z, entityRot * AxisDir(Axis::Z), {}, false},
    }};
    for (auto& ap : axes)
    {
        const glm::vec3 tipWorld = entityPos + ap.dir * worldUnitsPerHandle;
        const auto      tipProj  = GM::ProjectWorldToScreen(tipWorld, viewProj,
                                                            viewportImageOriginScreen,
                                                            viewportImageSize);
        if (tipProj.has_value())
        {
            ap.tipScreen  = tipProj->screen;
            ap.tipVisible = true;
        }
    }

    const ImVec2    mousePosIm = ImGui::GetMousePos();
    const glm::vec2 mousePos(mousePosIm.x, mousePosIm.y);

    // ---- hit-test ----
    if (!host.gizmo.IsDragging())
    {
        // Center handle 优先（与 axis 重叠时取 center）。
        const float distCenter = glm::length(mousePos - projOrigin->screen);
        if (distCenter < kCenterHitRadiusPx)
        {
            host.gizmo.hoveredAxis = Axis::Center;
        }
        else
        {
            Axis  bestAxis = Axis::None;
            float bestDist = hitThresholdPx;
            for (const auto& ap : axes)
            {
                if (!ap.tipVisible)
                {
                    continue;
                }
                const float d = GM::PointSegmentDistance2D(mousePos, projOrigin->screen, ap.tipScreen);
                if (d < bestDist)
                {
                    bestDist = d;
                    bestAxis = ap.axis;
                }
            }
            host.gizmo.hoveredAxis = bestAxis;
        }
    }
    else
    {
        host.gizmo.hoveredAxis = host.gizmo.draggingAxis;
    }

    const bool imageHovered = ImGui::IsItemHovered();

    // ---- 按下：分 axis / center 两路 ----
    if (!host.gizmo.IsDragging() && imageHovered && host.gizmo.IsHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (host.gizmo.hoveredAxis == Axis::Center)
        {
            // uniform center scale：记录鼠标屏幕坐标作为 ref；不需要 axis 解算
            host.gizmo.draggingAxis         = Axis::Center;
            host.gizmo.dragStartEntityScale = pTC->scale;
            host.gizmo.dragStartEntityPos   = entityPos; // 群组 scale pivot（A1：世界）
            // A1 层级：drag 基准用实体的**世界**旋转（与 origin / axis draw 同基）。
            // root / 父无旋转：worldRot==localRot → 零回归。
            host.gizmo.dragStartEntityRot   = EntityWorldRotation(*pWorld, entity, *pTC);
            host.gizmo.dragStartMouseScreen = glm::vec3(mousePos.x, mousePos.y, 0.0f);
            // 多选群组 scale：快照其余选中实体。position 存 local（命令 oldVal）+
            // world（绕 world pivot 缩放）；scale 存 local。root：world==local → 零回归。
            host.gizmo.dragStartAdditional.clear();
            for (const auto& other : host.selection.additionalSelectedEntities)
            {
                if (auto* pOtherTC = pWorld->GetComponent<TransformComponent>(other))
                {
                    EditorGizmoState::GroupDragSnapshot snap;
                    snap.entity     = other;
                    snap.position   = pOtherTC->position; // local 起点（命令 oldVal）
                    snap.rotation   = pOtherTC->rotation;
                    snap.scale      = pOtherTC->scale;
                    snap.worldStart = EntityWorldPosition(*pWorld, other, *pOtherTC);
                    host.gizmo.dragStartAdditional.push_back(snap);
                }
            }
            host.cmdStack.BeginGroup("Scale Drag", MergeMode::Ends);
        }
        else
        {
            const auto mouseRay = GM::ScreenToWorldRay(mousePos,
                                                       viewportImageOriginScreen,
                                                       viewportImageSize,
                                                       invViewProj);
            if (mouseRay.has_value())
            {
                const glm::vec3 axisDir = entityRot * AxisDir(host.gizmo.hoveredAxis);
                const auto      hit     = GM::ClosestPointOnAxisToRay(mouseRay->origin, mouseRay->dir,
                                                                      entityPos, axisDir);
                if (hit.has_value())
                {
                    const float signedDist = glm::dot(*hit - entityPos, axisDir);
                    // 起始 signed 距离过小（点击太靠近原点 → ratio 退化）
                    // 跳过本次按下：用户重试即可
                    if (std::abs(signedDist) > 1e-3f)
                    {
                        host.gizmo.draggingAxis         = host.gizmo.hoveredAxis;
                        host.gizmo.dragStartEntityScale = pTC->scale;
                        host.gizmo.dragStartEntityPos   = entityPos; // 群组 pivot（A1：世界）
                        // A1 层级：drag 基准用**世界**旋转（与 origin / axis draw 同基）。
                        host.gizmo.dragStartEntityRot      = EntityWorldRotation(*pWorld, entity, *pTC);
                        host.gizmo.dragStartScaleRefSigned = signedDist;
                        // 多选群组 scale：快照 pos(local+world) / rot / scale。
                        host.gizmo.dragStartAdditional.clear();
                        for (const auto& other : host.selection.additionalSelectedEntities)
                        {
                            if (auto* pOtherTC = pWorld->GetComponent<TransformComponent>(other))
                            {
                                EditorGizmoState::GroupDragSnapshot snap;
                                snap.entity     = other;
                                snap.position   = pOtherTC->position; // local 起点（命令 oldVal）
                                snap.rotation   = pOtherTC->rotation;
                                snap.scale      = pOtherTC->scale;
                                snap.worldStart = EntityWorldPosition(*pWorld, other, *pOtherTC);
                                host.gizmo.dragStartAdditional.push_back(snap);
                            }
                        }
                        host.cmdStack.BeginGroup("Scale Drag", MergeMode::Ends);
                    }
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
            glm::vec3 newScale  = host.gizmo.dragStartEntityScale;
            bool      hasUpdate = false;

            if (host.gizmo.draggingAxis == Axis::Center)
            {
                // uniform: 屏幕 (dx + dy) / 100 + 1 = factor（Lumix-like；
                // 向右 / 向下拖动放大；向左 / 向上拖动缩小）。
                const float dx = mousePos.x - host.gizmo.dragStartMouseScreen.x;
                const float dy = mousePos.y - host.gizmo.dragStartMouseScreen.y;
                float       f  = 1.0f + (dx + dy) / 100.0f;
                f              = std::clamp(f, kFactorMin, kFactorMax);
                newScale       = host.gizmo.dragStartEntityScale * f;
                hasUpdate      = true;
            }
            else
            {
                const auto mouseRay = GM::ScreenToWorldRay(mousePos,
                                                           viewportImageOriginScreen,
                                                           viewportImageSize,
                                                           invViewProj);
                if (mouseRay.has_value())
                {
                    // axisDir 必须用**按下帧捕获**的 dragStartEntityRot（A1 后存
                    // 实体的**世界**旋转，与 origin/axis draw 同基），而非每帧最新
                    // pTC->rotation：dragStartScaleRefSigned 是按下帧用该旋转算的沿轴
                    // 参考距离，drag 更新若改用别的旋转基准则 factor 失配。B2.6 编辑期
                    // 动画预览 tick 会在 drag 期间每帧改 selectedEntity 的 rotation（预览
                    // 实体==选中实体时），推翻了旧注释"drag 期间无人写 rotation"的假设——
                    // 故必须用捕获基准，否则旋转中拖 scale 轴会抖动 / 缩放方向跳变。
                    const glm::vec3 axisDir =
                        glm::mat3_cast(host.gizmo.dragStartEntityRot) * AxisDir(host.gizmo.draggingAxis);
                    const auto hit = GM::ClosestPointOnAxisToRay(mouseRay->origin, mouseRay->dir,
                                                                 host.gizmo.dragStartEntityPos,
                                                                 axisDir);
                    if (hit.has_value() && std::abs(host.gizmo.dragStartScaleRefSigned) > 1e-4f)
                    {
                        const float currentSigned = glm::dot(*hit - host.gizmo.dragStartEntityPos,
                                                             axisDir);
                        float       factor        = currentSigned / host.gizmo.dragStartScaleRefSigned;
                        factor                    = std::clamp(factor, kFactorMin, kFactorMax);
                        switch (host.gizmo.draggingAxis)
                        {
                            case Axis::X:
                                newScale.x = host.gizmo.dragStartEntityScale.x * factor;
                                break;
                            case Axis::Y:
                                newScale.y = host.gizmo.dragStartEntityScale.y * factor;
                                break;
                            case Axis::Z:
                                newScale.z = host.gizmo.dragStartEntityScale.z * factor;
                                break;
                            default:
                                break;
                        }
                        hasUpdate = true;
                    }
                }
            }

            // 比例 snap（gap 报告 §3 P0）：各分量量化到 snapScaleStep；clamp 到
            // ≥step 避免 snap 到 0 的退化 scale（snapEnabled=false 不进入=零回归）。
            // snap 开关开 或 拖拽中按住 Ctrl（Unity 标准临时吸附）。
            if (hasUpdate && (host.settings.snapEnabled || ImGui::GetIO().KeyCtrl))
            {
                const float step     = host.settings.snapScaleStep;
                auto        snapAxis = [step](float v)
                {
                    const float s = Orange::Editor::Util::SnapToStep(v, step);
                    return s <= 0.0f ? step : s;
                };
                newScale.x = snapAxis(newScale.x);
                newScale.y = snapAxis(newScale.y);
                newScale.z = snapAxis(newScale.z);
            }

            if (hasUpdate && newScale != pTC->scale)
            {
                pTC->scale = newScale;
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<glm::vec3>>(
                    entity,
                    std::string("Transform.scale"),
                    host.gizmo.dragStartEntityScale,
                    newScale,
                    MakeTransformScaleApply(&host, entity)));

                // 多选群组 scale：follower 绕 primary 位置（pivot）按 factorVec
                // 缩放位置 + 自身 scale 乘 factorVec。factorVec = primary newScale /
                // dragStartScale（含 snap，逐分量 guard 起点 0）。单选时快照空 →
                // 不执行 = 零回归。pos/scale 各一条命令在 "Scale Drag" group 内
                // 按 (entity,fieldKey) coalesce，EndGroup 一次 Undo 撤全组。
                const glm::vec3& s0 = host.gizmo.dragStartEntityScale;
                const glm::vec3  factorVec(
                    std::abs(s0.x) > 1e-6f ? newScale.x / s0.x : 1.0f,
                    std::abs(s0.y) > 1e-6f ? newScale.y / s0.y : 1.0f,
                    std::abs(s0.z) > 1e-6f ? newScale.z / s0.z : 1.0f);
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
                    // follower 位置缩放须在 primary 按下帧 local 系内做（factorVec 是
                    // 沿 primary local 轴的乘数）：旋转过的 primary 下，若按世界轴分量缩
                    // 放 follower 偏移，群组形变会与拖动轴脱钩。把偏移用 primary 旋转的
                    // 共轭转进 local 系缩放、再转回世界系。primary 未旋转（q=identity）时
                    // 退化为旧 ScaleAroundPivot 的世界轴分量缩放。
                    //
                    // A1 层级：pivot（dragStartEntityPos）+ q（dragStartEntityRot）+
                    // off 全在**世界**空间（snap.worldStart 是 follower 世界起点）。
                    // 算出 follower 新**世界** position 后，再经各自父 worldMatrix 转
                    // local 写回。follower scale 仍乘 factorVec 写 local（保守，与
                    // primary 同款"父带旋转 non-uniform 不精确"限制）。root：
                    // worldStart==position、pivot 世界==local、父 identity → nPos 与
                    // 旧逐字节一致（零回归）。
                    const glm::quat q            = host.gizmo.dragStartEntityRot;
                    const glm::vec3 off          = snap.worldStart - host.gizmo.dragStartEntityPos;
                    const glm::vec3 nWorldPos    = host.gizmo.dragStartEntityPos + q * (factorVec * (glm::conjugate(q) * off));
                    const glm::mat4 fParentWorld = ParentWorldMatrix(*pWorld, snap.entity);
                    const glm::vec3 nPos         = glm::vec3(
                        glm::inverse(fParentWorld) * glm::vec4(nWorldPos, 1.0f));
                    const glm::vec3 nScale = snap.scale * factorVec;
                    pFTC->position         = nPos;
                    pFTC->scale            = nScale;
                    host.cmdStack.Push(std::make_unique<SetFieldValueCommand<glm::vec3>>(
                        snap.entity, std::string("Transform.position"),
                        snap.position, nPos, // oldVal = local 起点
                        MakeTransformPositionApply(&host, snap.entity)));
                    host.cmdStack.Push(std::make_unique<SetFieldValueCommand<glm::vec3>>(
                        snap.entity, std::string("Transform.scale"),
                        snap.scale, nScale,
                        MakeTransformScaleApply(&host, snap.entity)));
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

    // ---- 绘制：axis line + tip cube + center cube ----
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (drawList != nullptr)
    {
        for (const auto& ap : axes)
        {
            if (!ap.tipVisible)
            {
                continue;
            }
            const bool   highlight = (host.gizmo.hoveredAxis == ap.axis) || (host.gizmo.draggingAxis == ap.axis);
            const ImU32  col       = SettingsAxisColor(host.settings, ap.axis, highlight);
            const float  thickness = highlight ? host.settings.gizmoLineWidthScaleHighlight
                                               : host.settings.gizmoLineWidthScaleIdle;
            const ImVec2 a{projOrigin->screen.x, projOrigin->screen.y};
            const ImVec2 b{ap.tipScreen.x, ap.tipScreen.y};
            drawList->AddLine(a, b, col, thickness);

            // tip 立方体（屏幕空间方块；不做透视）
            drawList->AddRectFilled(
                ImVec2(ap.tipScreen.x - kCubeHalfSizePx, ap.tipScreen.y - kCubeHalfSizePx),
                ImVec2(ap.tipScreen.x + kCubeHalfSizePx, ap.tipScreen.y + kCubeHalfSizePx),
                col);
        }

        // 中心 uniform handle
        const bool  centerHi = (host.gizmo.hoveredAxis == Axis::Center) || (host.gizmo.draggingAxis == Axis::Center);
        const ImU32 colCtr   = SettingsAxisColor(host.settings, Axis::Center, centerHi);
        drawList->AddRectFilled(
            ImVec2(projOrigin->screen.x - kCenterHalfSizePx, projOrigin->screen.y - kCenterHalfSizePx),
            ImVec2(projOrigin->screen.x + kCenterHalfSizePx, projOrigin->screen.y + kCenterHalfSizePx),
            colCtr);
    }

    return host.gizmo.IsDragging() || (imageHovered && host.gizmo.IsHovered());
}
