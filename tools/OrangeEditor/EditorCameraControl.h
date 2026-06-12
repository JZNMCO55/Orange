#ifndef ORANGE_EDITOR_EDITOR_CAMERA_CONTROL_H
#define ORANGE_EDITOR_EDITOR_CAMERA_CONTROL_H

// 编辑器 viewport 相机控制（轨道模式）——
//   * UpdateEditorCameraFromInput(EditorCameraState&) —— 在 Scene 面板的
//     Begin / End 之间调，读 ImGui 当前帧 IO 更新 azimuth / elevation /
//     radius；
//   * BuildEditorCamera(EditorCameraState, aspect) —— 由球坐标构造 Camera
//     组件值（Perspective + lookAt pivot）；aspect ≤ 0 退化到 1 避免投影
//     矩阵奇异；结果由调用方 push 给 `Pipeline::SetEditorCameraOverride`
//     （GAP-2026-05-15 落地后路径——ECS 内 Camera 组件不再被覆写）。
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

// FrameSelectedCamera —— 把轨道相机聚焦到当前选中 entity：pivot 移到该
// entity 的 Renderable mesh 世界 AABB 中心，radius 拉到能在垂直 FOV 内完整
// 看到包围球的距离，并按物体尺度重算 zNear / zFar（避免 Duck 165 单位被
// zFar=100 远裁、Avocado 0.04 单位太小看不见）。
//
// 解决"导入模型尺寸 / 位置千差万别，默认 pivot(原点)+radius 看不到 / 被视锥
// 裁剪"。F 键触发（EditorKeybindings::frameSelected，对齐 Unity / Unreal）。
//
// 返回 true 表示成功聚焦；以下情形 no-op 返回 false：无选中 / World 或 Asset
// 注册表缺失 / 选中 entity 无 Transform / 无 Renderable mesh（或 mesh 为空 /
// 退化为一点）。无 Renderable 但有 Transform 时退化为"对准 Transform 位置 +
// 默认 radius"（聚焦灯光 / 空 entity 仍居中），此分支返回 true。
bool FrameSelectedCamera(EditorHost& host);

// FrameEntityCamera —— 把轨道相机聚焦到**指定** entity（不依赖当前选中，不改
// selection）。语义与 FrameSelectedCamera 完全一致（同走 FrameEntitiesCamera 单
// entity 路径），只是对象由 caller 显式给出。MCP frame_entity(guid) 复用此入口：
// AI 对准任意实体看构图，不强制改用户选中。无效 entity / World 缺失 → false。
bool FrameEntityCamera(EditorHost& host, Orange::Engine::Entity entity);

// 全场景 Frame —— 把相机拉到能看全场景所有几何的距离（合并所有带 Transform
// 的 entity 世界 bounds）。Home 键 / View 菜单触发，对齐 Unity/Unreal "Frame All"。
// 空场景 / World 缺失 → no-op 返回 false。FrameSelected 的全场景版（共用
// FrameEntitiesCamera）。
bool FrameAllCamera(EditorHost& host);

#endif  // ORANGE_EDITOR_EDITOR_CAMERA_CONTROL_H
