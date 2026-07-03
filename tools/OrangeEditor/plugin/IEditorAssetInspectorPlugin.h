#ifndef ORANGE_EDITOR_PLUGIN_I_EDITOR_ASSET_INSPECTOR_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_I_EDITOR_ASSET_INSPECTOR_PLUGIN_H

// ---------------------------------------------------------------------------
// IEditorAssetInspectorPlugin —— Inspector 资源类型分派扩展点抽象基类
// ---------------------------------------------------------------------------
//
// v0.7 c0 落地（"前置整骨"，消除 editor-roadmap.md 已知限制 L16）。
//
// ---- 与 IEditorInspectorPlugin 的语义分工 --------------------------------
//
// 两个抽象正交并存，不互相替代：
//
//   * IEditorInspectorPlugin（v0.2.5 落） —— 按 **component schema** 分派。
//     "选中实体的 Animator 段是否要追加 mini-preview" 这类"装饰式扩展"
//     由它负责。Inspector 主体仍是 schema-driven 字段渲染，plugin 在
//     ParseBegin / ParseEnd 钩子里追加 UI。
//
//   * IEditorAssetInspectorPlugin（本文件） —— 按 **选中资源类型**
//     （扩展名 / kind）分派。"Asset 浏览器选中 .material 文件 → Inspector
//     整体改画 Material 编辑视图" 这类"接管整段 Inspector"由它负责。
//     与实体 Inspector 互斥：plugin 命中即跳过实体 Inspector 渲染。
//
// 触发场景对照：
//
//   * 选中 entity / 没选资源 → 走默认 entity Inspector（schema 字段 +
//     IEditorInspectorPlugin 装饰）
//   * Asset 浏览器选中 .material → MaterialAssetInspectorPlugin 接管
//   * Asset 浏览器选中 .anim_fsm（v0.7 c2 落） → AnimFsmAssetInspectorPlugin
//     接管
//   * Asset 浏览器选中 .scene.json 等未注册类型 → 未命中 plugin，仍走
//     默认 entity Inspector（即"什么都没接管" = 现有行为）
//
// ---- 设计对照（同栈编辑器调研结论）---------------------------------------
//
//   * vendor/LumixEngine/src/editor/asset_browser.h:15 `AssetBrowser::IPlugin`
//     ——按 ResourceType 分派，`addPlugin(IPlugin&, Span<const char*> extensions)`
//     按扩展名注册。本接口采纳同形态：plugin 自报扩展名命中策略
//   * vendor/godot/editor/editor_inspector.h `EditorInspectorPlugin`
//     ——按 Object 类型分派；与本接口"按 path 扩展名分派"语义不同（Godot
//     的 inspector plugin 是 component-level，对偶 IEditorInspectorPlugin
//     而非本接口）
//
// 选择"按 path string 分派"而非"按 enum AssetKind 分派"的理由：plugin
// 命中策略由 plugin 自己实现 `CanHandle(path)`，免去引入 AssetKind 枚举
// + 维护扩展名 → enum 映射表的额外脚手架；新增 asset 子模式只需写一个
// `path.ends_with(".anim_fsm")` 检查，无需改动公共枚举。
//
// ---- 调用约定 -------------------------------------------------------------
//
// 1. InspectorPanel::DrawInspectorPanel 入口处遍历
//    host.assetInspectorPlugins 调 `CanHandle(host.assets.selectedAssetPath)`。
//    第一条返回 true 的 plugin 接管整个 Inspector 区域；插件注册顺序 =
//    push_back 顺序 = 优先级。
//
// 2. plugin->Draw(host, assetPath):
//    plugin 完全接管 Inspector 区域绘制（包括所有 ImGui 调用）。返回后
//    InspectorPanel 跳过实体 Inspector 默认路径。
//
// 3. 未命中任何 plugin → 走默认实体 Inspector 路径（与 v0.5 c5 之前的
//    InspectorPanel 行为一致）。
//
// ---- 生命周期 -------------------------------------------------------------
//
// plugin 实例由 EditorHost.assetInspectorPlugins 内的 std::unique_ptr 持
// 有，生命周期跟 EditorHost 同期；析构顺序在 vector 销毁时按倒序。
// InspectorPanel 调度期间不持有任何 plugin-side 引用——plugin 内部状态
// 自己管（典型：MaterialAssetInspectorPlugin 内的 editing buffer 缓存）。
//
// ---- 不在本期范围 ---------------------------------------------------------
//
//   * 多选资源 / 异构资源 Inspector —— v1.x（撞需求再加）
//   * Plugin 优先级显式排序 / 互斥组 —— 按 push_back 顺序就够，没 use case
//   * Asset 浏览器右键菜单扩展 —— 独立 plugin 抽象（与 Inspector 分离）
// ---------------------------------------------------------------------------

#include <string>

// 前向声明，避免 plugin 头反向 include EditorHost.h（EditorHost.h 反过来
// 需要 include 本文件来容纳 vector<unique_ptr<IEditorAssetInspectorPlugin>>
// 字段）。
struct EditorHost;

namespace Orange::Editor::Plugin
{

    class IEditorAssetInspectorPlugin
    {
    public:
        virtual ~IEditorAssetInspectorPlugin() = default;

        IEditorAssetInspectorPlugin()                                              = default;
        IEditorAssetInspectorPlugin(const IEditorAssetInspectorPlugin&)            = delete;
        IEditorAssetInspectorPlugin& operator=(const IEditorAssetInspectorPlugin&) = delete;
        IEditorAssetInspectorPlugin(IEditorAssetInspectorPlugin&&)                 = delete;
        IEditorAssetInspectorPlugin& operator=(IEditorAssetInspectorPlugin&&)      = delete;

        // 本 plugin 是否处理当前选中资源路径。返回 true 时 InspectorPanel 调度
        // 本 plugin 的 Draw 钩子接管整段；false 时本 plugin 跳过、继续遍历下一
        // 条 plugin。
        //
        // plugin 自己决定过滤策略——推荐按扩展名后缀比较（与 Lumix
        // AssetBrowser::IPlugin 风格一致）。空 path（"" / 未选中资源）应返回
        // false，让默认实体 Inspector 路径接管。
        virtual bool CanHandle(const std::string& assetPath) const = 0;

        // 完全接管 Inspector 区域绘制。plugin 负责本次 Inspector 内所有 ImGui
        // 调用（不包含 ImGui::Begin / ImGui::End——那由 InspectorPanel 主流程
        // 持有）。返回后 InspectorPanel 跳过默认实体 Inspector。
        virtual void Draw(EditorHost& host, const std::string& assetPath) = 0;
    };

} // namespace Orange::Editor::Plugin

#endif // ORANGE_EDITOR_PLUGIN_I_EDITOR_ASSET_INSPECTOR_PLUGIN_H
