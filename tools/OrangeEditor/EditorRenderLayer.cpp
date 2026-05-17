// EditorRenderLayer 主 TU —— 构造 / 析构 / OnUpdate / OnEvent / dock 布局 /
// main menu / pending scene op / Assets / Console。其余面板（Scene /
// EntityTree / Inspector）在 panels/ 下独立 TU，共享同一类声明
// （EditorRenderLayer.h）。

#include "EditorRenderLayer.h"

#include "DemoWorld.h"
#include "EditorHierarchy.h"
#include "VulkanLoaderShim.h"
#include "command/SetFieldValueCommand.h"

#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>

#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/LayerVisibilitySync.h>
#include <orange/engine/physics/RigidBodyComponent.h>
#include <orange/engine/platform/Window.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/LayerComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <glm/gtc/quaternion.hpp>

#include <orange/renderer/RenderTypes.h>
#include <orange/renderer/VulkanInterop.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <imgui_internal.h>  // DockBuilder* API
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <variant>

namespace
{

// Esc 全局退出（与 Input::KeyCode::Escape 同值）；仅本 TU 用。
constexpr std::int32_t kEscapeKeyRaw = 256;

}  // namespace

// ---------------------------------------------------------------------------
// 构造 / 析构
// ---------------------------------------------------------------------------

EditorRenderLayer::EditorRenderLayer(Orange::Engine::AppHost&             appHost,
                                     Orange::Renderer::RenderDevice&      renderDevice,
                                     Orange::Renderer::IRenderer&         renderer,
                                     VkDescriptorPool                     descriptorPool,
                                     VkDevice                             device,
                                     EditorHost&                          editorHost)
    : Orange::Engine::Layer("EditorRender")
    , mAppHost(appHost)
    , mRenderDevice(renderDevice)
    , mRenderer(renderer)
    , mDescriptorPool(descriptorPool)
    , mDevice(device)
    , mHost(editorHost)
{
    // 注册 swap-chain overlay callback —— 引擎 EndFrame 内 swap-chain
    // 渲染窗口里调一次 ImGui_ImplVulkan_RenderDrawData，把当前帧 ImGui
    // DrawData 录到主窗口 swap-chain image。
    mRenderer.SetSwapchainOverlayCallback(
        [](Orange::Rhi::RHICommandList& cmd,
           std::uint32_t /*w*/, std::uint32_t /*h*/, std::uint64_t /*frameIndex*/)
        {
            void* rawCmd = Orange::Renderer::Interop::GetVulkanCommandBuffer(cmd);
            if (rawCmd != nullptr) {
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),
                                                static_cast<VkCommandBuffer>(rawCmd));
            }
        });

    // Scene 面板 ImGui::Image 用的 sampler —— 与 MakeImguiDescriptorPool
    // 同一条 loader 解析路径（OrangeRender 内 volk 已加载的
    // vkGetInstanceProcAddr），不依赖静态链 vulkan-1.lib。
    CreateScenePanelSampler();
}

EditorRenderLayer::~EditorRenderLayer()
{
    // 清 callback 避免捕获已销毁资源
    mRenderer.SetSwapchainOverlayCallback({});

    // Pipeline 先释放（持有 viewportColor / hdrColor 等 RHI 资源），
    // ImGui_ImplVulkan_Shutdown 在 main() 已先于 layer dtor 执行，所以
    // 这里**不**再调 ImGui_ImplVulkan_RemoveTexture（ImGui 内部 pool
    // 已 dead，descriptor set 同时 free，避免 use-after-free）。Pipeline
    // 仍可安全 Shutdown —— 它走 mRenderDevice.WaitIdle + RHI 资源释放。
    mpScenePipeline.reset();
    // Sampler 直接走 loader 销毁；main() 的关停序列保证 vkDevice 还活着。
    DestroyScenePanelSampler();
}

// ---------------------------------------------------------------------------
// OnUpdate / OnEvent
// ---------------------------------------------------------------------------

void EditorRenderLayer::OnUpdate(const Orange::Engine::FrameContext& frame)
{
    // ---- 每帧 framebuffer 同步 → 通知 OrangeRender swap-chain rebuild --
    //
    // 修复 bug：v0.4.5 后用户报"resize 后 ImGui 内容只占窗口左上一小块"。
    // Root cause：OrangeRender::BeginFrame 自动重建 swap-chain 仅依赖
    // (a) 消费者主动调 `Renderer::OnResize(...)` 置位 mSwapchainDirty
    // (b) AcquireNextImage 返回 OUT_OF_DATE
    // OrangeEditor 漏 wire (a)；(b) 在某些 driver / DPI / multi-monitor
    // 组合下不报 OUT_OF_DATE（driver 自动 scale），swap-chain 持续旧 size
    // → ImGui 渲染到旧 image 左上区域 → present 到新 surface 出现空白。
    //
    // 修法：OnUpdate 顶部 query GLFW framebuffer size，与上帧缓存对比，
    // 任何变化即 `mRenderer.OnResize`。首帧 0/0 哨兵让启动后第一次同步
    // 总会触发（与 maximize-on-startup workaround 协同确保 swap-chain 在
    // 第一帧前对齐 maximized framebuffer）。
    {
        auto*        pGlfwWindow = static_cast<GLFWwindow*>(
            mAppHost.GetWindow().GetGlfwWindowHandle());
        int          fbW = 0, fbH = 0;
        glfwGetFramebufferSize(pGlfwWindow, &fbW, &fbH);
        const auto newW = static_cast<std::uint32_t>(fbW > 0 ? fbW : 0);
        const auto newH = static_cast<std::uint32_t>(fbH > 0 ? fbH : 0);
        if ((newW != mLastFramebufferWidth || newH != mLastFramebufferHeight)
            && newW > 0 && newH > 0)
        {
            mRenderer.OnResize(newW, newH);
            mLastFramebufferWidth  = newW;
            mLastFramebufferHeight = newH;
        }
    }

    // 无论 Play/Edit 状态都推进编辑器时间，供 dissolve 等时间驱动 shader 预览
    const float dt = static_cast<float>(frame.time.deltaSeconds);
    mEditorTime += dt;
    if (mpScenePipeline != nullptr)
    {
        mpScenePipeline->SetFrameTime(mEditorTime);
    }

    // ---- Play 态 simulation tick（在 ImGui 帧开始前推进，保证
    //      本帧 DrawScenePanel → Pipeline::Render 看到最新状态）-----
    if (mHost.scene.playState == PlayState::Play && mHost.scene.pWorld != nullptr) {
        // Physics step → 把 dynamic body 新位姿写回 ECS Transform
        if (mpPhysicsWorld != nullptr) {
            // v0.6 c4：每帧 Step 之前同步 layer.visible → body enabled。
            // hidden layer 的 dynamic body 不参与积分 / 不产生 contact，匹配
            // "hide 一个 layer 整个 layer 不要参与物理"的 UX 预期。
            // partition 由 EditorSceneContext 值成员持有，始终 valid。
            Orange::Engine::Physics::ApplyLayerVisibility(
                *mHost.scene.pWorld, mHost.scene.partition, *mpPhysicsWorld);
            mpPhysicsWorld->Step(dt);
            auto& reg = mHost.scene.pWorld->Registry();
            using TC  = Orange::Engine::Scene::TransformComponent;
            using namespace Orange::Engine::Physics;
            for (auto e : reg.view<RigidBodyComponent>()) {
                auto& rb = reg.get<RigidBodyComponent>(e);
                if (rb.type == BodyType::Static) { continue; }
                if (!mpPhysicsWorld->IsValid(rb.handle)) { continue; }
                const BodyTransform xf = mpPhysicsWorld->GetBodyTransform(rb.handle);
                auto* tc = reg.try_get<TC>(e);
                if (tc != nullptr) {
                    tc->position.x = xf.position.x;
                    tc->position.y = xf.position.y;
                    // 2D 物理只有 Z 轴旋转，直接从角度重建 quat
                    tc->rotation = glm::quat(glm::vec3(0.0f, 0.0f, xf.angle));
                }
            }
        }

        // Particle emitter tick
        if (mpVfxSystem != nullptr) {
            mpVfxSystem->Tick(*mHost.scene.pWorld, dt);
            // 诊断：每秒打一次粒子计数，确认 sim 是否正常运行
            static float sDiagTimer = 0.0f;
            sDiagTimer += dt;
            if (sDiagTimer >= 1.0f) {
                sDiagTimer = 0.0f;
                std::fprintf(stdout, "[vfx-diag] live particles: %zu\n",
                             mpVfxSystem->TotalLiveParticleCount());
            }
        }

        // Animator tick
        {
            using namespace Orange::Engine::Animation;
            for (auto e : mHost.scene.pWorld->Registry().view<AnimatorComponent>()) {
                auto& ac = mHost.scene.pWorld->Registry().get<AnimatorComponent>(e);
                if (ac.animator != nullptr) {
                    ac.animator->Tick(dt);
                }
            }
        }
    }

    // ---- ImGui 帧开始 ---------------------------------------------
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Ctrl+Z / Ctrl+Y 全局 Undo/Redo —— 仅 Edit 态且无文本输入焦点时响应。
    // WantTextInput 阻止：InputText 活跃时 Z/Y 是正常字符输入，不应触发撤销。
    // cmdStack 现在是 EditorHost 的值成员（v0.2.5 commit 2），不再需要 null 检查。
    if (mHost.scene.playState == PlayState::Edit
        && !ImGui::GetIO().WantTextInput)
    {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
            mHost.cmdStack.Undo();
            ValidateEntityHandles();  // 清除可能被 Undo 销毁的实体句柄
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y)
            || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) {
            mHost.cmdStack.Redo();
            ValidateEntityHandles();  // 清除可能被 Redo 恢复/销毁的实体句柄
        }
    }

    // dock space —— 占满主 viewport，所有 imgui window 都可以 dock 进来。
    // DockSpaceOverViewport 返回的 ID 在主 viewport 生命周期内稳定，下面
    // DockBuilder 系列 API 用它建默认布局。
    const ImGuiID dockspaceId =
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    BuildDefaultLayoutOnce(dockspaceId);
    DrawMainMenuBar();
    UpdateWindowTitle();

    // 六个固定面板：Scene / Entity Tree / Inspector / Assets / Console /
    // Animation（v0.5 c2 起 Animation 加入底部 tab 容器，与 Cocos Creator 3.6.0
    // 底部布局对齐；本期仅占位，状态机图编辑由 v0.7 实施）。
    DrawScenePanel();
    DrawEntityTreePanel();
    DrawInspectorPanel();
    DrawLayersPanel();
    DrawAssetsPanel();
    DrawConsolePanel(frame);
    DrawAnimationPanel();

    // v0.6 c2：未保存确认 popup —— 必须在 ApplyPendingSceneOp 之前，让
    // popup 的 Save 按钮设置的 pendingSceneOp 在同帧 ApplyPendingSceneOp
    // 内被消费；popup 自身用 BeginPopupModal 阻塞 ImGui 帧逻辑（背后
    // panels 仍画但不响应输入），用户点 Save/Discard/Cancel 后才继续。
    DrawUnsavedConfirmPopup();

    // 帧末统一 apply 场景级操作。放在 panel 绘制完之后、ImGui::Render
    // 之前 —— 文件对话框是模态阻塞窗口，它内部会 pump 一些消息但不
    // 影响本帧的 ImGui DrawData；swap world 之后的 selectedEntity /
    // renamingEntity / euler 缓存清理也在此发生，下一帧才用新状态画。
    ApplyPendingSceneOp();
    ApplyPendingPlayOp();

    ImGui::Render();

    // ---- engine frame：BeginFrame → (overlay callback fires
    //      ImGui_ImplVulkan_RenderDrawData) → EndFrame ----------------
    Orange::Renderer::FrameTimeInfo time{};
    time.mTotalTimeSeconds = frame.time.totalSeconds;
    time.mDeltaTimeSeconds = static_cast<float>(frame.time.deltaSeconds);
    if (Orange::Failed(mRenderer.BeginFrame(time))) {
        std::fprintf(stderr, "[OrangeEditor] BeginFrame failed\n");
        mAppHost.RequestExit();
        return;
    }
    // 编辑器不渲染任何 SubmitItem 内容 —— 仅靠 overlay callback 内的
    // ImGui draw data。FrameLifecycle 在 hasDraw=false + overlay 已
    // 注册时会强制走 begin/end rendering 路径（FEATURE-2026-05-09 修
    // 复的 overlay-on-empty-frame bug），callback 仍能正常触发。
    if (Orange::Failed(mRenderer.EndFrame())) {
        std::fprintf(stderr, "[OrangeEditor] EndFrame failed\n");
        mAppHost.RequestExit();
        return;
    }

    // ---- multi-viewport：让 ImGui 渲染所有"已拖出主窗口"的额外
    //      viewport 到它们各自的 native window 上 -------------------
    ImGuiIO& io = ImGui::GetIO();
    if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

bool EditorRenderLayer::OnEvent(const Orange::Engine::Platform::WindowEvent& event)
{
    // ImGui_ImplGlfw_InitForVulkan(true) 时 install_callbacks=true，
    // ImGui 会自己装 GLFW 回调拿到所有事件 —— 这里**不**再转发
    // KeyEvent，避免双触发。仅把 Esc 作为编辑器的全局退出快捷键拦
    // 截。不检查 WantCaptureKeyboard：NavEnableKeyboard 开启后 ImGui
    // 几乎永远占着键盘焦点（demo / dock 任一可导航 widget 在就会拿
    // WantCaptureKeyboard=true），那样 Esc 永远到不了这里。Esc=quit
    // 是 scaffold 选定的开发期约定，与 ImGui 的常规键盘交互不会冲突。
    const auto* key = std::get_if<Orange::Engine::Platform::KeyEvent>(&event);
    if (key == nullptr) { return false; }
    if (key->action != Orange::Engine::Platform::KeyAction::Press) { return false; }
    if (key->key != kEscapeKeyRaw) { return false; }
    // v0.6 c2：dirty 时 Esc 拦截走未保存确认 popup（不直接 RequestExit）。
    if (mHost.scene.dirty) {
        if (mHost.scene.pendingCloseAction == PendingCloseAction::None) {
            mHost.scene.pendingCloseAction = PendingCloseAction::Exit;
        }
        return true;
    }
    std::fprintf(stdout, "[OrangeEditor] Esc 按下，请求退出\n");
    mAppHost.RequestExit();
    return true;
}

// ---------------------------------------------------------------------------
// Dock 默认布局
// ---------------------------------------------------------------------------

// 首帧（或 imgui.ini 还没存过布局时）建默认 dock 布局。判定条件用
// DockBuilderGetNode → 子节点为空，这样能兼容两种场景：
//   * 首次启动 / 删了 imgui.ini —— 节点存在但无子，建布局；
//   * 已有保存的布局 —— 节点有子，跳过、尊重用户调整。
// 注意 DockBuilder* 来自 imgui_internal.h，是 ImGui 公开但内部稳定度
// 比 imgui.h 略低的 API；编辑器侧使用是 ImGui 官方推荐路径。
void EditorRenderLayer::BuildDefaultLayoutOnce(ImGuiID dockspaceId)
{
    ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
    if (node != nullptr && node->IsSplitNode()) { return; }

    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId,
                              ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId,
                                  ImGui::GetMainViewport()->Size);

    // 布局：左 20% Entity Tree；右 25% Inspector；下 30% Assets/Console
    // tab；剩余中央留给 Scene。比例与 Unity / Unreal 默认 layout 接近，
    // 后续可让用户调；ImGui 会把改动写回 imgui.ini，下次启动恢复。
    ImGuiID center = dockspaceId;
    ImGuiID left   = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left,
                                                0.20f, nullptr, &center);
    ImGuiID right  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right,
                                                0.25f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down,
                                                0.30f, nullptr, &center);

    ImGui::DockBuilderDockWindow("Entity Tree", left);
    // v0.6 c5：Layers 面板与 Entity Tree dock 在同一节点（tab 共存）。
    // 默认 tab 顺序：Entity Tree → Layers；user 可拖出独立 dock 或换序。
    ImGui::DockBuilderDockWindow("Layers",      left);
    ImGui::DockBuilderDockWindow("Inspector",   right);
    // 底部 tab 容器（v0.5 c2，参 Cocos Creator 3.6.0 底部三 tab 布局）
    // 同节点 = tab。Animation 当前是 placeholder，v0.7 状态机图编辑落地后
    // 替换 panel 内容。Assets 内容 v0.5 c3 落地（目录树 + 文件列表）。
    ImGui::DockBuilderDockWindow("Assets",      bottom);
    ImGui::DockBuilderDockWindow("Console",     bottom);
    ImGui::DockBuilderDockWindow("Animation",   bottom);
    ImGui::DockBuilderDockWindow("Scene",       center);

    ImGui::DockBuilderFinish(dockspaceId);
}

// ---------------------------------------------------------------------------
// Main menu bar / scene op
// ---------------------------------------------------------------------------

// v0.6 c1：原生窗口 title 同步 scene 名 + dirty 状态。
// 格式（VS Code / Lumix 同款 "file [*] — app" 工业惯例）：
//   "demo.scene.json — OrangeEditor"        无 dirty / 已保存
//   "demo.scene.json * — OrangeEditor"      有未保存改动
//   "(unsaved scene) * — OrangeEditor"      新建未保存
// dirty backend (mHost.scene.dirty) 自 v0.2 cmdStack.SetOnChanged 钩子起
// 已 wired；本函数仅消费 + 推 GLFW。mLastWindowTitle 缓存避免每帧 GLFW
// SetWindowTextW（Win32 非客户区重绘）spam。
void EditorRenderLayer::UpdateWindowTitle()
{
    const std::string& path = mHost.scene.currentScenePath;
    std::string sceneName;
    if (path.empty())
    {
        sceneName = "(unsaved scene)";
    }
    else
    {
        const auto slash = path.find_last_of('/');
        sceneName = (slash == std::string::npos)
                  ? path
                  : path.substr(slash + 1);
    }
    const char* dirtyMark = mHost.scene.dirty ? " *" : "";
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s%s \xE2\x80\x94 OrangeEditor",
                  sceneName.c_str(), dirtyMark);
    if (mLastWindowTitle == buf) { return; }
    mLastWindowTitle = buf;
    auto* pGlfw = static_cast<GLFWwindow*>(
        mAppHost.GetWindow().GetGlfwWindowHandle());
    if (pGlfw != nullptr) { glfwSetWindowTitle(pGlfw, buf); }
}

// v0.6 c2：执行 pendingCloseAction（Exit/NewScene/OpenScene）+ 清状态。
// Discard 按钮直接调；Save 按钮路径在 dirty 清零后由
// DrawUnsavedConfirmPopup 早退分支自动调。
void EditorRenderLayer::DispatchPendingCloseAction()
{
    switch (mHost.scene.pendingCloseAction)
    {
        case PendingCloseAction::Exit:
            mAppHost.RequestExit();
            break;
        case PendingCloseAction::NewScene:
            mHost.scene.pendingSceneOp = SceneOp::New;
            break;
        case PendingCloseAction::OpenScene:
            mHost.scene.pendingSceneOp = SceneOp::Open;
            break;
        case PendingCloseAction::None:
            break;
    }
    mHost.scene.pendingCloseAction = PendingCloseAction::None;
}

// v0.6 c2：未保存改动 modal 确认。状态机：
//   pendingCloseAction != None + dirty=true   → 显示 popup
//   pendingCloseAction != None + dirty=false  → 静默 dispatch（Save 完成后路径）
//   pendingCloseAction == None                → 不显示
// Save 按钮转 SceneOp::Save → ApplyPendingSceneOp 帧末执行；下帧 dirty 清
// 零自动 dispatch pendingCloseAction（Save 失败 → dirty 仍 true → popup 重
// 新弹让用户重试或 Cancel）。
void EditorRenderLayer::DrawUnsavedConfirmPopup()
{
    if (mHost.scene.pendingCloseAction == PendingCloseAction::None) { return; }
    if (!mHost.scene.dirty)
    {
        DispatchPendingCloseAction();
        return;
    }

    static constexpr const char* kPopupId = "##unsaved_confirm";
    ImGui::OpenPopup(kPopupId);
    if (ImGui::BeginPopupModal(kPopupId, nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("当前 scene 有未保存的改动。");
        ImGui::TextDisabled("Save = 保存后继续  /  Discard = 丢弃改动继续  /  Cancel = 取消");
        ImGui::Separator();
        if (ImGui::Button("Save"))
        {
            mHost.scene.pendingSceneOp = SceneOp::Save;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard"))
        {
            DispatchPendingCloseAction();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            mHost.scene.pendingCloseAction = PendingCloseAction::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// 主菜单栏（File / View / Help ...）。BeginMainMenuBar 创建一个固定
// 顶部的浮动 bar，与 DockSpaceOverViewport 共存 —— ImGui 自动把
// dockspace 下移留出 menu bar 高度。文件操作不在此立即执行：菜单点
// 击仅设置 pendingSceneOp，真正的 dialog + Save/Load 调用走帧末
// ApplyPendingSceneOp。
//
// 快捷键 Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S 在菜单 label 处只
// 是显示文本，真要响应快捷键需要在 OnUpdate 里检 IsKeyPressed +
// ModCtrl。本 task 范围内菜单点击足以验收 Save/Load 流程；快捷键留
// 给后续微调（同时也避免与 Entity Tree 面板的 F2/Del 冲突）。
void EditorRenderLayer::DrawMainMenuBar()
{
    if (!ImGui::BeginMainMenuBar()) { return; }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Scene")) {
            // v0.6 c2：dirty 时拦截走未保存确认 popup（DispatchPendingCloseAction
            // 在 popup 走完 Save/Discard 后会重设 pendingSceneOp）。
            if (mHost.scene.dirty) {
                mHost.scene.pendingCloseAction = PendingCloseAction::NewScene;
            } else {
                mHost.scene.pendingSceneOp = SceneOp::New;
            }
        }
        if (ImGui::MenuItem("Open Scene...")) {
            if (mHost.scene.dirty) {
                mHost.scene.pendingCloseAction = PendingCloseAction::OpenScene;
            } else {
                mHost.scene.pendingSceneOp = SceneOp::Open;
            }
        }
        ImGui::Separator();
        // Save 亮判定 = "world 自上次保存/加载后被改过"。currentScenePath 是
        // 否非空不再作为前置条件——empty 时点 Save 会自动转 SaveAs 流程（见
        // ApplyPendingSceneOp 的 Save 分支）。
        const bool canQuickSave = mHost.scene.dirty;
        if (ImGui::MenuItem("Save", nullptr, false, canQuickSave)) {
            mHost.scene.pendingSceneOp = SceneOp::Save;
        }
        if (ImGui::MenuItem("Save Scene As...")) {
            mHost.scene.pendingSceneOp = SceneOp::SaveAs;
        }
        ImGui::Separator();
        // v0.6 c6：多文件 + manifest 路径——把每条 layer 的 entity 单独
        // 拆到 per-layer .scene.json，最后写 manifest 文件。多人编辑 / VCS
        // 友好（per-layer 文件独立 diff）；dirty 状态、未保存确认、Play
        // 快照仍走单文件 Save 路径（避免编辑器内多种序列化模式互撞）。
        if (ImGui::MenuItem("Save Split As...")) {
            mHost.scene.pendingSceneOp = SceneOp::SaveSplitAs;
        }
        if (ImGui::MenuItem("Open Split...")) {
            if (mHost.scene.dirty) {
                mHost.scene.pendingCloseAction = PendingCloseAction::OpenScene;
            } else {
                mHost.scene.pendingSceneOp = SceneOp::OpenSplit;
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) {
            // v0.6 c2：dirty 时拦截走未保存确认 popup。
            if (mHost.scene.dirty) {
                mHost.scene.pendingCloseAction = PendingCloseAction::Exit;
            } else {
                mAppHost.RequestExit();
            }
        }
        ImGui::EndMenu();
    }

    // ---- Save + Play / Pause / Stop 按钮（v0.6 c3 起 Save 加入）-------
    // 主菜单栏右侧：Save | Play | Pause | Stop | [State]。
    // Save 是 File 菜单 "Save" 项的 toolbar 主入口 —— File 菜单仍保留作
    // 为键盘快捷键 hint + 后备入口（与 Lumix / Godot / Cocos 同款双入口
    // 惯例）。dirty 时高亮：硬编码 accent 蓝（v0.6.5 视觉统一 milestone
    // 会迁到 EditorTheme token；当前 EditorTheme 尚未存在，无法 token 化）。
    // disabled / enabled 按当前 playState + dirty 推算（典型 transport-
    // control 风格：Play 在 Edit / Paused 可用，Pause 仅 Play 可用，Stop
    // 仅 Play / Paused 可用；Save 仅 dirty 可用——与 File>Save 同款条件）。
    {
        const PlayState ps = mHost.scene.playState;
        const char* stateLabel = (ps == PlayState::Edit)   ? "[Edit]"
                               : (ps == PlayState::Play)   ? "[Play]"
                                                           : "[Paused]";
        // 按钮 / 状态 label 宽度全部从 CalcTextSize 派生 —— v0.4 期硬编码
        // 36 / 70 像素在 1680×1120 × 150% scale 上会被字体撑爆 (Segoe UI
        // 24px 下 "Pause" 文本宽 ~44px > 36px button)。FramePadding 已被
        // main.cpp ImGui::GetStyle().ScaleAllSizes(dpiScale) 同步缩放。
        const ImGuiStyle& style = ImGui::GetStyle();
        const float framePadX   = style.FramePadding.x * 2.0f;
        const float btnSaveW    = ImGui::CalcTextSize("Save").x  + framePadX;
        const float btnPlayW    = ImGui::CalcTextSize("Play").x  + framePadX;
        const float btnPauseW   = ImGui::CalcTextSize("Pause").x + framePadX;
        const float btnStopW    = ImGui::CalcTextSize("Stop").x  + framePadX;
        // state label 取三种状态最长那个 + framePad，保证切换时右边距稳定。
        // 用 (std::max)(...) 圆括号包装绕开 windows.h max 宏污染（本 TU 通
        // 过 imgui_internal.h / GLFW backends 间接拉 windows.h，没 #define
        // NOMINMAX；main.cpp 那侧定义了 NOMINMAX 但不影响本 TU）。
        const float stateW = (std::max)(
            ImGui::CalcTextSize("[Edit]").x,
            (std::max)(ImGui::CalcTextSize("[Play]").x,
                       ImGui::CalcTextSize("[Paused]").x))
            + framePadX;
        const float itemSpc = style.ItemSpacing.x;
        const float groupW = btnSaveW + btnPlayW + btnPauseW + btnStopW + stateW
                           + 5.0f * itemSpc;
        ImGui::SameLine(ImGui::GetWindowWidth() - groupW);

        // Save 按钮：dirty 时 accent 高亮。颜色硬编码——v0.6.5 视觉统一
        // milestone 引入 EditorTheme token 后替换为 EditorTheme::Accent。
        const bool dirty = mHost.scene.dirty;
        if (dirty) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                ImVec4(0.30f, 0.55f, 0.95f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                ImVec4(0.15f, 0.40f, 0.80f, 1.0f));
        }
        ImGui::BeginDisabled(!dirty);
        if (ImGui::Button("Save", ImVec2(btnSaveW, 0))) {
            mHost.scene.pendingSceneOp = SceneOp::Save;
        }
        ImGui::EndDisabled();
        if (dirty) { ImGui::PopStyleColor(3); }

        const bool canPlay   = (ps == PlayState::Edit  || ps == PlayState::Paused);
        const bool canPause  = (ps == PlayState::Play);
        const bool canStop   = (ps == PlayState::Play  || ps == PlayState::Paused);
        ImGui::SameLine();
        ImGui::BeginDisabled(!canPlay);
        if (ImGui::Button("Play", ImVec2(btnPlayW, 0))) {
            mHost.scene.pendingPlayOp = (ps == PlayState::Paused) ? PlayOp::Resume
                                                             : PlayOp::EnterPlay;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!canPause);
        if (ImGui::Button("Pause", ImVec2(btnPauseW, 0))) {
            mHost.scene.pendingPlayOp = PlayOp::Pause;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!canStop);
        if (ImGui::Button("Stop", ImVec2(btnStopW, 0))) {
            mHost.scene.pendingPlayOp = PlayOp::Stop;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", stateLabel);
    }

    // 当前 scene 路径作为只读 indicator 显示在菜单栏左侧（File 菜单后）。
    // 注：以前显示在右侧，被 Play 按钮挤掉了 —— scene 名次要、Play 状态
    // 高频读，按编辑器惯例优先级 Play 控件靠右。
    const std::string& path = mHost.scene.currentScenePath;
    const char* sceneLabel  = path.empty() ? "[Untitled]" : path.c_str();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", sceneLabel);
    ImGui::EndMainMenuBar();
}

// 检查 EditorHost 里各实体句柄是否仍在 registry 中存活；对已被 Undo
// 销毁的实体清零，防止后续帧在死实体上调 DestroySubtree / GetComponent。
// 与 ResetEntityLocalState 的区别：Reset 是全清（切场景用），Validate
// 是精准检查（每次 Undo/Redo 后用）。
void EditorRenderLayer::ValidateEntityHandles()
{
    if (mHost.scene.pWorld == nullptr) { return; }
    auto& w = *mHost.scene.pWorld;

    if (mHost.selection.selectedEntity.IsValid() && !w.IsValid(mHost.selection.selectedEntity)) {
        mHost.selection.selectedEntity            = Orange::Engine::Entity::Invalid();
        mHost.selection.transformEulerCacheEntity = Orange::Engine::Entity::Invalid();
    }
    if (mHost.selection.renamingEntity.IsValid() && !w.IsValid(mHost.selection.renamingEntity)) {
        CancelRename();
    }
    if (mHost.selection.transformEulerCacheEntity.IsValid()
        && !w.IsValid(mHost.selection.transformEulerCacheEntity)) {
        mHost.selection.transformEulerCacheEntity = Orange::Engine::Entity::Invalid();
    }
    // pendingDelete / pendingReparent / pendingCreate 是帧内消耗完的一次性
    // 标志（DrawEntityTreePanel 末尾 apply），跨帧不存活，无需在此验证。
}

// 把 EditorHost 内与"被编辑 world 实体身份强相关"的状态全清空。
// Open / New 切 world 后必须调；不调的话 selectedEntity 会指向新 world
// 里不存在的 entity，Inspector 看到野指针。
void EditorRenderLayer::ResetEntityLocalState()
{
    mHost.selection.selectedEntity            = Orange::Engine::Entity::Invalid();
    mHost.selection.renamingEntity            = Orange::Engine::Entity::Invalid();
    mHost.selection.renameBuffer[0]           = '\0';
    mHost.selection.renameJustStarted         = false;
    mHost.selection.pendingDelete             = Orange::Engine::Entity::Invalid();
    mHost.selection.pendingReparent.valid     = false;
    mHost.selection.pendingCreate.valid       = false;
    mHost.selection.transformEulerCacheEntity = Orange::Engine::Entity::Invalid();
}

// 帧末统一 apply 用户菜单点击的场景操作。dialog 阻塞期 ImGui 主循环
// 等待，可接受 —— 编辑器无实时帧率要求。失败 / 取消都仅 stderr 记
// 录，不弹 modal，与项目"日志走 stderr，等 Core::Log 接入再改"的过
// 渡期惯例一致。
void EditorRenderLayer::ApplyPendingSceneOp()
{
    const SceneOp op = mHost.scene.pendingSceneOp;
    if (op == SceneOp::None) { return; }
    mHost.scene.pendingSceneOp = SceneOp::None;

    // 拿主窗口 HWND 给 dialog 当 parent，确保 dialog 居中 + 抢焦点。
    auto* gw = static_cast<GLFWwindow*>(
        mAppHost.GetWindow().GetGlfwWindowHandle());
    void* hwnd = (gw != nullptr) ? static_cast<void*>(glfwGetWin32Window(gw)) : nullptr;

    switch (op) {
        case SceneOp::New: {
            mHost.scene.pWorld = std::make_unique<Orange::Engine::World>();
            // v0.6 c4：partition 与 pWorld 同生命周期，scene swap 时一并重建。
            // 默认构造自动注册 "default" layer，SeedDemoWorld 内挂的 entity
            // 在没显式 SetLayerOf 时自然归 default。
            mHost.scene.partition = Orange::Engine::Scene::WorldPartition{};
            SeedDemoWorld(mHost);  // 与启动期一致；后续真要"空场景"再做"New Empty"
            mHost.scene.currentScenePath.clear();
            mHost.scene.dirty = false;
            ResetEntityLocalState();
            mHost.cmdStack.Clear();
            std::fprintf(stdout, "[OrangeEditor] new scene (seeded demo world)\n");
            break;
        }
        case SceneOp::Open: {
            std::string path;
            if (!ShowSceneFileDialog(/*isSave=*/false, hwnd, path)) { break; }
            auto pNew = std::make_unique<Orange::Engine::World>();
            const auto namedMat = BuildNamedMaterialInstances(mHost.assets);
            Orange::Engine::Scene::LoadOptions openLoadOpts;
            openLoadOpts.assetRegistry          = mHost.assets.pAssets.get();
            openLoadOpts.animatorRegistry       = mHost.assets.pAnimators.get();
            openLoadOpts.namedMaterialInstances = &namedMat;
            openLoadOpts.extraSerializers       = mHost.extraSerializers;
            auto rc = Orange::Engine::Scene::Load(path, *pNew, openLoadOpts);
            if (rc.IsErr()) {
                std::fprintf(stderr,
                             "[OrangeEditor] Scene::Load failed: %s (code=%u)\n",
                             path.c_str(),
                             static_cast<unsigned>(rc.Error()));
                break;  // 保留原 world
            }
            mHost.scene.pWorld = std::move(pNew);
            // v0.6 c4：单文件 Load 不读 manifest（partition 元数据没被持久
            // 化）；scene 里只有 LayerComponent.layerId。重建一个空 partition，
            // 再扫一遍 world 把出现过的 layerId 自动 AddLayer（visible 默认
            // true）—— 用户重启后仍能看到完整 layer 列表，已删 layer 上残
            // 留的 entity 也能被 partition 兜底视为 default 之外的合法 layer。
            mHost.scene.partition = Orange::Engine::Scene::WorldPartition{};
            {
                auto& reg = mHost.scene.pWorld->Registry();
                using LC  = Orange::Engine::Scene::LayerComponent;
                for (auto e : reg.view<LC>()) {
                    const auto& lc = reg.get<LC>(e);
                    if (lc.layerId.empty()) { continue; }
                    if (mHost.scene.partition.HasLayer(lc.layerId)) { continue; }
                    Orange::Engine::Scene::LayerInfo info;
                    info.id          = lc.layerId;
                    info.displayName = lc.layerId;
                    info.visible     = true;
                    mHost.scene.partition.AddLayer(std::move(info));
                }
            }
            mHost.scene.currentScenePath = path;
            mHost.scene.dirty = false;
            ResetEntityLocalState();
            mHost.cmdStack.Clear();
            std::fprintf(stdout, "[OrangeEditor] opened scene: %s\n", path.c_str());
            break;
        }
        case SceneOp::Save: {
            if (mHost.scene.currentScenePath.empty()) {
                // 没保存过 → 转 SaveAs。
                std::string path;
                if (!ShowSceneFileDialog(/*isSave=*/true, hwnd, path)) { break; }
                mHost.scene.currentScenePath = std::move(path);
            }
            {
                const auto namedMat = BuildNamedMaterialInstances(mHost.assets);
                Orange::Engine::Scene::SaveOptions saveOpts;
                saveOpts.assetRegistry          = mHost.assets.pAssets.get();
                saveOpts.namedMaterialInstances = &namedMat;
                saveOpts.extraSerializers       = mHost.extraSerializers;
                auto rc = Orange::Engine::Scene::Save(
                    *mHost.scene.pWorld, mHost.scene.currentScenePath, saveOpts);
                if (rc.IsErr()) {
                    std::fprintf(stderr,
                                 "[OrangeEditor] Scene::Save failed: %s (code=%u)\n",
                                 mHost.scene.currentScenePath.c_str(),
                                 static_cast<unsigned>(rc.Error()));
                } else {
                    mHost.scene.dirty = false;
                    std::fprintf(stdout, "[OrangeEditor] saved scene: %s\n",
                                 mHost.scene.currentScenePath.c_str());
                }
            }
            break;
        }
        case SceneOp::SaveAs: {
            std::string path;
            if (!ShowSceneFileDialog(/*isSave=*/true, hwnd, path)) { break; }
            const auto namedMat = BuildNamedMaterialInstances(mHost.assets);
            Orange::Engine::Scene::SaveOptions saveAsOpts;
            saveAsOpts.assetRegistry          = mHost.assets.pAssets.get();
            saveAsOpts.namedMaterialInstances = &namedMat;
            saveAsOpts.extraSerializers       = mHost.extraSerializers;
            auto rc = Orange::Engine::Scene::Save(*mHost.scene.pWorld, path, saveAsOpts);
            if (rc.IsErr()) {
                std::fprintf(stderr,
                             "[OrangeEditor] Scene::Save failed: %s (code=%u)\n",
                             path.c_str(),
                             static_cast<unsigned>(rc.Error()));
                break;
            }
            mHost.scene.currentScenePath = std::move(path);
            mHost.scene.dirty = false;
            std::fprintf(stdout, "[OrangeEditor] saved scene as: %s\n",
                         mHost.scene.currentScenePath.c_str());
            break;
        }
        case SceneOp::SaveSplitAs: {
            // v0.6 c6：拆 manifest + per-layer .scene.json 写盘。partition
            // 必须至少含两条 layer（default + 至少一条用户加的）才有意义；
            // 仅一条 layer 仍允许（manifest 只列 default，等价于单文件
            // 但走多文件路径）。源文件名按 layer id 派生：
            // manifestPath = "X.scene.manifest.json" → 同目录写
            // "<layer.id>.scene.json"；落盘前更新 partition.layer.source
            // 让 manifest 写出的 source 字段反映本次实际路径。
            std::string manifestPath;
            if (!ShowManifestFileDialog(/*isSave=*/true, hwnd, manifestPath)) { break; }
            // 为每条 layer 赋一个稳定的 source 文件名（若 partition 内已
            // 有 source 字段则保留，方便"反复 SaveSplitAs 到同名 manifest
            // 不改 layer 文件名"）。遍历 GetLayers() 拿 id，再通过非 const
            // GetLayer(id) 拿可改 LayerInfo*；不直接 mutate GetLayers()
            // 返回的 const& vector。
            const auto& layersView = mHost.scene.partition.GetLayers();
            for (const auto& l : layersView) {
                if (auto* mutInfo = mHost.scene.partition.GetLayer(l.id);
                    mutInfo != nullptr && mutInfo->source.empty())
                {
                    mutInfo->source = l.id + ".scene.json";
                }
            }
            const auto namedMat = BuildNamedMaterialInstances(mHost.assets);
            Orange::Engine::Scene::SaveOptions saveOpts;
            saveOpts.assetRegistry          = mHost.assets.pAssets.get();
            saveOpts.namedMaterialInstances = &namedMat;
            saveOpts.extraSerializers       = mHost.extraSerializers;
            const auto rc = Orange::Engine::Scene::SaveSplit(
                *mHost.scene.pWorld, mHost.scene.partition, manifestPath, saveOpts);
            if (rc.IsErr()) {
                std::fprintf(stderr,
                             "[OrangeEditor] Scene::SaveSplit failed: %s (code=%u)\n",
                             manifestPath.c_str(),
                             static_cast<unsigned>(rc.Error()));
                break;
            }
            // currentScenePath 此刻指向 manifest 文件；后续 File>Save 仍
            // 走单文件 Save 路径（覆盖 manifest 文件本身），与多文件 split
            // 路径**不**互通——用户后续要继续 split 落盘必须再走 Save
            // Split As。这条限制写进 v0.6 acceptance checklist 的"已知简化"。
            mHost.scene.currentScenePath = std::move(manifestPath);
            mHost.scene.dirty = false;
            std::fprintf(stdout, "[OrangeEditor] saved scene (split) as: %s (%zu layers)\n",
                         mHost.scene.currentScenePath.c_str(),
                         mHost.scene.partition.LayerCount());
            break;
        }
        case SceneOp::OpenSplit: {
            std::string manifestPath;
            if (!ShowManifestFileDialog(/*isSave=*/false, hwnd, manifestPath)) { break; }
            auto pNew = std::make_unique<Orange::Engine::World>();
            Orange::Engine::Scene::WorldPartition newPartition;
            const auto namedMat = BuildNamedMaterialInstances(mHost.assets);
            Orange::Engine::Scene::LoadOptions splitLoadOpts;
            splitLoadOpts.assetRegistry          = mHost.assets.pAssets.get();
            splitLoadOpts.animatorRegistry       = mHost.assets.pAnimators.get();
            splitLoadOpts.namedMaterialInstances = &namedMat;
            splitLoadOpts.extraSerializers       = mHost.extraSerializers;
            // LoadSplit 内部按 manifest.layers 顺序遍历每条 source，
            // 并通过 LoadOptions.assignLayerId 给本次新建且没挂
            // LayerComponent 的 entity 自动按归属 layer 兜底——不必再
            // 在 editor 侧扫一遍 entity 重建 partition（与单文件 Open 路径
            // 不同：LoadSplit 已经把 partition 灌入完整 manifest）。
            const auto rc = Orange::Engine::Scene::LoadSplit(
                manifestPath, *pNew, newPartition, splitLoadOpts);
            if (rc.IsErr()) {
                std::fprintf(stderr,
                             "[OrangeEditor] Scene::LoadSplit failed: %s (code=%u)\n",
                             manifestPath.c_str(),
                             static_cast<unsigned>(rc.Error()));
                break;
            }
            mHost.scene.pWorld           = std::move(pNew);
            mHost.scene.partition        = std::move(newPartition);
            mHost.scene.currentScenePath = manifestPath;
            mHost.scene.dirty            = false;
            ResetEntityLocalState();
            mHost.cmdStack.Clear();
            std::fprintf(stdout, "[OrangeEditor] opened scene (split): %s (%zu layers)\n",
                         manifestPath.c_str(),
                         mHost.scene.partition.LayerCount());
            break;
        }
        case SceneOp::None:
            break;  // unreachable, 上面已 early return
    }
}

// ---------------------------------------------------------------------------
// Play Mode —— 状态机迁移处理
// ---------------------------------------------------------------------------
void EditorRenderLayer::ApplyPendingPlayOp()
{
    const PlayOp op = mHost.scene.pendingPlayOp;
    if (op == PlayOp::None) { return; }
    mHost.scene.pendingPlayOp = PlayOp::None;

    switch (op) {
        case PlayOp::EnterPlay: {
            if (mHost.scene.playState != PlayState::Edit) { break; }

            // S2: World 快照落盘 —— Stop 时从此路径还原，保证 Play 期
            //     对 ECS 的所有修改（物理驱动 Transform / 粒子spawn）都
            //     能被丢弃，回到 Play 前的编辑状态。
            {
                namespace fs = std::filesystem;
                mHost.scene.playSnapshotPath =
                    (fs::temp_directory_path() /
                     "OrangeEditor_play_snapshot.scene.json").string();
                const auto namedMat = BuildNamedMaterialInstances(mHost.assets);
                Orange::Engine::Scene::SaveOptions saveOpts;
                saveOpts.assetRegistry          = mHost.assets.pAssets.get();
                saveOpts.namedMaterialInstances = &namedMat;
                saveOpts.extraSerializers       = mHost.extraSerializers;
                const auto rc = Orange::Engine::Scene::Save(
                    *mHost.scene.pWorld, mHost.scene.playSnapshotPath, saveOpts);
                if (rc.IsErr()) {
                    std::fprintf(stderr,
                        "[OrangeEditor] Play 快照落盘失败: %s (code=%u) —— 取消进入 Play\n",
                        mHost.scene.playSnapshotPath.c_str(),
                        static_cast<unsigned>(rc.Error()));
                    mHost.scene.playSnapshotPath.clear();
                    break;  // 快照失败则保持 Edit，不进 Play
                }
            }

            // S3: PhysicsWorld 接入 —— 遍历所有同时挂 RigidBody +
            //     Collider 的 entity，从 TransformComponent 填 initial
            //     pos / angle，注册进 PhysicsWorld，handle 反写回 ECS。
            {
                mpPhysicsWorld =
                    std::make_unique<Orange::Engine::Physics::PhysicsWorld>();
                auto& reg = mHost.scene.pWorld->Registry();
                using TC  = Orange::Engine::Scene::TransformComponent;
                using namespace Orange::Engine::Physics;
                for (auto e : reg.view<RigidBodyComponent, ColliderComponent>()) {
                    auto& rb = reg.get<RigidBodyComponent>(e);
                    auto& cc = reg.get<ColliderComponent>(e);
                    const auto* tc = reg.try_get<TC>(e);
                    if (tc != nullptr) {
                        rb.initialPosition =
                            glm::vec2(tc->position.x, tc->position.y);
                        // glm::eulerAngles 返回 (pitch, yaw, roll) 弧度；
                        // 2D 平面物理只用 Z 轴旋转（roll）
                        const glm::vec3 euler = glm::eulerAngles(tc->rotation);
                        rb.initialAngle = euler.z;
                    }
                    const BodyHandle h = mpPhysicsWorld->AddBody(rb, cc);
                    rb.handle = h;
                }
            }

            // S4: VfxSystem 接入 —— 需要 Pipeline 已就绪（Scene 面板
            //     必须至少渲染过一帧才会 lazy init Pipeline）。
            if (mpScenePipeline != nullptr && mHost.assets.pAssets != nullptr) {
                mpVfxSystem =
                    std::make_unique<Orange::Engine::Render::VfxSystem>();
                // framesInFlight 与 Pipeline 同值（典型 2）
                constexpr std::uint32_t kFIF = 2u;
                const auto rc = mpVfxSystem->Initialize(
                    static_cast<void*>(&mRenderDevice), kFIF,
                    *mHost.assets.pAssets);
                if (rc.IsErr()) {
                    std::fprintf(stderr,
                        "[OrangeEditor] VfxSystem::Initialize 失败 (code=%u)\n",
                        static_cast<unsigned>(rc.Error()));
                    mpVfxSystem.reset();
                } else {
                    mpScenePipeline->SetVfxSystem(mpVfxSystem.get());
                    std::fprintf(stdout, "[play] VfxSystem 初始化成功，粒子 tick 已启动\n");
                }
            } else {
                std::fprintf(stderr,
                    "[play] VfxSystem 跳过：Pipeline=%s  Assets=%s\n",
                    mpScenePipeline ? "ok" : "null",
                    mHost.assets.pAssets  ? "ok" : "null");
            }

            mHost.scene.playState = PlayState::Play;
            std::fprintf(stdout, "[play] Edit → Play\n");
            break;
        }
        case PlayOp::Pause: {
            if (mHost.scene.playState != PlayState::Play) { break; }
            mHost.scene.playState = PlayState::Paused;
            std::fprintf(stdout, "[play] Play → Paused\n");
            break;
        }
        case PlayOp::Resume: {
            if (mHost.scene.playState != PlayState::Paused) { break; }
            mHost.scene.playState = PlayState::Play;
            std::fprintf(stdout, "[play] Paused → Play\n");
            break;
        }
        case PlayOp::Stop: {
            if (mHost.scene.playState == PlayState::Edit) { break; }
            const char* prevLabel =
                (mHost.scene.playState == PlayState::Play) ? "Play" : "Paused";

            // S4 拆卸：先断开 Pipeline → VfxSystem 引用，再 Shutdown / reset
            if (mpScenePipeline != nullptr) {
                mpScenePipeline->SetVfxSystem(nullptr);
            }
            if (mpVfxSystem != nullptr) {
                mpVfxSystem->Shutdown();
                mpVfxSystem.reset();
            }

            // S3 拆卸：销毁 PhysicsWorld（所有 b2 body 随之释放）
            mpPhysicsWorld.reset();

            // S2 还原：从快照加载回 Edit 前的 World 状态。
            //   - 传 assetRegistry（mesh / material handle round-trip 需要）
            //   - 传 animatorRegistry：AnimatorComponent 在还原时需要重建
            //     IAnimator backend 实例（backend 名持久化，IAnimator 本身
            //     不序列化）；不传则 component 还原为 animator=nullptr，
            //     Inspector Animator 段会看到空 backend 名
            //   - 不传 physicsWorld：Edit 态不需要运行时 backend，handle 留
            //     Invalid 是正确的 Edit 态初值
            if (!mHost.scene.playSnapshotPath.empty()) {
                auto pNew = std::make_unique<Orange::Engine::World>();
                const auto namedMat = BuildNamedMaterialInstances(mHost.assets);
                Orange::Engine::Scene::LoadOptions loadOpts;
                loadOpts.assetRegistry          = mHost.assets.pAssets.get();
                loadOpts.animatorRegistry       = mHost.assets.pAnimators.get();
                loadOpts.namedMaterialInstances = &namedMat;
                loadOpts.extraSerializers       = mHost.extraSerializers;
                const auto rc = Orange::Engine::Scene::Load(
                    mHost.scene.playSnapshotPath, *pNew, loadOpts);
                if (rc.IsErr()) {
                    std::fprintf(stderr,
                        "[OrangeEditor] Play 快照还原失败 (code=%u)，"
                        "保留 Play 后的 World\n",
                        static_cast<unsigned>(rc.Error()));
                } else {
                    mHost.scene.pWorld = std::move(pNew);
                }
                std::filesystem::remove(mHost.scene.playSnapshotPath);
                mHost.scene.playSnapshotPath.clear();
            }

            mHost.scene.playState = PlayState::Edit;
            ResetEntityLocalState();
            // Stop 时 pWorld 已被换成还原快照的新实例；栈内所有命令的 pW
            // 仍指向旧 World，全部失效 —— 必须在此清栈，否则 Ctrl+Z 会
            // 用悬垂指针执行 lambda 导致崩溃。与 New / Open 场景切换时的
            // 处理保持一致（切 world 必清栈）。
            mHost.cmdStack.Clear();
            std::fprintf(stdout, "[play] %s → Edit\n", prevLabel);
            break;
        }
        case PlayOp::None:
            break;
    }
}

// ---------------------------------------------------------------------------
// 小面板（Assets / Console）—— 占位 + 帧统计
// ---------------------------------------------------------------------------

// v0.5 c3 Asset 浏览器实现 helpers（anonymous namespace 局部可见）。
namespace
{

// 递归画 dir 自身 + 所有子目录 tree node。click 时把 dir 写入
// `assets.browserCurrentDir` 让右侧 file list 刷新。
void DrawAssetTreeRecursive(EditorAssetContext& assets, const std::string& dir)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dir, ec)) { return; }

    // 节点 label = dir 最后一段；root 节点显示完整 "assets"。
    const auto slash = dir.find_last_of('/');
    const std::string label = (slash == std::string::npos)
                            ? dir
                            : dir.substr(slash + 1);

    const bool isSelected = (dir == assets.browserCurrentDir);
    int flags = ImGuiTreeNodeFlags_OpenOnArrow
              | ImGuiTreeNodeFlags_OpenOnDoubleClick
              | ImGuiTreeNodeFlags_DefaultOpen
              | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (isSelected) { flags |= ImGuiTreeNodeFlags_Selected; }

    const bool open = ImGui::TreeNodeEx(dir.c_str(), flags, "%s",
                                        label.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        assets.browserCurrentDir = dir;
    }
    if (open)
    {
        std::vector<std::string> subdirs;
        for (auto& entry : fs::directory_iterator(dir, ec))
        {
            if (entry.is_directory(ec))
            {
                subdirs.push_back(entry.path().generic_string());
            }
        }
        std::sort(subdirs.begin(), subdirs.end());
        for (auto& sub : subdirs)
        {
            DrawAssetTreeRecursive(assets, sub);
        }
        ImGui::TreePop();
    }
}

// 判 path 是否对应一个会触发 Inspector 子模式的 asset 类型（当前仅
// .material；后续 mesh / texture 子模式扩展时在此追加）。文件列表点击
// 路径要据此决定是否清掉 entity 选中（互斥选择，B3 修）。
bool DoesAssetTriggerInspectorSubMode(const std::string& path)
{
    if (path.size() < 9) { return false; }
    return path.compare(path.size() - 9, 9, ".material") == 0;
}

// 当前目录文件列表（不递归）。文件类型按扩展名前缀 [M]/[Mat]/[T]/[S]/[J]/[?]
// 显示，点选写入 `assets.selectedAssetPath`；BeginDragDropSource 起 DnD payload
// "ORANGE_ASSET" 携带 path 字符串供 v0.5 c4 Inspector AssetRef 字段接收。
void DrawAssetFileList(EditorHost& host, EditorAssetContext& assets)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(assets.browserCurrentDir, ec))
    {
        ImGui::TextDisabled("(directory '%s' not found)",
                            assets.browserCurrentDir.c_str());
        return;
    }

    std::vector<fs::path> files;
    for (auto& entry : fs::directory_iterator(assets.browserCurrentDir, ec))
    {
        if (entry.is_regular_file(ec))
        {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    for (const auto& f : files)
    {
        const std::string path = f.generic_string();
        const std::string name = f.filename().string();
        const std::string ext  = f.extension().string();

        const char* icon = "[?]";
        if      (ext == ".mesh" || ext == ".obj")   icon = "[M]";
        else if (ext == ".material")                icon = "[Mat]";
        else if (ext == ".png" || ext == ".jpg"
              || ext == ".jpeg" || ext == ".ktx")   icon = "[T]";
        else if (name.size() >= 11
              && name.compare(name.size() - 11, 11, ".scene.json") == 0)
                                                    icon = "[S]";
        else if (ext == ".json")                    icon = "[J]";

        const bool selected = (path == assets.selectedAssetPath);
        char labelBuf[512];
        std::snprintf(labelBuf, sizeof(labelBuf), "%s %s",
                      icon, name.c_str());
        if (ImGui::Selectable(labelBuf, selected))
        {
            assets.selectedAssetPath = path;
            // B3 修：互斥选择 —— 选中一个会触发 Inspector 子模式的 asset
            // （当前 .material），清掉 entity 选中。这样 Inspector 干净切到
            // 资源编辑视图，与 Cocos / Unity Project 面板手感一致。
            // 不触发子模式的 asset（.mesh / 普通 json 等）选中不清 entity——
            // 用户可能想"先选 entity 看属性，再点 asset 拿到路径做 DnD"。
            if (DoesAssetTriggerInspectorSubMode(path))
            {
                host.selection.selectedEntity            = Orange::Engine::Entity::Invalid();
                host.selection.transformEulerCacheEntity = Orange::Engine::Entity::Invalid();
            }
        }
        // DnD source：path 字符串（含 '\0' 终止符）作为 payload 数据；
        // v0.5 c4 接收方在 Inspector AssetRef 字段内 AcceptDragDropPayload
        // 拿到 path 后调 prop.set(component, &pathString) 写入字段。
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("ORANGE_ASSET",
                                      path.data(),
                                      path.size() + 1);
            ImGui::Text("Drag %s", name.c_str());
            ImGui::EndDragDropSource();
        }

        // v0.6 c7：L17 右键 "Pick to Inspector field"。修复 v0.5 B3 副作用
        // —— 点 .material 触发 Material 子模式接管，Inspector 不画实体，Pick
        // 按钮永远不显示。提供右键路径精准写入选中 entity 的 Renderable 字段
        // 而不需要先解除 Material 子模式。Selectable 左键才把 selectedAssetPath
        // 改写，右键 BeginPopupContextItem 不触发 left-click 路径，所以选中
        // entity 不会被本路径清掉。
        if (ImGui::BeginPopupContextItem("##asset_file_ctx"))
        {
            using ::Orange::Engine::Entity;
            using ::Orange::Engine::Render::RenderableComponent;
            const Entity selEntity = host.selection.selectedEntity;
            const bool   selValid  = selEntity.IsValid()
                                  && host.scene.pWorld != nullptr
                                  && host.scene.pWorld->IsValid(selEntity);
            const auto* rc = selValid
                ? host.scene.pWorld->GetComponent<RenderableComponent>(selEntity)
                : nullptr;
            const bool canPickMesh     = (rc != nullptr)
                                      && (ext == ".mesh" || ext == ".obj");
            const bool canPickMaterial = (rc != nullptr)
                                      && (ext == ".material");

            if (!selValid) {
                ImGui::TextDisabled("(no entity selected)");
            } else if (rc == nullptr) {
                ImGui::TextDisabled("(selected entity has no Renderable)");
            }

            ImGui::BeginDisabled(!canPickMesh);
            if (ImGui::MenuItem("Pick to Renderable.mesh"))
            {
                using ::Orange::Engine::Asset::MeshAsset;
                std::string oldPath;
                if (rc != nullptr && rc->mesh.IsValid()
                    && host.assets.pAssets != nullptr)
                {
                    oldPath = std::string{host.assets.pAssets
                        ->PathOf<MeshAsset>(rc->mesh)};
                }
                auto apply = [pH = &host, capE = selEntity]
                              (const std::string& p) {
                    auto* pW = pH->scene.pWorld.get();
                    if (pW == nullptr || !pW->IsValid(capE)) { return; }
                    auto* pRC = pW->GetComponent<RenderableComponent>(capE);
                    if (pRC == nullptr) { return; }
                    if (p.empty()) { pRC->mesh = {}; return; }
                    auto* pReg = pH->assets.pAssets.get();
                    if (pReg == nullptr) { return; }
                    auto lr = pReg->Load<
                        ::Orange::Engine::Asset::MeshAsset>(p);
                    if (lr.IsOk()) { pRC->mesh = lr.Value(); }
                };
                host.cmdStack.Push(
                    std::make_unique<SetFieldValueCommand<std::string>>(
                        selEntity, "Renderable.mesh",
                        oldPath, path, std::move(apply)));
            }
            ImGui::EndDisabled();

            ImGui::BeginDisabled(!canPickMaterial);
            if (ImGui::MenuItem("Pick to Renderable.material"))
            {
                std::string oldPath;
                const auto named = BuildNamedMaterialInstances(host.assets);
                if (rc != nullptr && rc->materialInstance != nullptr) {
                    for (const auto& [p, ptr] : named) {
                        if (ptr == rc->materialInstance) { oldPath = p; break; }
                    }
                }
                auto apply = [pH = &host, capE = selEntity]
                              (const std::string& p) {
                    auto* pW = pH->scene.pWorld.get();
                    if (pW == nullptr || !pW->IsValid(capE)) { return; }
                    auto* pRC = pW->GetComponent<RenderableComponent>(capE);
                    if (pRC == nullptr) { return; }
                    if (p.empty()) { pRC->materialInstance = nullptr; return; }
                    const auto m = BuildNamedMaterialInstances(pH->assets);
                    auto it = m.find(p);
                    pRC->materialInstance = (it != m.end())
                        ? it->second : nullptr;
                };
                host.cmdStack.Push(
                    std::make_unique<SetFieldValueCommand<std::string>>(
                        selEntity, "Renderable.materialInstance",
                        oldPath, path, std::move(apply)));
            }
            ImGui::EndDisabled();

            ImGui::EndPopup();
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", path.c_str());
        }
    }
}

}  // anonymous namespace

// v0.5 c3：Asset 浏览器面板。左侧目录树（assets/ 递归扫描）+ 右侧当前
// 目录文件列表 + 类型 icon prefix + DnD source。选中状态走 EditorAssetContext。
//
// 当前简化范围：
//   * 仅浏览 source asset（D2 决策）；不引入缩略图生成 / 编译产物显示
//   * 类型 icon 用 ASCII 前缀（[M]/[Mat]/[T]/[S]/[J]/[?]），不接入 icon
//     font —— icon font 接入由 v0.6.5 视觉统一 milestone 实施
//   * 不支持创建 / 删除 / 重命名（v0.6 多 chunk + dirty 状态时再加）
void EditorRenderLayer::DrawAssetsPanel()
{
    ImGui::Begin("Assets");

    auto& assets = mHost.assets;

    // 顶部：当前路径 + 退到父目录按钮（root assets/ 时禁用）
    ImGui::TextDisabled("Path:");
    ImGui::SameLine();
    ImGui::TextUnformatted(assets.browserCurrentDir.c_str());
    ImGui::SameLine();
    const bool atRoot = (assets.browserCurrentDir == "assets");
    ImGui::BeginDisabled(atRoot);
    if (ImGui::SmallButton(".."))
    {
        const auto slash = assets.browserCurrentDir.find_last_of('/');
        if (slash != std::string::npos)
        {
            assets.browserCurrentDir.resize(slash);
        }
    }
    ImGui::EndDisabled();
    ImGui::Separator();

    // 左 30% 目录树 + 右 70% 文件列表，BeginChild 内独立滚动。
    constexpr float kLeftRatio = 0.30f;
    const float leftW = ImGui::GetContentRegionAvail().x * kLeftRatio;

    ImGui::BeginChild("##asset_tree", ImVec2(leftW, 0), true);
    // B1 修：平铺 assets/ 顶层子目录（去掉 root "assets" TreeNode）。
    // 旧版用 root 节点 + DefaultOpen 持久化展开状态到 imgui.ini —— 一旦
    // 用户在某次会话折叠了 root 或某个顶层目录，下次启动 imgui.ini 状态
    // 覆盖 DefaultOpen，scenes / configs 等节点就"看起来不存在"。新版直
    // 接平铺 + 每次启动首次强制展开顶层目录（Cond_Once），匹配 Cocos /
    // Unity Project 面板手感。
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists("assets", ec))
        {
            const auto cwd = fs::current_path(ec).generic_string();
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
                "assets/ 目录未找到");
            ImGui::TextDisabled("cwd: %s", cwd.c_str());
            ImGui::TextDisabled("（启动期 ChdirToRepoRoot 未能定位仓库根 —— "
                                ".exe 不在仓库 build/ 子树内？）");
        }
        else
        {
            std::vector<std::string> topDirs;
            for (auto& entry : fs::directory_iterator("assets", ec))
            {
                if (entry.is_directory(ec))
                {
                    topDirs.push_back(entry.path().generic_string());
                }
            }
            std::sort(topDirs.begin(), topDirs.end());

            for (auto& d : topDirs)
            {
                // Cond_Once：每次启动首次强制打开（盖过 imgui.ini 可能记录
                // 的折叠状态），期间用户折叠/展开操作保留 —— 避免 imgui.ini
                // 旧状态卡死目录树，又不破坏正常 UX。
                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                DrawAssetTreeRecursive(assets, d);
            }
            if (topDirs.empty())
            {
                ImGui::TextDisabled("(assets/ 内无子目录)");
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##asset_list", ImVec2(0, 0), true);
    DrawAssetFileList(mHost, assets);
    ImGui::EndChild();

    ImGui::End();
}

// v0.5 c2：底部 tab 容器加 Animation 占位面板（Assets / Console / Animation
// 三 tab 与 Cocos Creator 3.6.0 底部布局对齐）。本期仅占位，状态机图编辑
// 由 v0.7 实施。BuildDefaultLayoutOnce 内把 Animation window dock 到 bottom
// 节点（与 Assets / Console 同 slot 自动变 tab）。
void EditorRenderLayer::DrawAnimationPanel()
{
    ImGui::Begin("Animation");
    ImGui::TextDisabled("animation timeline / state-machine graph editor "
                        "— v0.7 实施");
    ImGui::TextDisabled("当前接受视觉降级：动画 backend 切换 / 状态机编辑 "
                        "仅 Inspector 字段路径可用");
    ImGui::End();
}

// Console 面板放调试信息：帧 index、deltaTime、Esc 退出按钮、Vulkan
// multi-viewport 提示。当前编辑器没有日志系统，先把这些
// 当作 "console" 的内容，等真接 Core::Log 时换成日志流。
void EditorRenderLayer::DrawConsolePanel(const Orange::Engine::FrameContext& frame)
{
    ImGui::Begin("Console");
    ImGui::Text("OrangeEditor v0.0.3");
    ImGui::Separator();
    ImGui::Text("frame index: %llu",
                static_cast<unsigned long long>(frame.time.frameIndex));
    ImGui::Text("delta: %.3f ms",
                frame.time.deltaSeconds * 1000.0);
    ImGui::Separator();
    ImGui::TextWrapped(
        "Drag any panel's tab OUT of the main window to detach it as a "
        "floating native OS window (ImGui multi-viewport).");
    ImGui::Separator();
    if (ImGui::Button("Quit (or press Esc)")) {
        mAppHost.RequestExit();
    }
    ImGui::End();
}
