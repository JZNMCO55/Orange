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
    void ResetEntityLocalState();
    // Undo/Redo 之后立即调用：把已被销毁的实体句柄从 EditorHost 各字段里清掉，
    // 避免后续帧对死实体做 DestroySubtree / GetComponent 等操作崩溃。
    void ValidateEntityHandles();
    void ApplyPendingSceneOp();
    void ApplyPendingPlayOp();
    static void DrawAssetsPanel();
    void DrawConsolePanel(const Orange::Engine::FrameContext& frame);

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
    // CollapsingHeader 包装 —— 多一个 "右键 → Remove Component" 上下文
    // 菜单。outRemove 表示用户本帧请求了移除，调用方在 fields 渲染完后
    // 据此调 RemoveComponent。
    static bool ComponentHeader(const char* label, bool* outRemove,
                                bool defaultOpen = true);
    void DrawInspectorName             (Orange::Engine::Entity e);
    void DrawInspectorTransform        (Orange::Engine::Entity e);
    void DrawInspectorHierarchy        (Orange::Engine::Entity e);
    // DirectionalLight 已迁移到 schema-driven 渲染（v0.2.5 commit 3）；
    // 不再有 DrawInspectorDirectionalLight 成员。后续 component 同步迁移。
    void DrawInspectorRenderable       (Orange::Engine::Entity e);
    // RigidBody 已迁 schema-driven（v0.2.5 commit 4）；声明删除。
    void DrawInspectorCollider         (Orange::Engine::Entity e);
    void DrawInspectorParticleEmitter  (Orange::Engine::Entity e);
    void DrawInspectorAnimator         (Orange::Engine::Entity e);

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

    // ---- Play Mode simulation 运行时（Edit 态均为 nullptr）--------------
    // Play → Stop 时统一销毁（PhysicsWorld reset 即销毁所有 b2 body；
    // VfxSystem 先 SetVfxSystem(nullptr) + Shutdown 再 reset）。
    std::unique_ptr<Orange::Engine::Physics::PhysicsWorld> mpPhysicsWorld;
    std::unique_ptr<Orange::Engine::Render::VfxSystem>     mpVfxSystem;
};

#endif  // ORANGE_EDITOR_EDITOR_RENDER_LAYER_H
