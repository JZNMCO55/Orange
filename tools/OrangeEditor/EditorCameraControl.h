#ifndef ORANGE_EDITOR_EDITOR_CAMERA_CONTROL_H
#define ORANGE_EDITOR_EDITOR_CAMERA_CONTROL_H

// 编辑器 viewport 相机控制（轨道模式）——
//   * UpdateEditorCameraFromInput(EditorCameraState&) —— 在 Scene 面板的
//     Begin / End 之间调，读 ImGui 当前帧 IO 更新 azimuth / elevation /
//     radius；
//   * BuildEditorCamera(EditorCameraState, aspect) —— 由球坐标构造 Camera
//     组件值（Perspective + lookAt pivot）；aspect ≤ 0 退化到 1 避免投影
//     矩阵奇异；
//   * ApplyEditorCameraToWorld(EditorHost&, aspect) —— 把 BuildEditorCamera
//     结果写到 World 里首个 Camera 组件上；找不到 no-op。
//
// 控制约定（与 UpdateEditorCameraFromInput 内的输入捕获保持一致）：
//   * 鼠标左键拖动（hover Scene 面板时按下） —— 轨道旋转 azimuth / elevation
//   * 滚轮（hover Scene 面板时） —— 缩放 radius（推近 / 拉远）
//
// v0.2.5 整骨：原 EditorState::EditorCamera 嵌套类型被提为顶层
// EditorCameraState（context/EditorCameraState.h）。

#include "EditorHost.h"

#include <orange/engine/render/Camera.h>

// 注：函数参数从 EditorCameraState& 改为 EditorHost& —— 实现内需要读
// `host.gizmo.IsHovered() / IsDragging()` 状态来避免与 gizmo 抢 LMB
// 拖动。读上一帧的 gizmo 状态是预期的：实际 UX 下 hover handle → click
// 至少跨多帧（60fps 单帧 16ms < 人类反应时间），上一帧 hover 状态正确
// 反映"按下 LMB 那一刻"。
void UpdateEditorCameraFromInput(EditorHost& host);

Orange::Engine::Render::Camera
BuildEditorCamera(const EditorCameraState& ec, float aspect);

void ApplyEditorCameraToWorld(EditorHost& host, float aspect);

#endif  // ORANGE_EDITOR_EDITOR_CAMERA_CONTROL_H
