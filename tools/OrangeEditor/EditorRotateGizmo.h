#ifndef ORANGE_EDITOR_EDITOR_ROTATE_GIZMO_H
#define ORANGE_EDITOR_EDITOR_ROTATE_GIZMO_H

// EditorRotateGizmo —— Scene viewport 内 3 圆环 world-space rotate gizmo
// （绕 X 红 / Y 绿 / Z 蓝轴旋转）。
//
// v0.4 c3 落地。固定 world 空间（local 切换 v1.x 触发，对偶 translate）。
//
// 调用约定（与 ScenePanel.cpp::DrawScenePanel 集成）：
//   * 在 ImGui::Image 之后、picking input check 之前调；
//   * 仅当 host.gizmo.mode == Rotate 时由 ScenePanel 分派（W/E/R 模式互斥）；
//   * 入参 / 返回值与 DrawAndHandleTranslateGizmo 对偶；
//   * 内部消费 host.gizmo（draggingAxis / dragStartEntityRot /
//     dragStartRotateRef）+ host.selection + host.cmdStack + host.scene
//     + host.camera。
//
// 设计参考：vendor/LumixEngine/src/editor/gizmo.cpp Rotate 段
//   * 3 个圆环位于 entity world 位置；圆环平面法向量 = world X/Y/Z；半径
//     = worldUnitsPerHandle（与 translate axis 长度同款，让 W/E 切换时
//     视觉大小一致）；
//   * 绘制：32 段 polyline 投影到屏幕，连成圆环；
//   * hit-test：每段 polyline 屏幕距离 < kHitThresholdPx（同 translate
//     像素阈值），最近的环 = hovered；
//   * drag math：mouse ray 与 axis plane（plane normal = axis，plane
//     origin = entity 位置）的交点 P → currentRef = normalize(P -
//     entity)；与 dragStartRotateRef 求 signed angle（atan2 路径，
//     gimbal-safe）→ axisAngle(axisDir, deltaAngle) * dragStartEntityRot
//     = newRotation。
//
// CommandStack 联动：
//   * LMB 按下 hover 圆环 → BeginGroup("Rotate Drag", MergeMode::Ends)
//     + 记录 dragStartEntityRot + dragStartRotateRef；
//   * 拖动每帧（newRot != currentRot 时）Push SetFieldValueCommand
//     <glm::quat>(entity, "Transform.rotation", oldVal=dragStartEntity
//     Rot, newVal=newRot)；intra-group coalesce 合并多帧；
//   * 拖动 apply lambda 同步 invalidate `selection.transformEulerCache
//     Entity`，让 Inspector 下一帧 Quat case 从最新 quat 重算 Euler 显
//     示（与 SchemaInspector.cpp Quat case 的 invalidate 路径同款）；
//   * LMB 释放 → EndGroup。一次拖动 = 一条 Undo 撤销。
//
// Play Mode 禁用 + 拖动期间安全终止：与 translate 同款。

#include "EditorHost.h"

#include <glm/vec2.hpp>

bool DrawAndHandleRotateGizmo(EditorHost& host,
                              glm::vec2   viewportImageOriginScreen,
                              glm::vec2   viewportImageSize,
                              float       aspect);

#endif // ORANGE_EDITOR_EDITOR_ROTATE_GIZMO_H
