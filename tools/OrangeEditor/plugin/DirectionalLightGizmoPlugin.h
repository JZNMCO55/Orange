#ifndef ORANGE_EDITOR_PLUGIN_DIRECTIONAL_LIGHT_GIZMO_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_DIRECTIONAL_LIGHT_GIZMO_PLUGIN_H

// DirectionalLightGizmoPlugin —— v0.4 c4 milestone 第一个真实
// IEditorGizmoPlugin case（与 v0.3 c3 AnimatorMiniPreviewPlugin 对偶）。
//
// 职责：当选中实体挂着 DirectionalLightComponent 时，在 viewport overlay
// 上从 entity world 位置沿 `direction` 方向画一根**黄色箭头**，可视化光
// 的传播方向（与组件注释"direction 是光的传播方向，不是从表面到光源"
// 对齐——箭头**朝向光照投射方向**，即"光从这里走向那里"）。
//
// 设计意图：验证 IEditorGizmoPlugin "装饰式 overlay" 路径——plugin 仅
// 读取 component 数据投到屏幕画线，**不**接管 LMB 拖动、**不**入命令栈。
// HitTest 默认返回 false（继承基类默认实现）；用户改方向仍走 Inspector
// 的 Vec3 DragFloat3。这是 v0.4 minimum viable plugin case，c5+ 真撞到
// "viewport 内拖箭头改方向"需求时再加 HitTest + drag 路径。
//
// 选型理由：DirectionalLight 是 demo scene 已经存在的 component（13 个
// 实体里 1 个），c4 落地后可在编辑器内直接观察；ParticleEmitter 同款已
// 经在 demo（火焰 + 萤火两个 emitter），同 commit 走 ParticleEmitter
// GizmoPlugin。两个 plugin 一起作为 IEditorGizmoPlugin 抽象的双 case 验
// 证（与 v0.3 单 plugin case 不同——v0.4 一次性出两个，验证 plugin 之
// 间互不串味 + 多 plugin 并存绘制顺序）。

#include "IEditorGizmoPlugin.h"

namespace Orange::Editor::Plugin
{

    class DirectionalLightGizmoPlugin : public IEditorGizmoPlugin
    {
    public:
        // 按 schema.typeName == "DirectionalLight" 字符串比较匹配（与
        // RegisterDirectionalLightComponentSchema 内 typeName 字面量保持一致）。
        bool CanHandle(const Orange::Editor::Schema::ComponentSchema& schema) const override;

        // 画 entity world 位置 → entity + normalize(direction) * handleLength
        // 的黄色 3D 箭头（屏幕长度自适应，与 Translate/Rotate gizmo 同款 ~90 px）。
        void Draw(EditorHost&                                    host,
                  Orange::Engine::Entity                         entity,
                  const Orange::Editor::Schema::ComponentSchema& schema,
                  void*                                          component,
                  const GizmoContext&                            ctx) override;

        // HitTest 走基类默认（false）——本 plugin 是纯装饰 overlay，不参与
        // mouse picking。继承默认即可，不重写。
    };

} // namespace Orange::Editor::Plugin

#endif // ORANGE_EDITOR_PLUGIN_DIRECTIONAL_LIGHT_GIZMO_PLUGIN_H
