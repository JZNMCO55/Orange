#ifndef ORANGE_EDITOR_CONTEXT_EDITOR_GIZMO_STATE_H
#define ORANGE_EDITOR_CONTEXT_EDITOR_GIZMO_STATE_H

// EditorGizmoState —— viewport overlay gizmo 的 hover / drag 跨帧状态。
//
// v0.4 c2 引入（translate-only）；v0.4 c3 扩 rotate / scale + W/E/R 模式
// 切换。Axis::None / W/E/R 之外的轴枚举（如中心 uniform-scale handle）
// 用 Axis::Center。
//
// 设计纪律（与 v0.2.5 commit 1 拆 4 个 sub-context 同款）：
//   "viewport gizmo 跨帧状态" 是第 5 个 editor-global 域 —— 按 CLAUDE.md
//   "OrangeEditor 架构纪律"节"没合适 context 就先拆 context"，本期同步
//   落 EditorGizmoState 而非塞回 EditorSelection / EditorSceneContext。
//
// 设计参考：vendor/LumixEngine/src/editor/gizmo.cpp 内 `Gizmo` 类持有
// 的 m_active / m_axis / m_drag_start_pos 等成员；OrangeEditor 走数据
// 与逻辑分离 —— 状态在本 struct，逻辑在 EditorTranslateGizmo.cpp /
// EditorRotateGizmo.cpp / EditorScaleGizmo.cpp。

#include <orange/engine/scene/Entity.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <utility>
#include <vector>

struct EditorGizmoState
{
    enum class Mode : std::uint8_t
    {
        Translate = 0,  // W
        Rotate    = 1,  // E
        Scale     = 2,  // R
    };

    enum class Axis : std::uint8_t
    {
        None   = 0,
        X      = 1,
        Y      = 2,
        Z      = 3,
        Center = 4,  // Scale gizmo 中心 uniform-scale handle；其他 mode 不用
    };

    // 变换参考系（gap 报告 §3 P0）。X 键切换。默认 World = 历史行为（零回归）。
    // 作用 Translate / Rotate（轴向 = World 全局 / Local 跟实体 rotation）；
    // Scale 历史即 local（写死 entityRot*axis），不受本枚举影响（保持不变）。
    enum class Space : std::uint8_t
    {
        World = 0,
        Local = 1,
    };
    Space space = Space::World;

    // 当前 gizmo 模式（W / E / R 切换）。键盘快捷键由 ScenePanel 内 ImGui
    // hotkey 探测路径写本字段；切换时若 draggingAxis != None 暂保持模式不变
    // 到 LMB 释放，避免 mid-drag 切模式撞坑。
    Mode mode = Mode::Translate;

    // viewport 工具栏的"Gizmos on/off"总开关（v0.4 c5 落地）。false 时
    // 所有 gizmo（内置 Transform translate/rotate/scale + plugin overlay）
    // 都既不绘制也不响应输入——但模式 / hovered / dragging 等内部状态保
    // 持，让用户切回 visible 时恢复一致 UX。
    //
    // 与 Play Mode 禁用路径正交：Play Mode 由 host.scene.playState 控制，
    // 强制禁用；visible 是用户主动控制的"我不想看 gizmo"开关，Edit Mode
    // 期内才有意义。
    bool visible = true;

    // 当前 hover 的 handle —— 每帧 hit-test 重置（None = 鼠标不在任何
    // handle 上）。draggingAxis != None 期间 hoveredAxis 强制等于
    // draggingAxis（拖动期不参与 hit-test）。
    Axis hoveredAxis = Axis::None;

    // 当前正在拖动的 handle —— None = 没有拖动中。draggingAxis != None
    // 期间 ScenePanel 跳过 picking 触发，避免 LMB 释放时既拖完 gizmo
    // 又触发 picking。
    Axis draggingAxis = Axis::None;

    // ---- 通用拖动起点（所有 mode 共用）----------------------------------
    // 实体在按下 LMB 那一帧的 transform 快照——translate 用 position 作
    // oldValue / 增量 baseline；rotate 用 rotation 作 axisAngle 累乘的
    // baseline；scale 用 scale 作乘数 baseline。
    glm::vec3 dragStartEntityPos   = glm::vec3(0.0f);
    glm::quat dragStartEntityRot   = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 dragStartEntityScale = glm::vec3(1.0f);

    // ---- Translate 专用 ------------------------------------------------
    // 鼠标 ray 与拖动轴线的最近点 world 坐标（按下 LMB 那一帧）。每帧用
    // 最新鼠标 ray 重算最近点，与本字段相减得到沿轴位移增量。
    glm::vec3 dragStartHitOnAxis = glm::vec3(0.0f);

    // ---- A1 层级（world→local）：primary 拖动起点的 **local** position --
    // dragStartEntityPos 自 A1 起存**世界**起点（gizmo 画在 mesh 世界位置 + 沿
    // 世界轴 drag）；但写回 TransformComponent.position 是 **local**，命令的
    // oldValue（undo 目标）必须是拖动起点的 local。两者对 root / 原点父实体
    // 相等（world==local）→ 零回归；仅 parent 到非原点实体时分叉。
    glm::vec3 dragStartEntityLocalPos = glm::vec3(0.0f);

    // ---- A1 层级（world→local）：rotate 拖动起点的 **world** rotation ----
    // dragStartEntityRot 存的是拖动起点的 **local** rotation（命令 oldVal / undo
    // 目标）。rotate gizmo 的圆环画在 mesh 世界朝向、drag deltaQ 在世界空间累乘，
    // 故需另存拖动起点的世界旋转作 drag 基准：targetWorldRot = deltaQ *
    // dragStartEntityWorldRot，写回前经 inverse(parentWorldRot) 转 local。对
    // root / 原点父：worldRot==localRot → 两字段相等 → 零回归。
    glm::quat dragStartEntityWorldRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    // ---- 多选群组变换专用 ----------------------------------------------
    // 按下 LMB 那一帧，除 primary 外其余选中实体的 transform 快照。群组
    // translate/rotate/scale 都以 primary 位置为 pivot，让 follower 随 primary
    // 刚体联动（translate 用 position；rotate 用 position+rotation；scale 用
    // position+scale）。空 = 单选（无群组）。绕 pivot 的数学见 EditorGroupTransform.h。
    struct GroupDragSnapshot
    {
        Orange::Engine::Entity entity;
        glm::vec3              position = glm::vec3(0.0f);   // 拖动起点 local position（命令 oldVal）
        glm::quat              rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3              scale    = glm::vec3(1.0f);
        // A1 层级：拖动起点的 **world** position。群组 translate 的 groupDelta
        // 是世界位移，follower 新世界 = worldStart + groupDelta，再经各自
        // parentWorld 转 local 写回。root/原点父：worldStart==position → 零回归。
        glm::vec3              worldStart = glm::vec3(0.0f);
        // A1 层级：拖动起点的 **world** rotation。群组 rotate 的 deltaQ 在世界
        // 空间累乘，follower 新世界 rot = deltaQ * worldRot，再经各自父 worldRot
        // 转 local 写回。root/原点父：worldRot==rotation → 零回归。
        glm::quat              worldRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    };
    std::vector<GroupDragSnapshot> dragStartAdditional;

    // ---- Rotate 专用 ---------------------------------------------------
    // 拖动起点：mouse ray 与 axis 平面交点相对 entity 的向量在 axis 平
    // 面内的投影方向（单位向量）。每帧重算 → 与本字段求 signedAngle =
    // 角度增量；newRot = axisAngle(axisDir, deltaAngle) * dragStartRot。
    glm::vec3 dragStartRotateRef = glm::vec3(1.0f, 0.0f, 0.0f);

    // ---- Scale 专用 ----------------------------------------------------
    // 拖动起点：mouse ray 与 axis line 最近点相对 dragStartEntityPos 的
    // 沿轴 signed 距离（dot(hit-pos, axisDir)）。每帧 currentSigned /
    // dragStartScaleRefSigned = 沿该轴的乘数 factor（clamp 防 NaN / 极
    // 端值）。Axis::Center（uniform scale）走屏幕 dy / 100 启发式，不用
    // 本字段。
    float dragStartScaleRefSigned = 0.0f;

    // ---- Scale Center（uniform）专用 ----------------------------------
    // 拖动起点：鼠标屏幕坐标（按下 LMB 那一帧）。每帧 (currentMouseY -
    // dragStartMouseScreen.y) / 100 + 1 = uniform factor（Y 向上拖 = 缩
    // 小 / Y 向下拖 = 放大，与 Lumix 同款屏幕约定）。
    glm::vec3 dragStartMouseScreen = glm::vec3(0.0f, 0.0f, 0.0f);

    bool IsDragging() const noexcept { return draggingAxis != Axis::None; }
    bool IsHovered()  const noexcept { return hoveredAxis  != Axis::None; }
};

#endif  // ORANGE_EDITOR_CONTEXT_EDITOR_GIZMO_STATE_H
