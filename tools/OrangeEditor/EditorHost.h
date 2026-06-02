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
#include "context/EditorColliderEditState.h"
#include "context/EditorGizmoState.h"
#include "context/EditorKeybindings.h"
#include "context/EditorSceneContext.h"
#include "context/EditorSelection.h"
#include "context/EditorSettings.h"
#include "plugin/IEditorAssetInspectorPlugin.h"
#include "plugin/IEditorGizmoPlugin.h"
#include "plugin/IEditorInspectorPlugin.h"

#include <orange/engine/audio/AudioEngine.h>
#include <orange/engine/scene/ComponentSerializerEntry.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

// ThumbnailService 前向声明 —— thumbnails 字段是 unique_ptr，本头只需不完整
// 类型。完整定义（拖 Vulkan / ImGui / RHITexture 重量级头）只在 main.cpp 构造
// / 析构 EditorHost 的 TU + ThumbnailService.cpp 内可见，不传染所有 include
// EditorHost.h 的编辑器 TU（尤其避免 vulkan.h → windows.h min/max 宏污染）。
namespace Orange::Editor::Render
{
class ThumbnailService;
}

struct EditorHost
{
    EditorSelection    selection;
    EditorSceneContext scene;
    EditorAssetContext assets;
    EditorCameraState  camera;
    EditorGizmoState   gizmo;
    EditorColliderEditState colliderEdit;
    EditorSettings     settings;
    EditorKeybindings  keybindings;

    // Component 值剪贴板（右键组件头 Copy / Paste Values，参 Lumix StudioApp /
    // Unity "Copy Component / Paste Component Values"）。Copy 把源组件各 property
    // 值快照成 restorer 闭包（SchemaInspector::CaptureComponentState）+ 记类型名；
    // Paste 仅当目标组件 schema.typeName 与剪贴板一致时把快照应用到目标（可 Undo）。
    // entity 子树剪贴板（mEntityClipboard，在 EditorRenderLayer）是另一层级，互不干扰。
    struct ComponentValueClipboard
    {
        std::string                             typeName;   // 源组件类型；Paste 校验
        std::vector<std::function<void(void*)>> restorers;  // 各 property 值快照
        bool                                    valid = false;
    };
    ComponentValueClipboard componentClipboard;

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

    // Inspector 资源类型分派扩展点注册表 —— v0.7 c0 落地（消除 L16）。
    //
    // 与 inspectorPlugins / gizmoPlugins 并存：前者按 component schema 分
    // 派（"实体段装饰"），后者按选中资源扩展名分派（"Asset 浏览器选中
    // .material → Inspector 整体改画"）。InspectorPanel::DrawInspectorPanel
    // 入口处遍历本注册表，第一条 CanHandle(selectedAssetPath) == true 的
    // plugin 接管整段；未命中则走默认实体 Inspector 路径。
    //
    // 详见 plugin/IEditorAssetInspectorPlugin.h 头注释（设计意图 / 调用约
    // 定 / 与 IEditorInspectorPlugin 的语义分工）。
    std::vector<std::unique_ptr<Orange::Editor::Plugin::IEditorAssetInspectorPlugin>>
        assetInspectorPlugins;

    // 游戏侧 / demo 侧自定义 component 序列化器注册表。
    // main 启动期把各 ComponentSerializerEntry push_back 进来；所有
    // Scene::Save / Load 调用点通过 `extraSerializers` 字段把整张表以
    // std::span 视图传给引擎序列化器，实现非侵入的 Save/Load round-trip。
    // 存储用 value（非 unique_ptr）—— ComponentSerializerEntry 只含函数
    // 指针 + string_view，自身无资源所有权，拷贝语义安全。
    std::vector<Orange::Engine::Scene::ComponentSerializerEntry> extraSerializers;

    // v1.1 T2：OS 文件 drop 到主窗口时的入站队列。main.cpp 的
    // glfwSetDropCallback 把绝对路径 push 进来；EditorRenderLayer::OnUpdate
    // 帧末统一 drain（按入队顺序逐条调 ImportDispatcher::Dispatch）。
    // 在 GLFW callback 线程 vs 主线程之间无并发：drop callback 由 glfwPollEvents
    // 在主线程内同步触发，与 OnUpdate 在同一帧但顺序固定（poll 先 push，
    // OnUpdate 后 drain），不需要 mutex。
    std::vector<std::string> pendingImports;

    // 编辑器级别的全局 AudioEngine —— 编辑器的"应用进程音频上下文"，与
    // Inspector 试播按钮 + Asset 浏览器音频预览 + PlayMode 期 AudioSource
    // 组件实例化共享同一 ma_engine（避免多实例同时持设备 mutex / 多次拉起
    // WASAPI session）。
    //
    // 启动期 main 构造（默认 desc，硬件 backend）；析构在 EditorHost 析构
    // 时自动跑（main 持值类型 EditorHost，倒序销毁字段）。
    //
    // 没声卡 / WASAPI 失败时 audioEngine.IsInitialized() == false，所有调用
    // 路径（PlayOneShot / CreateInstance）已是 no-op 安全（参 AudioEngine.h
    // 头注释"游戏没声卡的机器仍能跑"）。
    Orange::Engine::Audio::AudioEngine audioEngine;

    // 材质球缩略图服务（Asset Browser 里 .material 显示渲染缩略图，替代
    // "[Mat]" 文本 icon）。与 audioEngine 同位：编辑器进程级全局服务，挂在
    // EditorHost 而非塞进 EditorRenderLayer / EditorState mega-class（schema-
    // first 纪律）。
    //
    // unique_ptr 而非值成员：构造需要 RenderDevice + ImGui descriptor pool +
    // EditorHost& 自引用，这些在 main 启动期 ImGui / Renderer init 之后才就绪，
    // 故由 main 在那一刻 make_unique（EditorHost 是无构造函数的聚合，不能在
    // 成员初始化里自引用）。viewport Pipeline 由 EditorRenderLayer 经
    // SetPipeline 注入；layer 析构前后的 Shutdown 时机见 main / ScenePanel。
    std::unique_ptr<Orange::Editor::Render::ThumbnailService> thumbnails;
};

#endif  // ORANGE_EDITOR_EDITOR_HOST_H
