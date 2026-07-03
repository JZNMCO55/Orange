#ifndef ORANGE_EDITOR_EDITOR_TRANSLATE_GIZMO_H
#define ORANGE_EDITOR_EDITOR_TRANSLATE_GIZMO_H

// EditorTranslateGizmo —— Scene viewport 内 3 轴 world-space translate
// gizmo（X 红 / Y 绿 / Z 蓝）。
//
// v0.4 c2 落地：translate-only；c3 扩 rotate / scale + W/E/R 模式切换。
// 本期固定 world 空间（local 空间切换 v1.x 触发）。
//
// 调用约定（与 ScenePanel.cpp::DrawScenePanel 集成）：
//   * 在 ImGui::Image(scene viewport) 之后、picking input check 之前调；
//   * 入参：viewport 面板的 image item 左上角屏幕坐标 + image 尺寸 +
//     aspect（与 BuildEditorCamera 一致）；
//   * 返回：true 表示 gizmo 接管了本帧 LMB（hover handle 或正在拖动）
//     —— ScenePanel 应在 picking 触发判定前 short-circuit 跳过；
//   * 内部消费 host.gizmo + host.selection.selectedEntity + host.cmd
//     Stack + host.scene.pWorld + host.camera + host.scene.playState。
//
// 设计参考：vendor/LumixEngine/src/editor/gizmo.cpp Translate 段
//   * handle hit-test：world axis line 端点投影到屏幕，2D 点到线段最短
//     距离 < 阈值 px（屏幕像素阈值比 world-space ray-line 距离阈值更
//     直观、与 ImGui 像素 UI 对齐）；
//   * drag math：mouse ray 与 axis line 的最近点 → 与拖动起点最近点之
//     差 = 沿轴位移增量（增量直接落 entity.position）；
//   * draw：3 条 axis line + arrow head triangle 走 ImDrawList，颜色 X
//     红 / Y 绿 / Z 蓝；hovered / dragging 轴走更亮颜色 + 加粗。
//
// CommandStack 联动（消费 v0.2.5 c13 的 BeginGroup / EndGroup API；本
// gizmo 是该 API 首批真实 caller）：
//   * LMB 按下 hover 轴 → BeginGroup("Translate Drag", MergeMode::Ends)
//     + 记录 dragStartEntityPos + dragStartHitOnAxis；
//   * 拖动期间每帧（newPos != currentPos 时）Push SetFieldValueCommand
//     <Vec3>(entity, "Transform.position", oldVal=dragStartEntityPos,
//     newVal=newPos)；intra-group coalesce 把多帧合并为单条；
//   * LMB 释放 → EndGroup。一次完整拖动 = 一条 Undo 撤销（回到 drag
//     Start 那一刻的 position）。
//
// Play Mode 禁用：scene.playState != PlayState::Edit 时 gizmo 既不绘
// 制也不响应输入（与 InspectorPanel 的 BeginDisabled 行为对偶）。拖动
// 期间撞上 playState 切换 / 选中实体丢失 / Transform 被 Remove → 立刻
// EndGroup + 重置 state，避免 pending group 卡死。

#include "EditorHost.h"

#include <glm/vec2.hpp>

// 在 Scene 面板的 ImGui::Image 之后调。
//
// 入参：
//   * host                       —— 拿 selection / camera / cmdStack /
//                                    world / gizmo state；
//   * viewportImageOriginScreen  —— ImGui::GetItemRectMin() 拿到的
//                                    viewport image 左上角屏幕坐标；
//   * viewportImageSize          —— viewport image 像素尺寸；
//   * aspect                     —— 与 BuildEditorCamera 一致的 aspect。
//
// 返回 true 表示 gizmo 接管了本帧 LMB（hover handle 或正在拖动）；
// caller（ScenePanel）应跳过 picking 触发 path。
bool DrawAndHandleTranslateGizmo(EditorHost& host,
                                 glm::vec2   viewportImageOriginScreen,
                                 glm::vec2   viewportImageSize,
                                 float       aspect);

#endif // ORANGE_EDITOR_EDITOR_TRANSLATE_GIZMO_H
