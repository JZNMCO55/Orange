#ifndef ORANGE_EDITOR_PLUGIN_GIZMO_CONTEXT_H
#define ORANGE_EDITOR_PLUGIN_GIZMO_CONTEXT_H

// GizmoContext —— viewport gizmo plugin Draw / HitTest 钩子的入参数据包。
//
// 设计意图（与 IEditorGizmoPlugin.h "GizmoContext 前向声明纪律"段配套）：
// 把 plugin 渲染 / hit-test 需要的 viewport 状态打包成单一引用入参，避
// 免 plugin 接口因为"多挂一两个字段"反复 bump 签名。后续如果需要扩字段
// （hit-test handle 写槽 / mouse ray / hovered ghost state 等）直接在本
// 结构追加，**不**破坏已注册 plugin 的编译——fields 增加不变更 plugin
// .h 引用的字段语义即可保编译兼容（参 v0.2.5 c12 头注释"收益"段）。
//
// v0.4 c4 落地：仅含 Draw 钩子所需的 4 个字段（viewProj 矩阵 / image 起
// 点尺寸 / ImDrawList 句柄）。HitTest 钩子相关字段（mouseRay / handle 写
// 槽）在 v0.4+ 真有 gizmo plugin 想做交互式 hit-test 时再加——本期内置
// Transform / Light / Particle gizmo 都不通过 plugin HitTest 路径走：
//   * 内置 Translate / Rotate / Scale 走专门 EditorTranslateGizmo /
//     EditorRotateGizmo / EditorScaleGizmo（c2 / c3，**不**走 plugin）
//   * c4 引入的 Light / ParticleEmitter gizmo plugin 是**纯装饰 overlay**
//     （DirectionalLight direction 箭头可视化 / ParticleEmitter spawn
//     box + velocity 向量可视化），用户改值仍通过 Inspector，gizmo 不
//     接管 LMB 拖动

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

struct ImDrawList;

namespace Orange::Editor::Plugin
{

    struct GizmoContext
    {
        // viewport 的 view * projection 合成矩阵（caller 每帧从 BuildEditor
        // Camera 拿）。plugin 用 GizmoMath::ProjectWorldToScreen 投影 world
        // 点到屏幕。
        glm::mat4 viewProj;

        // viewport image item 在屏幕坐标系内的左上角 / 尺寸（用于 NDC → 屏
        // 幕像素的位姿映射）。
        glm::vec2 imageOrigin;
        glm::vec2 imageSize;

        // 要往哪个 ImDrawList 提交线段 / 多边形（典型 = ImGui::GetWindowDraw
        // List() 返回值；caller 在调度前拿好，避免 plugin 自己额外引 ImGui 头）。
        ImDrawList* drawList = nullptr;
    };

} // namespace Orange::Editor::Plugin

#endif // ORANGE_EDITOR_PLUGIN_GIZMO_CONTEXT_H
