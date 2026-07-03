#ifndef ORANGE_EDITOR_EDITOR_PICKING_H
#define ORANGE_EDITOR_EDITOR_PICKING_H

// EditorPicking —— viewport 点击命中实体的工具集。
//
// 流程：屏幕坐标 → NDC → world ray → 遍历 (Transform, Renderable) 实体的
// AABB → ray-AABB 测试 → 取 t 最小（最近）的命中。结果写回
// EditorSelection.selectedEntity，Entity Tree 通过既有 selection 联动机
// 制自动高亮（无需双向写）。
//
// 设计要点：
//   * Picking 不是每帧执行，由用户 viewport 点击触发——成本宽容；不缓
//     存 per-mesh AABB，每次扫一遍 mesh 顶点（几十 entity × 几百顶点
//     完全可接受）
//   * world 矩阵走 T*R*S，与 src/render/RenderScene.cpp::ComposeWorld
//     Matrix 同款（hierarchy 当前不参与渲染坐标变换，picking 一并对齐）
//   * Vulkan NDC：y-down + z∈[0,1]，与 ImGui 屏幕坐标 y-down 同向——
//     反投影时不再额外翻转 y
//   * 命中失败返回 Entity::Invalid()——caller（典型 ScenePanel）按需决
//     定是否在"空白点击"上清除当前选中
//
// 参考：vendor/LumixEngine/src/editor/render_interface.h::castRayAt 同
// 款几何，我方在编辑器侧自包含实现（不污染 OrangeRender / OrangeEngine
// 公共面，符合 engine-known-gaps 跨 session 工作流）。

#include "EditorHost.h"

#include <orange/engine/scene/Entity.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// 在 viewport 内点击屏幕坐标 (ndcX, ndcY) 时计算命中的最近实体。
//
// 入参：
//   * host             —— EditorHost 拿 World + camera state
//   * ndc              —— [-1, 1] 区间的 viewport 归一化坐标（已减去面
//                          板原点 + 除面板尺寸 + *2-1）；y 已与 Vulkan NDC
//                          对齐（向下为正）
//   * aspect           —— 当前面板宽高比；用于 BuildEditorCamera 拿
//                          projection 矩阵
//
// 返回 Entity::Invalid() 表示未命中任何 entity（典型：点空白处）。
Orange::Engine::Entity
PickEntityAt(EditorHost& host, glm::vec2 ndc, float aspect);

// 屏幕 NDC → 与水平地面平面 y=groundY 的世界交点（拖资源到 viewport 空白处时
// 的落点）。与 PickEntityAt 同款反投影（BuildEditorCamera + invVP），但求射线与
// 地面平面交点而非实体 AABB。射线平行地面 / 朝上不相交（或交点在相机后方）时
// 退化到"相机沿射线前方固定距离"的点，保证总有一个合理落点。
glm::vec3
ScreenRayToGround(EditorHost& host, glm::vec2 ndc, float aspect,
                  float groundY = 0.0f);

#endif // ORANGE_EDITOR_EDITOR_PICKING_H
