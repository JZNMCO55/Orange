#ifndef ORANGE_EDITOR_EDITOR_CAMERA_CONTROL_H
#define ORANGE_EDITOR_EDITOR_CAMERA_CONTROL_H

// 编辑器 viewport 相机控制（轨道模式）——
//   * UpdateEditorCameraFromInput(EditorCamera&) —— 在 Scene 面板的
//     Begin / End 之间调，读 ImGui 当前帧 IO 更新 azimuth / elevation /
//     radius；
//   * BuildEditorCamera(EditorCamera, aspect) —— 由球坐标构造 Camera
//     组件值（Perspective + lookAt pivot）；aspect ≤ 0 退化到 1 避免投影
//     矩阵奇异；
//   * ApplyEditorCameraToWorld(EditorState&, aspect) —— 把 BuildEditorCamera
//     结果写到 World 里首个 Camera 组件上；找不到 no-op。
//
// 控制约定（与 UpdateEditorCameraFromInput 内的输入捕获保持一致）：
//   * 鼠标左键拖动（hover Scene 面板时按下） —— 轨道旋转 azimuth / elevation
//   * 滚轮（hover Scene 面板时） —— 缩放 radius（推近 / 拉远）

#include "EditorState.h"

#include <orange/engine/render/Camera.h>

void UpdateEditorCameraFromInput(EditorState::EditorCamera& ec);

Orange::Engine::Render::Camera
BuildEditorCamera(const EditorState::EditorCamera& ec, float aspect);

void ApplyEditorCameraToWorld(EditorState& state, float aspect);

#endif  // ORANGE_EDITOR_EDITOR_CAMERA_CONTROL_H
