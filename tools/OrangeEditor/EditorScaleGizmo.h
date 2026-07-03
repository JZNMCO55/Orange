#ifndef ORANGE_EDITOR_EDITOR_SCALE_GIZMO_H
#define ORANGE_EDITOR_EDITOR_SCALE_GIZMO_H

// EditorScaleGizmo —— Scene viewport 内 3 轴 + 中心 uniform handle 的
// world-space scale gizmo（X 红 / Y 绿 / Z 蓝 + 白色中心立方体）。
//
// v0.4 c3 落地。固定 world 空间。
//
// 调用约定（与 translate / rotate 对偶）：仅当 host.gizmo.mode == Scale
// 时由 ScenePanel 分派；入参 / 返回值同款。
//
// 设计：
//   * 3 个 axis handle 在 entity 位置沿 ±X/±Y/±Z 摆出 axis line + tip 小立
//     方体（视觉与 translate 的 arrow head 区分：scale 用方块、translate
//     用三角箭头）；
//   * 1 个中心 uniform handle = entity 屏幕位置的白色小立方体；拖它走
//     uniform scale（所有轴同步乘 factor）；
//   * hit-test：单轴用 2D 点-线段距离阈值（同 translate）；中心 handle 用
//     2D 点到中心位置距离 < kCenterHitRadiusPx；
//   * 单轴 drag math：沿 axisDir 的 signed 距离 ratio → 单轴 scale 乘
//     dragStartScale[axis] * factor，其他两轴保持不变；
//   * 中心 uniform drag math：屏幕 (dx + dy) / 100 + 1 = factor（Lumix-
//     like），lambda 内乘到全 3 轴；
//   * factor clamp [0.01, 100] 防止退化 + NaN（dragStart signed 距离 ≈ 0
//     时本帧跳过 update）。
//
// CommandStack 联动：
//   * 按下 → BeginGroup("Scale Drag", MergeMode::Ends) + 记录 dragStart
//     EntityScale + dragStartScaleRefSigned / dragStartMouseScreen；
//   * 拖动每帧 Push SetFieldValueCommand<Vec3>(entity, "Transform.scale",
//     oldVal=dragStartEntityScale, newVal=newScale)；intra-group coalesce；
//   * 释放 → EndGroup。

#include "EditorHost.h"

#include <glm/vec2.hpp>

bool DrawAndHandleScaleGizmo(EditorHost& host,
                             glm::vec2   viewportImageOriginScreen,
                             glm::vec2   viewportImageSize,
                             float       aspect);

#endif // ORANGE_EDITOR_EDITOR_SCALE_GIZMO_H
