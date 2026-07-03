#ifndef ORANGE_EDITOR_COLLIDER_VERTEX_EDIT_H
#define ORANGE_EDITOR_COLLIDER_VERTEX_EDIT_H

// ColliderVertexEdit —— Polygon / EdgeChain collider 顶点的 viewport 内交互
// 编辑（engine-known-gaps GAP-2026-05-21）。
//
// 在 ScenePanel 的 viewport 渲染（ImGui::Image）之后、内置 gizmo / picking
// 之前调用：当 host.colliderEdit.active 时接管鼠标交互，绘制顶点 handle +
// 处理选 / 拖 / 加 / 删，并把每个动作走 CommandStack（拖拽 coalesce）。
//
// 顶点世界变换与 ColliderDebugDraw.cpp 保持一致（local.xy 经 entity
// quaternion 旋转 + position 平移，z 取 entity.position.z）—— 保证 viewport
// 里看到的 wireframe 与可拖拽的 handle 像素级吻合。
//
// 几何 / 反投影数学复用 OrangeEditor::Internal::GizmoMath（与内置 gizmo 同款
// ScreenToWorldRay / RayPlaneIntersect / ProjectWorldToScreen），不引入新的
// 反投影实现。

#include <glm/vec2.hpp>

struct EditorHost;

namespace Orange::Editor
{

    // 处理 collider 顶点编辑交互 + 绘制 handle。
    //
    // 入参（与 ScenePanel 内 gizmo / picking 调用约定一致）：
    //   * imageOrigin / imageSize —— viewport ImGui::Image 在屏幕坐标系的左上角
    //                                与尺寸（NDC ↔ 屏幕像素映射用）
    //   * aspect                  —— 当前面板宽高比（BuildEditorCamera 取 projection）
    //
    // 返回值：
    //   * true  —— 本帧 collider edit 子模式 active（caller 应跳过内置 gizmo +
    //              viewport picking，避免鼠标双重消费）
    //   * false —— 未激活（host.colliderEdit.active == false 或 entity / collider
    //              已失效自动 Reset）；caller 走正常 gizmo / picking 路径
    bool HandleColliderVertexEdit(EditorHost& host,
                                  glm::vec2   imageOrigin,
                                  glm::vec2   imageSize,
                                  float       aspect);

} // namespace Orange::Editor

#endif // ORANGE_EDITOR_COLLIDER_VERTEX_EDIT_H
