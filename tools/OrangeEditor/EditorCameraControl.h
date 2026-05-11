#ifndef ORANGE_EDITOR_EDITOR_CAMERA_CONTROL_H
#define ORANGE_EDITOR_EDITOR_CAMERA_CONTROL_H

// 编辑器 viewport 相机控制 ——
//   * EditorCameraForward(yaw, pitch) —— 由欧拉角解 forward 单位向量
//     （右手系，pitch = 0 / yaw = 0 时朝 -Z）；
//   * UpdateEditorCameraFromInput(EditorCamera&) —— 在 Scene 面板的
//     Begin / End 之间调，读 ImGui 当前帧 IO 更新 position / yaw / pitch；
//   * BuildEditorCamera(EditorCamera, aspect) —— 由当前状态构造 Camera
//     组件值（Perspective + lookAt）；aspect ≤ 0 退化到 1 避免投影矩阵奇异；
//   * ApplyEditorCameraToWorld(EditorState&, aspect) —— 把 BuildEditorCamera
//     结果写到 World 里首个 Camera 组件上；找不到 no-op。Camera 组件不在
//     SceneSerialization 路径上，本写入对 Save / Load 透明。
//
// 控制约定（与 UpdateEditorCameraFromInput 内的输入捕获保持一致）：
//   * 鼠标右键拖动（hover Scene 面板时按下） —— 旋转 yaw / pitch
//   * 滚轮（hover Scene 面板时） —— 沿 forward 方向距离 + / -
//   * WASD（focus Scene 面板时） —— 相对相机朝向水平移动
//   * Q / E（focus Scene 面板时） —— 世界 Y 上 / 下

#include "EditorState.h"

#include <orange/engine/render/Camera.h>

#include <glm/vec3.hpp>

glm::vec3 EditorCameraForward(float yaw, float pitch) noexcept;

void UpdateEditorCameraFromInput(EditorState::EditorCamera& ec);

Orange::Engine::Render::Camera
BuildEditorCamera(const EditorState::EditorCamera& ec, float aspect);

void ApplyEditorCameraToWorld(EditorState& state, float aspect);

#endif  // ORANGE_EDITOR_EDITOR_CAMERA_CONTROL_H
