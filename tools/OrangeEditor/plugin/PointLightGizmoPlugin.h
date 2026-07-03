#ifndef ORANGE_EDITOR_PLUGIN_POINT_LIGHT_GIZMO_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_POINT_LIGHT_GIZMO_PLUGIN_H

// PointLightGizmoPlugin —— viewport overlay 装饰 gizmo（与 DirectionalLight
// GizmoPlugin / ParticleEmitterGizmoPlugin 同款"plugin 只画 overlay，不接
// 管 LMB 拖动"模式）。
//
// 职责：选中实体挂 PointLight 时，画一个 entity world 位置的黄色"灯泡"
// 图标 + 围绕该位置的 range 边界圆环（XY 平面投影；3D 球简化为 2D 圆，
// 让用户在 viewport 内能看到光照影响范围）。
//
// 不画 wire sphere —— 真 3D wire sphere 需要 DebugDrawScene 通路 + camera
// 截面，体量更大；range 边界圆环已能给出"光能照多远"的视觉提示。

#include "IEditorGizmoPlugin.h"

namespace Orange::Editor::Plugin
{

    class PointLightGizmoPlugin : public IEditorGizmoPlugin
    {
    public:
        bool CanHandle(const Orange::Editor::Schema::ComponentSchema& schema) const override;

        void Draw(EditorHost&                                    host,
                  Orange::Engine::Entity                         entity,
                  const Orange::Editor::Schema::ComponentSchema& schema,
                  void*                                          component,
                  const GizmoContext&                            ctx) override;
    };

} // namespace Orange::Editor::Plugin

#endif // ORANGE_EDITOR_PLUGIN_POINT_LIGHT_GIZMO_PLUGIN_H
