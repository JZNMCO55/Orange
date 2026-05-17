#ifndef ORANGE_EDITOR_EDITOR_RENDER_LAYER_H
#define ORANGE_EDITOR_EDITOR_RENDER_LAYER_H

// EditorRenderLayer —— 编辑器的核心 layer，挂在 AppHost::LayerStack 末尾。
// 每帧 OnUpdate 内：
//   1. ImGui::NewFrame + 建 dockspace + 5 个固定面板（Scene / Entity Tree
//      / Inspector / Assets / Console）+ main menu bar；
//   2. ApplyPendingSceneOp 收尾文件操作（File 菜单点击的延迟执行）；
//   3. ImGui::Render；
//   4. engine BeginFrame → 注册的 overlay callback 内做
//      ImGui_ImplVulkan_RenderDrawData → EndFrame；
//   5. multi-viewport（拖出主窗口的 ImGui floating window）平台帧。
//
// 实现按"巨型类切分到多个 TU"的常见做法：本头是唯一声明，跨多个 .cpp
// 文件分组定义成员函数：
//   * EditorRenderLayer.cpp        —— 构造 / 析构 / OnUpdate / OnEvent /
//                                     dock layout / main menu / pending op /
//                                     Assets / Console
//   * panels/ScenePanel.cpp        —— Scene 面板（off-screen pipeline +
//                                     sampler + descriptor set + ImGui::Image）
//   * panels/EntityTreePanel.cpp   —— Entity Tree 面板（递归绘制 + DnD +
//                                     rename 状态机）
//   * panels/InspectorPanel.cpp    —— Inspector 面板（每个组件一个 Draw*
//                                     段 + + Add Component）
// 字段全部 private，跨 TU 仅靠成员函数访问。

#include "EditorHost.h"

#include <orange/engine/app/AppHost.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/app/Layer.h>
#include <orange/engine/physics/PhysicsWorld.h>
#include <orange/engine/platform/WindowEvent.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/VfxSystem.h>
#include <orange/engine/scene/Entity.h>

#include <orange/renderer/RenderDevice.h>
#include <orange/renderer/Renderer.h>

#include <imgui.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>

class EditorRenderLayer : public Orange::Engine::Layer
{
public:
    EditorRenderLayer(Orange::Engine::AppHost&             appHost,
                      Orange::Renderer::RenderDevice&      renderDevice,
                      Orange::Renderer::IRenderer&         renderer,
                      VkDescriptorPool                     descriptorPool,
                      VkDevice                             device,
                      EditorHost&                          editorHost);
    ~EditorRenderLayer() override;

    void OnUpdate(const Orange::Engine::FrameContext& frame) override;
    bool OnEvent(const Orange::Engine::Platform::WindowEvent& event) override;

private:
    // ---- EditorRenderLayer.cpp -----------------------------------------
    static void BuildDefaultLayoutOnce(ImGuiID dockspaceId);
    void DrawMainMenuBar();
    // v0.6.5 c0：独立顶部 toolbar（紧贴 main menu bar 下方），实现在
    // panels/ToolbarPanel.cpp。承载 Save / Play / Pause / Stop / [State]
    // transport controls，居中布局参 Cocos Creator 3.8.8。必须在
    // DockSpaceOverViewport 之前调用，让 BuildWorkOffset 累积生效。
    void DrawMainToolbar();
    // v0.6 c1：每帧把"<scene>[ *] — OrangeEditor" 推到 GLFW 原生窗口
    // title。dirty 状态由 mHost.scene.dirty 决定，scene 名取
    // currentScenePath basename（空路径走 "(unsaved scene)"）。内部
    // 缓存上次 title，仅在变化时调 glfwSetWindowTitle 避免无谓 OS-level
    // 非客户区重绘。
    void UpdateWindowTitle();
    void ResetEntityLocalState();
    // Undo/Redo 之后立即调用：把已被销毁的实体句柄从 EditorHost 各字段里清掉，
    // 避免后续帧对死实体做 DestroySubtree / GetComponent 等操作崩溃。
    void ValidateEntityHandles();
    void ApplyPendingSceneOp();
    void ApplyPendingPlayOp();
    // v0.6 c2：未保存改动确认 modal。pendingCloseAction != None 触发；
    // Save / Discard / Cancel 三选一分别 →  调 SceneOp::Save 然后等下帧
    // dirty=false 自动 dispatch / 立即 dispatch / 重置 pendingCloseAction。
    void DrawUnsavedConfirmPopup();
    // v0.6 c2：执行 pendingCloseAction（Exit/NewScene/OpenScene）+ 清状态。
    // Discard 路径直接调；Save 路径在 dirty 清零后由 DrawUnsavedConfirmPopup
    // 早退分支自动调。
    void DispatchPendingCloseAction();
    void DrawAssetsPanel();
    static void DrawAnimationPanel();
    void DrawConsolePanel(const Orange::Engine::FrameContext& frame);
    // ---- panels/LayersPanel.cpp (v0.6 c5) -------------------------------
    // Layer manifest 编辑：visibility 切换 / 添加 / 删除。
    // Hierarchy panel 的 layer 列 + 右键 "Move to layer >" 在
    // EntityTreePanel.cpp 内。
    void DrawLayersPanel();

    // ---- panels/ScenePanel.cpp -----------------------------------------
    void DrawScenePanel();
    void CreateScenePanelSampler();
    void DestroyScenePanelSampler();
    bool EnsureScenePipeline(std::uint32_t width, std::uint32_t height);
    void RebindSceneDescriptorSetIfNeeded();

    // ---- panels/EntityTreePanel.cpp ------------------------------------
    void DrawEntityTreePanel();
    void DrawEntityNodeRecursive(Orange::Engine::Entity entity);
    void BeginRename(Orange::Engine::Entity entity);
    void CommitRename(Orange::Engine::Entity entity);
    void CancelRename();
    // DnD payload type 标识。ImGui 用此字符串区分不同类型的 drag payload。
    static constexpr const char* kEntityPayload = "ORANGE_EDITOR_ENTITY";

    // ---- panels/InspectorPanel.cpp -------------------------------------
    void DrawInspectorPanel();
    // v0.2.5 commit 3 ~ 10 把所有 DrawInspectorXxx 成员（DirectionalLight /
    // RigidBody / Name / Transform / Hierarchy / ParticleEmitter / Renderable
    // / Collider / Animator）+ 静态 ComponentHeader helper 全数清除：
    //   * 9 个 component 段：见 schema/RegisterBuiltinSchemas.cpp
    //   * CollapsingHeader 包装：见 schema/SchemaInspector.cpp 的
    //     ComponentHeaderLocal（schema 内部 helper）
    //   * +Add Component 菜单：c10 起按 ComponentSchemaRegistry 枚举驱动，
    //     不再 hardcode 单个组件
    // 类外不再 mention 任何具体内置 component 类型 —— 见 InspectorPanel.cpp
    // 顶注释。

    // ---- 字段 -----------------------------------------------------------
    // mAppHost  ：引擎层 AppHost（窗口 / LayerStack / 主循环）；
    // mHost     ：编辑器顶层 EditorHost（聚合 4 个 sub-context + CommandStack
    //              + 后续 plugin registry）。两者均由 main 拥有，layer 持
    //              非拥有引用。EditorHost 命名匹配 Lumix StudioApp / Godot
    //              EditorNode 的工业惯例：editor 自己的 application hub。
    Orange::Engine::AppHost&         mAppHost;
    Orange::Renderer::RenderDevice&  mRenderDevice;
    Orange::Renderer::IRenderer&     mRenderer;
    VkDescriptorPool                 mDescriptorPool;  // owned by main, not by layer
    VkDevice                         mDevice;
    EditorHost&                      mHost;            // owned by main, not by layer

    // ---- Scene 面板 off-screen 渲染状态 ----------------------------------
    std::unique_ptr<Orange::Engine::Render::Pipeline> mpScenePipeline;
    VkSampler                                         mSceneSampler{VK_NULL_HANDLE};
    VkDescriptorSet                                   mSceneDescSet{VK_NULL_HANDLE};
    bool                                              mSceneDescSetDirty{true};
    std::uint32_t                                     mScenePanelWidth{0};
    std::uint32_t                                     mScenePanelHeight{0};
    // 一次性失败保险（Initialize 失败后不再每帧 retry / spam log）。
    bool                                              mScenePipelineFailed{false};
    // 单调递增的编辑器运行时间（秒），每帧累加 deltaSeconds，无论 Play/Edit
    // 状态均推进——供 dissolve 等时间驱动 shader 在 Edit 模式下也能预览动画。
    float                                             mEditorTime{0.0f};

    // 上一帧 GLFW framebuffer size 缓存 —— OnUpdate 每帧 query 当前
    // framebuffer，与缓存比对发现变化即调 `mRenderer.OnResize(...)` 通知
    // OrangeRender 重建 swap-chain。0 哨兵值让首帧总会触发一次 OnResize
    // 与启动 maximize 同步。
    //
    // 修复 bug：v0.4.5 后用户在某台机器报 "resize 后 ImGui 内容只占窗口左上
    // 一小块，剩余黑色空白"。Root cause：OrangeRender BeginFrame 内 swap-
    // chain 自动重建仅依赖 (a) `mSwapchainDirty` 由消费者主动调 OnResize
    // 置位 (b) AcquireNextImage 返回 OUT_OF_DATE。OrangeEditor 漏 wire (a)；
    // (b) 在某些 Vulkan driver 配置下不报 OUT_OF_DATE（driver 自动 scale /
    // blit 容错过头）→ swap-chain 永远不重建 → 渲染到旧 size image →
    // present 到新 size surface 出现"image 在 surface 左上角 + 剩余空白"。
    std::uint32_t                                     mLastFramebufferWidth{0};
    std::uint32_t                                     mLastFramebufferHeight{0};

    // v0.6 c1：上一帧推到 GLFW 的窗口 title 缓存。UpdateWindowTitle 算
    // 新 title 与本字段比对，仅在不同时调 glfwSetWindowTitle。
    std::string                                       mLastWindowTitle;

    // ---- Play Mode simulation 运行时（Edit 态均为 nullptr）--------------
    // Play → Stop 时统一销毁（PhysicsWorld reset 即销毁所有 b2 body；
    // VfxSystem 先 SetVfxSystem(nullptr) + Shutdown 再 reset）。
    std::unique_ptr<Orange::Engine::Physics::PhysicsWorld> mpPhysicsWorld;
    std::unique_ptr<Orange::Engine::Render::VfxSystem>     mpVfxSystem;
};

#endif  // ORANGE_EDITOR_EDITOR_RENDER_LAYER_H
