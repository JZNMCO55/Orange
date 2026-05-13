#ifndef ORANGE_EDITOR_PLUGIN_I_EDITOR_GIZMO_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_I_EDITOR_GIZMO_PLUGIN_H

// ---------------------------------------------------------------------------
// IEditorGizmoPlugin —— viewport gizmo 渲染 + hit-test 扩展点抽象基类
// ---------------------------------------------------------------------------
//
// v0.2.5 commit 12 落地：**仅声明 + 注册表**，本期不实现真实 gizmo plugin、
// 不集成进 viewport 渲染。v0.4 "Gizmo & 特殊对象可视化" milestone 实现：
//
//   * Transform translate / rotate / scale 三态 gizmo（内置，非 plugin）
//   * Light direction 箭头（plugin: DirectionalLight）
//   * ParticleEmitter spawn offset box + 初速度向量（plugin: ParticleEmitter）
//   * Camera frustum（plugin: 待 Camera component 引入后）
//   * 游戏侧 component 自定义 gizmo（plugin）
//
// 内置 Transform gizmo 不走 plugin 路径——所有 entity 都用同一套 translate /
// rotate / scale，不需要 per-component 注册过滤；走专门的 TransformGizmo
// 子系统（v0.4 设计）。Plugin 路径仅承载 **component-specific overlay**。
//
// ---- 设计对照（同栈编辑器调研结论）---------------------------------------
//
//   * vendor/godot/editor/plugins/node_3d_editor_gizmos.h 的
//     `EditorNode3DGizmoPlugin` —— has_gizmo / create_gizmo /
//     get_name / set_state 等多档接口，比本期最小集复杂；OrangeEditor 不需
//     要 plugin 自管 gizmo 实例（Godot 的设计是 gizmo "生成"出来挂在 Node
//     上），所以走更扁平的 Draw / HitTest 直接调度
//   * vendor/LumixEngine/src/editor/gizmo.cpp —— 没有显式 plugin 抽象，所
//     有 gizmo 写死在 SceneView。Lumix 那套不可扩展，OrangeEditor 不学
//
// ---- 调用约定（v0.4 viewport overlay 集成时实现，本期仅文档化）-----------
//
// 1. v0.4 SceneView / viewport overlay 渲染路径，每帧对 **selected entity** 遍
//    历 host.gizmoPlugins 调 `CanHandle(schema)`，对所有返回 true 的 plugin
//    依次调 `Draw(host, entity, schema, component, ctx)`。
//
//    "selected entity only" 是预期 v0.4 行为——非选中实体不画 gizmo 避免视
//    觉噪声；如果未来要"所有有 light 的实体都画 light 图标"那种 always-on
//    gizmo，再扩 plugin 接口加 `AlwaysOn()` 钩子。
//
// 2. 鼠标点 viewport 时遍历 plugin 调 `HitTest(host, entity, schema, component,
//    ctx)` 返回 true 即认定 gizmo 被选中。HitTest 内 plugin 自己负责把 hit
//    handle 标识写进 EditorSelection / GizmoContext 的可写槽位（v0.4 决定具
//    体写在哪）。
//
// 3. 鼠标拖动时 SceneView 把 drag delta 转 ECS 字段编辑命令——这一步 v0.4
//    SceneView 自己处理（plugin 已经在 HitTest 阶段告知"哪个 handle"），不
//    需要单独的 Drag 钩子。
//
// ---- GizmoContext 前向声明纪律 -------------------------------------------
//
// `GizmoContext` 在本头**仅前向声明**，由 v0.4 在 plugin/GizmoContext.h
// （或 v0.4 gizmo 子系统选择的位置）定义。预期字段：
//
//   * 当前 viewport 的 view + projection matrix 快照
//   * ImDrawList* 或编辑器自管的 2D overlay 绘制句柄
//   * 当前鼠标的 picking ray（worldspace Ray3）
//   * gizmo handle 命中槽（可写——HitTest 内 plugin 写入命中的 handle id）
//
// 前向声明的代价：concrete plugin .cpp 必须 include GizmoContext.h 才能 cast
// 这个引用拿 fields。代价可接受——本期没有任何 concrete plugin，v0.4 写
// 第一个真实 plugin 时一并 include 即可。
//
// 收益：本头 ABI 在 v0.4 加 GizmoContext 字段时**无需变更**——只要 fields
// 增加不破坏现有 plugin 用的字段，plugin .h 不动、已注册的 plugin 不重新
// 编译。
//
// ---- 生命周期 -------------------------------------------------------------
//
// plugin 实例由 EditorHost.gizmoPlugins 内的 std::unique_ptr 持有，生命周
// 期跟 EditorHost 同期；析构顺序在 vector 销毁时按倒序。SchemaInspector /
// SceneView 在 plugin 调用期间不持有任何 plugin-side 引用——plugin 内部
// 状态自己管。
//
// ---- 不在本期范围 ---------------------------------------------------------
//
//   * Viewport overlay 集成（Draw / HitTest 调用路径）—— v0.4
//   * GizmoContext 结构体定义 —— v0.4
//   * 内置 Transform gizmo（translate / rotate / scale）—— v0.4，**不走** plugin
//   * 任何 concrete gizmo plugin —— v0.4
// ---------------------------------------------------------------------------

#include <orange/engine/scene/Entity.h>

// 前向声明——理由同 IEditorInspectorPlugin.h。EditorHost.h 反向 include
// 本文件来容纳 vector<unique_ptr<IEditorGizmoPlugin>> 字段，因此本头**不能**
// include EditorHost.h（防循环）。
struct EditorHost;

namespace Orange::Editor::Schema
{
struct ComponentSchema;
}

namespace Orange::Editor::Plugin
{

// 前向声明：v0.4 gizmo milestone 定义此结构（见头注释"GizmoContext 前向声
// 明纪律"）。本期注册任何 IEditorGizmoPlugin 派生类合法但调用 Draw / HitTest
// 时需要 v0.4 提供的 GizmoContext 实例——v0.2.5 不存在该调用点。
struct GizmoContext;

class IEditorGizmoPlugin
{
public:
    virtual ~IEditorGizmoPlugin() = default;

    IEditorGizmoPlugin()                                            = default;
    IEditorGizmoPlugin(const IEditorGizmoPlugin&)                   = delete;
    IEditorGizmoPlugin& operator=(const IEditorGizmoPlugin&)        = delete;
    IEditorGizmoPlugin(IEditorGizmoPlugin&&)                        = delete;
    IEditorGizmoPlugin& operator=(IEditorGizmoPlugin&&)             = delete;

    // 本 plugin 是否为某 component schema 绘制 gizmo / 参与 hit-test。
    // 返回 true 时 SceneView overlay 调度本 plugin 的 Draw / HitTest 钩子。
    //
    // plugin 自决过滤策略——推荐按 `schema.typeName` 字符串比较（与
    // IEditorInspectorPlugin::CanHandle 同款约定）。
    virtual bool CanHandle(const Orange::Editor::Schema::ComponentSchema& schema) const = 0;

    // 在 viewport overlay 上绘制 gizmo（线段 / billboard / icon 等）。
    //
    // 调用时机（v0.4 预期）：当前 selected entity 上挂着本 plugin CanHandle
    // 的 component 时，每帧调用一次。plugin 通过 ctx 拿 viewport 状态 +
    // overlay 绘制句柄。
    //
    // 纯虚——本钩子是 gizmo plugin 的存在意义，必须实现。
    virtual void Draw(EditorHost&                                            host,
                      Orange::Engine::Entity                                 entity,
                      const Orange::Editor::Schema::ComponentSchema&         schema,
                      void*                                                  component,
                      const GizmoContext&                                    ctx) = 0;

    // 鼠标 picking ray 对 gizmo 几何的命中检测。
    //
    // 返回值：
    //   * true  —— gizmo 被命中。plugin 负责把"哪个 handle 命中 / 拖动起点"
    //              等信息写进 ctx（v0.4 决定具体写入位置——可能是 ctx
    //              内可写字段、可能是 EditorSelection 内附加槽位）
    //   * false —— 未命中。caller 继续询问下一个 plugin
    //
    // 默认 implementation 返回 false——gizmo plugin 可选地参与 hit-test。
    // 不需要 hit-test 的纯装饰类 gizmo（如静态状态指示图标）继承默认即可。
    virtual bool HitTest(EditorHost&                                            host,
                         Orange::Engine::Entity                                 entity,
                         const Orange::Editor::Schema::ComponentSchema&         schema,
                         void*                                                  component,
                         const GizmoContext&                                    ctx)
    {
        (void)host; (void)entity; (void)schema; (void)component; (void)ctx;
        return false;
    }
};

}  // namespace Orange::Editor::Plugin

#endif  // ORANGE_EDITOR_PLUGIN_I_EDITOR_GIZMO_PLUGIN_H
