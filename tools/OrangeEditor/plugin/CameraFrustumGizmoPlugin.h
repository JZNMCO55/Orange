#ifndef ORANGE_EDITOR_PLUGIN_CAMERA_FRUSTUM_GIZMO_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_CAMERA_FRUSTUM_GIZMO_PLUGIN_H

// CameraFrustumGizmoPlugin —— v0.4 c5 milestone 第 3 个 IEditorGizmoPlugin
// case（c4 落 DirectionalLight / ParticleEmitter，c5 加 Camera frustum）。
//
// 职责：当选中实体挂着 `Render::Camera` component 时，在 viewport overlay
// 上画该相机的 frustum 线框（near + far rect 4 角 + 8 段连接边），帮助美
// 术 / 关卡设计师在编辑器内判断游戏运行时相机的视野范围。
//
// ⚠ 当前限制（与 docs/engine-known-gaps.md
// GAP-2026-05-15-camera-editor-vs-runtime-separation 关联）：
//
// 引擎 `Render::Camera` 当前被 `ApplyEditorCameraToWorld` 每帧整体覆写为
// 编辑器轨道相机的 view / projection。如果直接读 component.view /
// component.projection 算 frustum，会得到"编辑器自己的视野"——frustum
// 线框与 viewport 自身边框重合，对用户无信息量。
//
// 临时方案（本 plugin 当前实现）：
//   * **fov / aspect / near / far 用 hardcode 默认值**（45° / 16:9 / 0.1
//     / 10.0）—— 引擎尚无 `CameraDesc { fov, aspect, near, far }` 概念
//   * **view 由 entity.Transform 推导**（lookAt(position, position + rot
//     * -Z, rot * +Y)）—— 绕开 component.view 被覆写的问题，让 frustum 朝
//     向跟随 entity 摆位
//   * 视觉上 frustum 反映 entity **位姿 + 朝向**，但不反映 component 真实
//     fov / aspect / near / far 数值（这些是 hardcode）
//
// 修复路径：等 GAP-2026-05-15 落地（候选：引擎引入 CameraDesc / 编辑器
// 引擎 viewport camera 分离 / Camera role 标签分类），plugin 切到读真实
// 数据。代码内 TODO 注释明示。

#include "IEditorGizmoPlugin.h"

namespace Orange::Editor::Plugin
{

class CameraFrustumGizmoPlugin : public IEditorGizmoPlugin
{
public:
    // 按 schema.typeName == "Camera" 字符串比较（与 RegisterCameraComponent
    // Schema 内 typeName 字面量一致）。
    bool CanHandle(const Orange::Editor::Schema::ComponentSchema& schema) const override;

    // 画 frustum 12 条边线（near rect 4 段 + far rect 4 段 + 连接 4 段）。
    // 颜色用青色，与 c4 ParticleEmitter spawn box 同色系——表示"这是个
    // 范围而不是单一对象"。
    void Draw(EditorHost&                                            host,
              Orange::Engine::Entity                                 entity,
              const Orange::Editor::Schema::ComponentSchema&         schema,
              void*                                                  component,
              const GizmoContext&                                    ctx) override;

    // HitTest 走基类默认（false）—— 与 c4 同款纯装饰 overlay。
};

}  // namespace Orange::Editor::Plugin

#endif  // ORANGE_EDITOR_PLUGIN_CAMERA_FRUSTUM_GIZMO_PLUGIN_H
