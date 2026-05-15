#ifndef ORANGE_EDITOR_CONTEXT_EDITOR_GIZMO_STATE_H
#define ORANGE_EDITOR_CONTEXT_EDITOR_GIZMO_STATE_H

// EditorGizmoState —— viewport overlay gizmo 的 hover / drag 跨帧状态。
//
// v0.4 c2 引入：本期仅承载 Translate gizmo 状态（hoveredAxis /
// draggingAxis / 拖动起点缓存）。c3 扩 Rotate / Scale 时同结构追加字
// 段（增 mode enum + plane drag start 等），不拆新 context。
//
// 设计纪律（与 v0.2.5 commit 1 拆 4 个 sub-context 同款）：
//   "viewport gizmo 跨帧状态" 是第 5 个 editor-global 域 —— 按 CLAUDE.md
//   "OrangeEditor 架构纪律"节"没合适 context 就先拆 context"，本期同步
//   落 EditorGizmoState 而非塞回 EditorSelection / EditorSceneContext。
//
// 设计参考：vendor/LumixEngine/src/editor/gizmo.cpp 内 `Gizmo` 类持有
// 的 m_active / m_axis / m_drag_start_pos 等成员；OrangeEditor 走数据
// 与逻辑分离 —— 状态在本 struct，逻辑在 EditorTranslateGizmo.cpp（c3
// 起 + EditorRotateGizmo.cpp / EditorScaleGizmo.cpp）。

#include <glm/vec3.hpp>

#include <cstdint>

struct EditorGizmoState
{
    enum class Axis : std::uint8_t
    {
        None = 0,
        X    = 1,
        Y    = 2,
        Z    = 3,
    };

    // 当前 hover 的 axis handle —— 每帧 hit-test 重置（None = 鼠标不在
    // 任何 handle 上）。draggingAxis != None 期间 hoveredAxis 强制等于
    // draggingAxis（拖动期不参与 hit-test）。
    Axis hoveredAxis = Axis::None;

    // 当前正在拖动的 axis handle —— None = 没有拖动中。draggingAxis !=
    // None 期间 ScenePanel 跳过 picking 触发，避免 LMB 释放时既拖完
    // gizmo 又触发 picking。
    Axis draggingAxis = Axis::None;

    // 拖动起点：实体在按下 LMB 那一帧的 position（用作 SetFieldValue
    // Command 的 oldValue + 增量计算 baseline）。
    glm::vec3 dragStartEntityPos = glm::vec3(0.0f);

    // 拖动起点：鼠标 ray 与拖动轴线的最近点 world 坐标（按下 LMB 那
    // 一帧计算）。每帧用最新鼠标 ray 重算最近点，与本字段相减得到沿
    // 轴位移增量。
    glm::vec3 dragStartHitOnAxis = glm::vec3(0.0f);

    bool IsDragging() const noexcept { return draggingAxis != Axis::None; }
    bool IsHovered()  const noexcept { return hoveredAxis  != Axis::None; }
};

#endif  // ORANGE_EDITOR_CONTEXT_EDITOR_GIZMO_STATE_H
