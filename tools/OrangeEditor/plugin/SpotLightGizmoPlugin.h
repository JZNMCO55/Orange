#ifndef ORANGE_EDITOR_PLUGIN_SPOT_LIGHT_GIZMO_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_SPOT_LIGHT_GIZMO_PLUGIN_H

// SpotLightGizmoPlugin —— viewport overlay 装饰 gizmo（与 DirectionalLight /
// PointLight GizmoPlugin 同款"plugin 只画 overlay，不接管 LMB 拖动"模式）。
//
// 职责：选中实体挂 SpotLight 时，画一个锥体 wireframe —— apex 在 entity
// world 位置，沿 Transform.rotation 派生的锥光方向张开到 range 距离，base
// 圆半径 = range × tan(outerConeAngle)。给用户"光打向哪、张多宽、照多远"
// 的直观提示。方向 / 位置随 entity Transform 即时跟随。

#include "IEditorGizmoPlugin.h"

namespace Orange::Editor::Plugin
{

class SpotLightGizmoPlugin : public IEditorGizmoPlugin
{
public:
    bool CanHandle(const Orange::Editor::Schema::ComponentSchema& schema) const override;

    void Draw(EditorHost&                                            host,
              Orange::Engine::Entity                                 entity,
              const Orange::Editor::Schema::ComponentSchema&         schema,
              void*                                                  component,
              const GizmoContext&                                    ctx) override;
};

}  // namespace Orange::Editor::Plugin

#endif  // ORANGE_EDITOR_PLUGIN_SPOT_LIGHT_GIZMO_PLUGIN_H
