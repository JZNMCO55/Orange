#ifndef ORANGE_EDITOR_PLUGIN_POST_PROCESS_VOLUME_GIZMO_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_POST_PROCESS_VOLUME_GIZMO_PLUGIN_H

// PostProcessVolumeGizmoPlugin —— viewport overlay 装饰 gizmo（与 PointLight /
// DirectionalLight / ParticleEmitter / CameraFrustum GizmoPlugin 同款 "plugin
// 只画 overlay，不接管 LMB 拖动" 模式）。
//
// 职责：选中实体挂 PostProcessComponent 且 mode=Local 时，画两层 wireframe
// box —— 内层是 localExtent 半尺寸盒（weight=1 区，画面 100% 受该 volume
// 影响），外层是 (localExtent + blendDistance) 半尺寸盒（smoothstep 淡入
// 边界，相机出 outer box 后 weight=0）。
//
// 模板：参 CameraFrustumGizmoPlugin 的 12 段画法（near rect 4 + far rect 4
// + connecting 4 = 12 条边），box 8 corners = center ± (±extent.x, ±extent.y,
// ±extent.z)。center 取 entity.Transform.position（与 Pipeline.cpp v2
// SyncPostProcessFromWorld 的 boxCenter 取法严格一致），AABB 不考虑 rotation
// （与 v2 Pipeline 算法一致——v2 用相机 world position vs entity.position 各
// 轴绝对距离判 box 命中，不走 rotation）。
//
// Mode=Global 时本 plugin 跳过（Global 是全局生效，无几何边界可画）。

#include "IEditorGizmoPlugin.h"

namespace Orange::Editor::Plugin
{

class PostProcessVolumeGizmoPlugin : public IEditorGizmoPlugin
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

#endif  // ORANGE_EDITOR_PLUGIN_POST_PROCESS_VOLUME_GIZMO_PLUGIN_H
