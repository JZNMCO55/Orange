// EditorRenderLayer 主 TU —— 构造 / 析构 / OnUpdate / OnEvent / dock 布局 /
// main menu / pending scene op / Assets / Console。其余面板（Scene /
// EntityTree / Inspector）在 panels/ 下独立 TU，共享同一类声明
// （EditorRenderLayer.h）。

#include "EditorRenderLayer.h"

#include "DemoWorld.h"
#include "EditorHierarchy.h"
#include "VulkanLoaderShim.h"

#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/RigidBodyComponent.h>
#include <orange/engine/platform/Window.h>
#include <orange/engine/render/RenderableComponent.h>
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

    // 五个固定面板：Scene / Entity Tree / Inspector / Assets / Console。
    DrawScenePanel();
    DrawEntityTreePanel();
    DrawInspectorPanel();
    DrawAssetsPanel();
    DrawConsolePanel(frame);

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
    ImGui::DockBuilderDockWindow("Inspector",   right);
    ImGui::DockBuilderDockWindow("Assets",      bottom);
    ImGui::DockBuilderDockWindow("Console",     bottom);  // 同节点 = tab
    ImGui::DockBuilderDockWindow("Scene",       center);

    ImGui::DockBuilderFinish(dockspaceId);
}

// ---------------------------------------------------------------------------
// Main menu bar / scene op
// ---------------------------------------------------------------------------

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
            mHost.scene.pendingSceneOp = SceneOp::New;
        }
        if (ImGui::MenuItem("Open Scene...")) {
            mHost.scene.pendingSceneOp = SceneOp::Open;
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
        if (ImGui::MenuItem("Exit")) {
            mAppHost.RequestExit();
        }
        ImGui::EndMenu();
    }

    // ---- Play / Pause / Stop 按钮 -------------------------
    // 直接放主菜单栏右侧（不另开 toolbar，避免再加一行垂直空间占用）。
    // 状态显示用一个 Text + 三个按钮：disabled / enabled 按当前 playState
    // 推算（典型 transport-control 风格：Play 在 Edit / Paused 可用，
    // Pause 仅 Play 可用，Stop 仅 Play / Paused 可用）。
    {
        const PlayState ps = mHost.scene.playState;
        const char* stateLabel = (ps == PlayState::Edit)   ? "[Edit]"
                               : (ps == PlayState::Play)   ? "[Play]"
                                                           : "[Paused]";
        // 三个按钮 + 状态 label 总宽：粗算 36*3 + 60 = 168。
        constexpr float kButtonW   = 36.0f;
        constexpr float kStateW    = 70.0f;
        constexpr float kGroupW    = kButtonW * 3.0f + kStateW + 16.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - kGroupW);

        const bool canPlay   = (ps == PlayState::Edit  || ps == PlayState::Paused);
        const bool canPause  = (ps == PlayState::Play);
        const bool canStop   = (ps == PlayState::Play  || ps == PlayState::Paused);
        ImGui::BeginDisabled(!canPlay);
        if (ImGui::Button("Play", ImVec2(kButtonW, 0))) {
            mHost.scene.pendingPlayOp = (ps == PlayState::Paused) ? PlayOp::Resume
                                                             : PlayOp::EnterPlay;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!canPause);
        if (ImGui::Button("Pause", ImVec2(kButtonW, 0))) {
            mHost.scene.pendingPlayOp = PlayOp::Pause;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!canStop);
        if (ImGui::Button("Stop", ImVec2(kButtonW, 0))) {
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

void EditorRenderLayer::DrawAssetsPanel()
{
    ImGui::Begin("Assets");
    ImGui::TextDisabled("asset browser — Phase 6 后续");
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
