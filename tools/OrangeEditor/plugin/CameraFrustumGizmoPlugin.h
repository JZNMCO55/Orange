#ifndef ORANGE_EDITOR_PLUGIN_CAMERA_FRUSTUM_GIZMO_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_CAMERA_FRUSTUM_GIZMO_PLUGIN_H

// CameraFrustumGizmoPlugin —— v0.4 c5 milestone 第 3 个 IEditorGizmoPlugin
// case（c4 落 DirectionalLight / ParticleEmitter，c5 加 Camera frustum）。
//
// 职责：当选中实体挂着 `Render::Camera` component 时，在 viewport overlay
// 上画该相机的 frustum 线框（near + far rect 4 角 + 8 段连接边），帮助美
// 术 / 关卡设计师在编辑器内判断游戏运行时相机的视野范围。
//
// 现行实现（GAP-2026-05-15 落地后路径）：
//
//   * **projection** 直接来自 component.projection —— 由用户在 DemoWorld /
//     `Camera::Perspective` 设置，反映该相机真实的 fov / aspect / near /
//     far。Pipeline 改走 `SetEditorCameraOverride` 路径接收编辑器轨道相机，
//     ECS 内 Camera 组件**不再被每帧覆写**，所以 component 数据可信
//   * **view** 由 entity.Transform 推导（lookAt(position, position + rot *
//     -Z, rot * +Y)）—— Camera 看向 -Z 是 OpenGL / GLTF / Vulkan 工业惯例。
//     这条路径让 frustum 实时跟随用户摆位 entity，与 Unity / Lumix 同款
//     UX 约定
//
// 视觉：frustum 既反映 entity 摆位（Transform.position / rotation）也反映
// 用户在 Inspector 内设置的 fov / aspect / near / far（component.projection
// 派生）。

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
        void Draw(EditorHost&                                    host,
                  Orange::Engine::Entity                         entity,
                  const Orange::Editor::Schema::ComponentSchema& schema,
                  void*                                          component,
                  const GizmoContext&                            ctx) override;

        // HitTest 走基类默认（false）—— 与 c4 同款纯装饰 overlay。
    };

} // namespace Orange::Editor::Plugin

#endif // ORANGE_EDITOR_PLUGIN_CAMERA_FRUSTUM_GIZMO_PLUGIN_H
