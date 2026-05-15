#ifndef ORANGE_EDITOR_PLUGIN_PARTICLE_EMITTER_GIZMO_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_PARTICLE_EMITTER_GIZMO_PLUGIN_H

// ParticleEmitterGizmoPlugin —— v0.4 c4 milestone 第二个真实
// IEditorGizmoPlugin case（与同 commit 的 DirectionalLightGizmoPlugin 并
// 列作 plugin 抽象的双 case 验证）。
//
// 职责：当选中实体挂着 ParticleEmitterComponent 时，在 viewport overlay
// 上可视化两个数据：
//
//   1. **spawn box** —— entity world XY 平面（z = entity.position.z）内
//      `desc.spawnOffsetMin` / `desc.spawnOffsetMax` 围成的 2D 矩形。
//      4 角投影到屏幕画线框。emitter 是 2D 粒子系统（按 VfxSystem 当前
//      实现 spawn 时只取 entity.position.xy + offset，不参与 3D 旋转），
//      因此 spawn box 也按 XY 平面绘制，不跟 entity rotation 转。
//
//   2. **initial velocity 向量** —— 从 entity world 位置沿
//      `avg(desc.initialVelocityMin, desc.initialVelocityMax)` 方向画一
//      根箭头（XY 平面内，z=0）。长度自适应屏幕 ~80 px。
//
// 设计意图：与 DirectionalLightGizmoPlugin 同款"纯装饰 overlay"路径——
// 不接管 LMB、不入命令栈。HitTest 走基类默认 false。Inspector 仍是字段
// 编辑的唯一入口；plugin 仅给作者在 viewport 内"看清楚"参数。
//
// 颜色约定：用青色（cyan）系，与 Light gizmo 黄色 + 内置 Translate 红/
// 绿/蓝清晰区分。

#include "IEditorGizmoPlugin.h"

namespace Orange::Editor::Plugin
{

class ParticleEmitterGizmoPlugin : public IEditorGizmoPlugin
{
public:
    // 按 schema.typeName == "ParticleEmitter" 字符串比较（与
    // RegisterParticleEmitterComponentSchema 内 typeName 字面量一致）。
    bool CanHandle(const Orange::Editor::Schema::ComponentSchema& schema) const override;

    // 画 spawn box（4 角连线）+ initial velocity 平均向量箭头。
    void Draw(EditorHost&                                            host,
              Orange::Engine::Entity                                 entity,
              const Orange::Editor::Schema::ComponentSchema&         schema,
              void*                                                  component,
              const GizmoContext&                                    ctx) override;

    // HitTest 走基类默认（false）—— 纯装饰。
};

}  // namespace Orange::Editor::Plugin

#endif  // ORANGE_EDITOR_PLUGIN_PARTICLE_EMITTER_GIZMO_PLUGIN_H
