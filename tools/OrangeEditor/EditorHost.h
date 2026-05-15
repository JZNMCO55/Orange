#ifndef ORANGE_EDITOR_EDITOR_HOST_H
#define ORANGE_EDITOR_EDITOR_HOST_H

// EditorHost —— 编辑器顶层"应用入口" / hub 单例，聚合 4 个 sub-context +
// CommandStack。
//
// 历史脉络：
//   * v0.1 ~ v0.2 期：EditorState 是 19 字段 god struct，每个 milestone 往里
//     堆字段没有任何子域切分
//   * v0.2.5 commit 1：按域拆出 EditorSelection / EditorSceneContext /
//     EditorAssetContext / EditorCameraState 四个 sub-context，EditorState
//     退化成"4 个 context + 顶层 pCmdStack"的薄壳；这是机械搬迁，仍保留
//     "单一 EditorState&"的 caller 形状
//   * v0.2.5 commit 2（本 commit）：把 EditorState 整体替换为 EditorHost；
//     CommandStack 从顶层 unique_ptr<CommandStack> 转为 EditorHost 的值成员
//     `cmdStack`；EditorState 名称彻底退场
//
// 设计参考：
//   * vendor/LumixEngine/src/editor/studio_app.h —— `StudioApp` 是 Lumix 的
//     编辑器中央 hub：拥有 WorldEditor / PropertyGrid / AssetBrowser /
//     LogUI / 插件注册表 / 主循环。所有编辑器服务都从 StudioApp 上挂出
//   * vendor/godot/editor/editor_node.h —— Godot 的 EditorNode 同样是单
//     入口聚合点
//
// 这两套都把"编辑器有它自己的 application 概念"显式建出来，避免任何"全
// 局编辑器服务"被迫塞进散落的 god struct 字段。OrangeEditor 之前缺这一层，
// 任何新功能（剪贴板 / quick search / preferences / 插件 registry）都只能
// 继续往 EditorState 上堆字段—— editor-roadmap.md v0.2.5 "5 条诊断"第 5 条。
//
// 命名约定：本 host 类的实例在 main 中名为 `editorHost`；EditorRenderLayer
// 内同时持有 AppHost&（引擎层概念，命名为 `mAppHost`）与 EditorHost&（编辑
// 器层概念，命名为 `mHost`）。后者是编辑器中央 hub，匹配 Lumix StudioApp /
// Godot EditorNode 的工业惯例。
//
// 未来扩展（v0.2.5 后续 deliverables）：
//   * IEditorInspectorPlugin / IEditorGizmoPlugin 注册表（v0.2.5 commit
//     N，与 PropertySchema 同期落）
//   * 全局编辑器服务（剪贴板 / preferences / quick search / log UI 接入）
//
// 这些都在本 host 上挂出来，不再走"加字段到 EditorState"的老路。

#include "command/CommandStack.h"
#include "context/EditorAssetContext.h"
#include "context/EditorCameraState.h"
#include "context/EditorGizmoState.h"
#include "context/EditorSceneContext.h"
#include "context/EditorSelection.h"
#include "plugin/IEditorGizmoPlugin.h"
#include "plugin/IEditorInspectorPlugin.h"

#include <orange/engine/scene/ComponentSerializerEntry.h>

#include <memory>
#include <vector>

struct EditorHost
{
    EditorSelection    selection;
    EditorSceneContext scene;
    EditorAssetContext assets;
    EditorCameraState  camera;
    EditorGizmoState   gizmo;

    // 命令栈是编辑器全局单例：所有 mutate 走它产生 / Undo / Redo。
    // 值成员（非 unique_ptr）—— 没有跨 host 共享需求，少一层间接 + 免
    // nullptr 检查。CommandStack 自身 POD-ish，构造无副作用。
    CommandStack cmdStack;

    // Inspector 渲染扩展点注册表 —— v0.2.5 commit 11 仅声明 + 注册表，
    // 集成进 SchemaInspector + 第一个真实 plugin case 在 v0.3 落地。详见
    // plugin/IEditorInspectorPlugin.h 头注释（设计意图 / 调用约定 / 不在
    // 本期范围）。
    //
    // 存储用 std::vector<unique_ptr<...>>——plugin 由 host 持有 ownership，
    // 析构顺序按 vector 倒序自动管理；plugin 注册顺序 = SchemaInspector 检
    // 查优先级（按 push_back 顺序遍历，第一条 CanHandle == true 接管）。
    //
    // v0.2.5 commit 11 完成时本字段恒为空——尚无真实 plugin 注册路径，
    // 也无 SchemaInspector 调度路径；字段存在仅为锁定 EditorHost 公共接
    // 口，让 v0.3 落地时无需再 bump EditorHost layout。
    std::vector<std::unique_ptr<Orange::Editor::Plugin::IEditorInspectorPlugin>>
        inspectorPlugins;

    // Viewport gizmo 渲染 + hit-test 扩展点注册表 —— v0.2.5 commit 12 仅
    // 声明 + 注册表，集成进 SceneView overlay + 第一个真实 plugin case 在
    // v0.4 "Gizmo & 特殊对象可视化" milestone 落地。详见 plugin/IEditor
    // GizmoPlugin.h 头注释（设计意图 / 调用约定 / 不在本期范围）。
    //
    // 与 inspectorPlugins 同款 std::vector + unique_ptr 模式；plugin 注册顺
    // 序 = SceneView overlay 遍历顺序 = Draw 调用顺序（多 plugin 都返回 true
    // 时全部绘制，**不**互斥；HitTest 同样按注册顺序检查，第一个命中即结束）。
    //
    // 内置 Transform translate / rotate / scale gizmo **不** 走本注册表
    // ——见 plugin/IEditorGizmoPlugin.h 头注释。本注册表只承载 component-
    // specific overlay（DirectionalLight 方向箭头 / ParticleEmitter spawn
    // box / Camera frustum / 游戏侧 component 自定义 gizmo 等）。
    //
    // v0.2.5 commit 12 完成时本字段恒为空；字段存在仅为锁定 EditorHost
    // 公共接口，让 v0.4 落地时无需再 bump EditorHost layout。
    std::vector<std::unique_ptr<Orange::Editor::Plugin::IEditorGizmoPlugin>>
        gizmoPlugins;

    // 游戏侧 / demo 侧自定义 component 序列化器注册表。
    // main 启动期把各 ComponentSerializerEntry push_back 进来；所有
    // Scene::Save / Load 调用点通过 `extraSerializers` 字段把整张表以
    // std::span 视图传给引擎序列化器，实现非侵入的 Save/Load round-trip。
    // 存储用 value（非 unique_ptr）—— ComponentSerializerEntry 只含函数
    // 指针 + string_view，自身无资源所有权，拷贝语义安全。
    std::vector<Orange::Engine::Scene::ComponentSerializerEntry> extraSerializers;
};

#endif  // ORANGE_EDITOR_EDITOR_HOST_H
