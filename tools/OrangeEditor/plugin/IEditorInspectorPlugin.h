#ifndef ORANGE_EDITOR_PLUGIN_I_EDITOR_INSPECTOR_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_I_EDITOR_INSPECTOR_PLUGIN_H

// ---------------------------------------------------------------------------
// IEditorInspectorPlugin —— Inspector 渲染扩展点抽象基类
// ---------------------------------------------------------------------------
//
// v0.2.5 commit 11 落地：**仅声明 + 注册表**，本期不实现真实 plugin、不集
// 成进 SchemaInspector。v0.3 起出第一个真实 plugin case（候选：Animator
// 加 mini-preview / Material 加资源预览缩略图）；SchemaInspector 集成路径
// 在该 commit 同步落地。
//
// ---- 设计意图 -------------------------------------------------------------
//
// v0.2.5 schema-first 完工后，所有内置 + 游戏侧 component 都通过
// ComponentSchema 注册暴露字段；但部分 component 需要"超出 schema 通用控
// 件"的自定义 UI——典型：
//
//   * Material 字段 + 实时缩略图预览
//   * Animator 字段 + 内嵌迷你时间轴 / clip 预览
//   * Light 字段 + viewport 同步小开关 / 颜色温度色环
//
// 这些"装饰式"扩展不该写进 schema 系统本体（schema 只表达字段元数据，引入
// "渲染 callback"会把它退化回 v0.1 期的 hardcode UI），也不该 hardcode 在
// InspectorPanel.cpp（违反 v0.2.5 "OrangeEditor 架构纪律"——任何 per-
// component UI 必须以独立注册项 / plugin / schema 形式存在）。
//
// 引入 plugin 抽象让扩展能挂在 EditorHost.inspectorPlugins 注册表上；
// SchemaInspector 渲染某 component 段时遍历 plugin 注册表寻找 CanHandle ==
// true 的扩展，按 ParseBegin / ParseEnd 钩子契约调度。
//
// ---- 设计对照（同栈编辑器调研结论）---------------------------------------
//
//   * vendor/godot/editor/editor_inspector.h `EditorInspectorPlugin` ——
//     有 parse_begin / parse_property / parse_category / parse_end 四档钩子。
//     最丰富的 property-level 拦截可让 plugin 接管某具体字段的渲染而不替
//     换整段。
//   * vendor/LumixEngine/src/editor/property_grid.h `PropertyGrid::IPlugin` ——
//     单一 `onGUI(ComponentType, EntityRef)` 钩子配合 ComponentType 过滤；
//     用 IPlugin 加 onGUI 就接管整段，没有 begin/end 双钩子的概念。
//
// OrangeEditor 走 **Godot 风格的多档钩子**，但本期最小集只落 begin / end 两
// 档（component 段级）。理由：
//
//   * begin/end 双钩子已经能覆盖"装饰式"扩展的主要 use case（在段首 /
//     段尾追加 UI）。返回 true 的 ParseBegin 也能实现 Lumix 那种"接管整段"
//     的语义
//   * property-level 拦截（parse_property）等遇到具体需要再加。提前引入
//     parse_property 会暴露"plugin 与 schema 字段命名硬绑定"的接口面，
//     当前没有 use case 来检验该接口边界
//
// ---- 调用约定（v0.3 SchemaInspector 集成时实现，本期仅文档化）------------
//
// 1. SchemaInspector 渲染某 component 段前，遍历 host.inspectorPlugins 调
//    `CanHandle(schema)`。第一条返回 true 的 plugin 接管该段（plugin 注
//    册顺序 = 优先级；后注册的覆盖先注册的——LIFO 不适合，registry 应保
//    持 push-back 顺序、SchemaInspector 按 push-back 顺序检查）。
//
// 2. plugin->ParseBegin(host, entity, schema, component):
//      返回 true  → 跳过默认 schema 渲染（plugin 完全接管整段）
//      返回 false → 继续默认 schema 字段渲染
//
// 3. plugin->ParseEnd(host, entity, schema, component):
//      在默认渲染（或 plugin 自接管）之后调用，用于追加扩展 UI。
//      无返回值——append 永远是 best-effort，不影响主渲染流程。
//
// 注：CanHandle 用 schema 引用作为过滤键。plugin 自己决定按 `schema.typeName`
// 字符串比较（推荐——schema 重注册或类型相同的不同实例也匹配）还是按
// `&schema == &mTargetSchema` 指针比较（不推荐——schema 实例地址通过
// std::deque 稳定，但跨 module 重新注册会断裂）。
//
// ---- 生命周期 -------------------------------------------------------------
//
// plugin 实例由 EditorHost.inspectorPlugins 内的 std::unique_ptr 持有，生
// 命周期跟 EditorHost 同期；析构顺序在 vector 销毁时按倒序。SchemaInspector
// 在 plugin 调用期间不持有任何 plugin-side 引用——plugin 内部状态自己管。
//
// ---- 不在本期范围 ---------------------------------------------------------
//
//   * SchemaInspector 内 plugin 调度集成 —— v0.3
//   * IEditorGizmoPlugin —— v0.2.5 commit 12
//   * Plugin 热重载 / 动态 add / remove —— v1.x（依赖 hot reload milestone）
//   * Plugin 间通信 / 共享上下文 —— 撞上需求再加，本期不预先撒网
// ---------------------------------------------------------------------------

#include <orange/engine/scene/Entity.h>

// 前向声明，避免 plugin 头反向 include EditorHost.h（EditorHost.h 反过来需
// 要 include 本文件来容纳 vector<unique_ptr<IEditorInspectorPlugin>> 字段）。
struct EditorHost;

namespace Orange::Editor::Schema
{
    struct ComponentSchema;
}

namespace Orange::Editor::Plugin
{

    class IEditorInspectorPlugin
    {
    public:
        virtual ~IEditorInspectorPlugin() = default;

        IEditorInspectorPlugin()                                         = default;
        IEditorInspectorPlugin(const IEditorInspectorPlugin&)            = delete;
        IEditorInspectorPlugin& operator=(const IEditorInspectorPlugin&) = delete;
        IEditorInspectorPlugin(IEditorInspectorPlugin&&)                 = delete;
        IEditorInspectorPlugin& operator=(IEditorInspectorPlugin&&)      = delete;

        // 本 plugin 是否处理某 component schema 段。返回 true 时 SchemaInspector
        // 调度本 plugin 的 ParseBegin / ParseEnd 钩子；false 时本 plugin 跳过。
        //
        // plugin 自己决定过滤策略——推荐按 `schema.typeName` 字符串比较。
        virtual bool CanHandle(const Orange::Editor::Schema::ComponentSchema& schema) const = 0;

        // 在默认 schema 字段渲染**之前**调用。
        //
        // 返回值：
        //   * true  —— plugin 接管整段，SchemaInspector 跳过默认字段渲染。常用
        //              于"完全自定义 component UI"的场景（少见）
        //   * false —— 默认渲染正常进行；本钩子只在段首追加额外 UI（典型：
        //              section banner / 状态指示 / 缩略图）
        //
        // 默认 implementation 返回 false（不接管），仅在派生类需要重写时覆盖。
        virtual bool ParseBegin(EditorHost&                                    host,
                                Orange::Engine::Entity                         entity,
                                const Orange::Editor::Schema::ComponentSchema& schema,
                                void*                                          component)
        {
            (void)host;
            (void)entity;
            (void)schema;
            (void)component;
            return false;
        }

        // 在默认 schema 字段渲染**之后**调用（即使 ParseBegin 返回 true 接管整
        // 段，ParseEnd 仍会被调到——让"plugin 自渲染主区 + 通过 ParseEnd 追加
        // footer"的组合可行）。
        //
        // 典型用例：在 component 段最末追加"应用 preset"按钮 / mini-preview /
        // 资源缩略图 / playback 控件。
        //
        // 默认 implementation 是 no-op。
        virtual void ParseEnd(EditorHost&                                    host,
                              Orange::Engine::Entity                         entity,
                              const Orange::Editor::Schema::ComponentSchema& schema,
                              void*                                          component)
        {
            (void)host;
            (void)entity;
            (void)schema;
            (void)component;
        }
    };

} // namespace Orange::Editor::Plugin

#endif // ORANGE_EDITOR_PLUGIN_I_EDITOR_INSPECTOR_PLUGIN_H
