// EditorRenderLayer 主 TU —— 构造 / 析构 / OnUpdate / OnEvent / dock 布局 /
// main menu / pending scene op / Assets / Console。其余面板（Scene /
// EntityTree / Inspector）在 panels/ 下独立 TU，共享同一类声明
// （EditorRenderLayer.h）。

#include "EditorRenderLayer.h"

#include "BuiltinAssets.h"  // BuildNamedMaterialInstances（v1.0.1 c11 拆出）
#include "DemoWorld.h"      // SeedDemoWorld / SeedPbrShowcaseWorld
#include "EditorAssetDropHandler.h"  // CreateEntityFromMeshAsset / SyncSubMeshMaterialsForMesh
#include "EditorCameraControl.h"  // FrameSelectedCamera / FrameAllCamera（View 菜单）
#include "EditorAssetReferences.h"  // FindAssetReferences（资产引用只读扫描）
#include "EditorHierarchy.h"
#include "EditorPrefabActions.h"  // Create Prefab modal 承接 + 写盘 helper
#include "EditorTextUtil.h"  // Util::ContainsCaseInsensitive（Console + Asset 搜索共用）
#include "VulkanLoaderShim.h"
#include "command/LambdaCommand.h"  // 资产 rename 可 undo（文件+.meta+引用）
#include "command/SetFieldValueCommand.h"
#include "MaterialFileIO.h"  // v1.1.1 · Asset Browser Create Material modal
#include "import/ImportDispatcher.h"
#include "import/GltfSceneImporter.h"
#include "import/MetaSidecar.h"
#include "plugin/MaterialAssetInspectorPlugin.h"  // SaveEditingMaterialToDisk（关窗确认存材质）
#include "render/ThumbnailService.h"  // 材质球缩略图（FlushPending + GetOrRequestThumbnail）
#include "theme/EditorTheme.h"

#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/SoundAsset.h>

#include <orange/engine/animation/AnimationSystem.h>
#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/animation/ClipAnimator.h>
#include <orange/engine/audio/AudioEngine.h>
#include <orange/engine/audio/AudioSourceComponent.h>
#include <orange/engine/audio/SoundInstance.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/core/Log.h>
#include <orange/engine/core/Memory.h>
#include <orange/engine/core/Profiler.h>

#include <functional>
#include <string_view>
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
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>

namespace
{

// Console 日志条目入队时刻，格式化为本地 "HH:MM:SS"。sink 在任意线程触发，
// 用线程安全的 localtime_s（MSVC）；编辑器 Windows-first，无需跨平台分支。
std::string FormatWallClockNow()
{
    const auto      now = std::chrono::system_clock::now();
    const std::time_t t  = std::chrono::system_clock::to_time_t(now);
    std::tm         tm{};
    localtime_s(&tm, &t);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return std::string{buf};
}

// Esc 全局退出（与 Input::KeyCode::Escape 同值）；仅本 TU 用。
constexpr std::int32_t kEscapeKeyRaw = 256;

// 大小写不敏感子串匹配已抽到 EditorTextUtil.h（Console + Asset 搜索共用，单测
// editor_text_util_test）；用 Orange::Editor::Util::ContainsCaseInsensitive。

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

    // 摘掉 grid aux pass 注册：Pipeline 析构前先 SetAuxPassProvider(nullptr)
    // 让 Pipeline 内 pAuxPassProvider 字段失效，再释放 provider 自身（含
    // GPU 资源）。顺序：Pipeline → SetAuxPassProvider(nullptr) → provider
    // Shutdown → provider 析构（unique_ptr.reset）→ Pipeline.reset()。
    // 若反过来先 Pipeline.reset 也安全 —— Pipeline.Shutdown 内 WaitIdle 已
    // 保证 provider 的 GPU 工作已完成，且 Pipeline 析构后不再调 provider
    // hook —— 但显式摘注册让意图更清晰。
    if (mpScenePipeline)
    {
        mpScenePipeline->SetAuxPassProvider(nullptr);
    }
    if (mpEditorGridProvider)
    {
        mpEditorGridProvider->Shutdown();
        mpEditorGridProvider.reset();
    }

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

    // 自动存档推进（首帧顺带 lazy-init + 残留 autosave 崩溃恢复检测）。仅 Edit
    // 态推进，dirty gate 在 DoAutosave 内。GAP-2026-05-29-editor-autosave-wiring。
    UpdateAutosave(dt);
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
                ORANGE_LOG_DEBUG("[vfx-diag] live particles: {}",
                                 mpVfxSystem->TotalLiveParticleCount());
            }
        }

        // Animator tick —— 走引擎层 Animation::TickAnimators（单一真相源，
        // 游戏侧消费同一入口；DRY）。行为与此前内联循环一致：遍历所有
        // AnimatorComponent，对非空 animator 调 Tick(dt)。
        Orange::Engine::Animation::TickAnimators(*mHost.scene.pWorld, dt);

        // Audio: 同步 component 字段 → 已实例化的 SoundInstance（用户在
        // Play 期改 volume / pitch / loop slider 时声音实时跟随）。pitch /
        // loop 公共面尚未暴露，先仅 sync volume。
        if (mHost.audioEngine.IsInitialized()) {
            using namespace Orange::Engine::Audio;
            auto& reg = mHost.scene.pWorld->Registry();
            for (auto e : reg.view<AudioSourceComponent>()) {
                auto& as = reg.get<AudioSourceComponent>(e);
                Orange::Engine::Entity eWrap{static_cast<std::uint64_t>(
                    static_cast<std::uint32_t>(e))};
                auto it = mEntityToSoundInstance.find(eWrap);
                if (it != mEntityToSoundInstance.end() && it->second
                    && it->second->IsValid()) {
                    it->second->SetVolume(as.volume);
                }
            }
        }
    }

    // ---- Edit 态动画 clip 预览 tick（B2.6）------------------------------
    // 编辑器 Edit 模式**不**跑全量 TickAnimators；Inspector 的 Play 按钮把
    // host.animPreview 指向某 entity 的 ClipAnimator，这里**只对该一个**
    // animator 推进 Tick(dt)，让用户在不进 PlayState::Play 的前提下预览 clip。
    // 与上方 Play 模式全量 tick 互斥（playState 分支二选一），不会双写 elapsed。
    // 每帧从 previewEntity 重新解析 ClipAnimator（实体 / 组件可能被 Undo /
    // 切场景销毁）——解析失败即自动清预览，避免持野指针。
    if (mHost.scene.playState == PlayState::Edit
        && mHost.animPreview.previewPlaying
        && mHost.animPreview.previewEntity.IsValid()
        && mHost.scene.pWorld != nullptr)
    {
        using AC = Orange::Engine::Animation::AnimatorComponent;
        AC* pAc = mHost.scene.pWorld->GetComponent<AC>(mHost.animPreview.previewEntity);
        auto* pClip = (pAc != nullptr && pAc->animator)
            ? dynamic_cast<Orange::Engine::Animation::ClipAnimator*>(pAc->animator.get())
            : nullptr;
        if (pClip != nullptr)
        {
            pClip->Tick(dt);
        }
        else
        {
            // 预览目标没了（实体删除 / backend 被切走）→ 停预览，避免空转。
            mHost.animPreview.Clear();
        }
    }

    // 切换选中实体时清预览并把旧目标归位（Seek(0)）—— 预览跟随 Inspector
    // 当前选中实体；切走后旧 animator 不应继续在 viewport 动。仅 Edit 态有
    // 预览态需要维护。
    if (mHost.scene.playState == PlayState::Edit
        && mHost.animPreview.previewEntity.IsValid()
        && mHost.animPreview.previewEntity != mHost.selection.selectedEntity)
    {
        if (mHost.scene.pWorld != nullptr)
        {
            using AC = Orange::Engine::Animation::AnimatorComponent;
            AC* pAc = mHost.scene.pWorld->GetComponent<AC>(
                mHost.animPreview.previewEntity);
            auto* pClip = (pAc != nullptr && pAc->animator)
                ? dynamic_cast<Orange::Engine::Animation::ClipAnimator*>(
                      pAc->animator.get())
                : nullptr;
            if (pClip != nullptr) { pClip->Seek(0.0f); }  // 归位 t0
        }
        mHost.animPreview.Clear();
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

        // Ctrl+S / Ctrl+Shift+S / Ctrl+N / Ctrl+O 文件操作快捷键。菜单 label
        // 早已宣传这些 chord，此前却只是显示文本未接线（DrawMainMenuBar 注释
        // 自承"留给后续微调"）。在此补上：设与对应菜单项完全相同的 pending op，
        // 真正的 dialog + Save/Load 仍走帧末 ApplyPendingSceneOp（节奏一致）+
        // dirty 时的未保存确认 popup 一并复用。IsKeyChordPressed 精确匹配 mods
        // （Ctrl+S 与 Ctrl+Shift+S 互斥，同 Z/Y），F2/Del 无 Ctrl 不冲突。
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)) {
            mHost.scene.pendingSceneOp = SceneOp::SaveAs;
        }
        else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
            // dirty 才存（对齐菜单 Save 的 canQuickSave 灰禁）；空路径时 Save
            // 分支会自动转 SaveAs。
            if (mHost.scene.dirty) { mHost.scene.pendingSceneOp = SceneOp::Save; }
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N)) {
            if (mHost.scene.dirty || HasUnsavedMaterial()) {
                mHost.scene.pendingCloseAction = PendingCloseAction::NewScene;
            } else {
                mHost.scene.pendingSceneOp = SceneOp::New;
            }
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) {
            mPendingOpenScenePath.clear();  // 走文件对话框（清 recent 残留）
            if (mHost.scene.dirty || HasUnsavedMaterial()) {
                mHost.scene.pendingCloseAction = PendingCloseAction::OpenScene;
            } else {
                mHost.scene.pendingSceneOp = SceneOp::Open;
            }
        }
    }

    // Asset 浏览器双击 .scene.json 的打开请求（DrawAssetFileList 经 host.scene
    // 桥接，见 requestedOpenScenePath 注释）：路由到与 Open Recent 完全相同的流程
    // （注入 mPendingOpenScenePath 跳过对话框 + dirty 时走未保存确认）。仅 Edit 态。
    if (mHost.scene.playState == PlayState::Edit
        && !mHost.scene.requestedOpenScenePath.empty())
    {
        mPendingOpenScenePath = mHost.scene.requestedOpenScenePath;
        mHost.scene.requestedOpenScenePath.clear();
        if (mHost.scene.dirty || HasUnsavedMaterial()) {
            mHost.scene.pendingCloseAction = PendingCloseAction::OpenScene;
        } else {
            mHost.scene.pendingSceneOp = SceneOp::Open;
        }
    }

    // 顶部固定 UI 的渲染顺序（v0.6.5 c0 起）：
    //   1. DrawMainMenuBar    —— BeginMainMenuBar 占主 viewport 顶部一行
    //   2. DrawMainToolbar    —— BeginViewportSideBar(Up) 占第二行
    //   3. DockSpaceOverViewport —— 在 1 + 2 扣除后的 work area 内画 dock
    // ImGui 把 BeginMainMenuBar / BeginViewportSideBar 累积到 viewport
    // BuildWorkOffset，下帧 commit 到 WorkOffset；DockSpaceOverViewport
    // 读 viewport WorkPos/WorkSize 派生 dock 区域。因此把 menu/toolbar 调用
    // 放在 DockSpaceOverViewport 之前是稳态正确的（首帧 t=0 可能 dockspace
    // 短暂覆盖 toolbar 一帧，稳态后正常）。
    DrawMainMenuBar();
    DrawMainToolbar();

    // dock space —— 占满主 viewport 内 menu + toolbar 扣除后的剩余区域。
    // DockSpaceOverViewport 返回的 ID 在主 viewport 生命周期内稳定，下面
    // DockBuilder 系列 API 用它建默认布局。
    const ImGuiID dockspaceId =
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    // v1.0.1 c4：检测 viewport 尺寸大幅变化（典型场景：maximize ↔ restore）。
    // ImGui dock 子节点在窗口尺寸跳变后仍维持上一帧的绝对像素 SizeRef：
    //   * restore（收缩）：中央 Scene 被两侧 dock 挤到 0 宽
    //   * maximize（扩大）：两侧 dock 仍按 restore 时的小像素维持，大画布
    //     里看起来 panel 偏窄，底部 Assets 甚至会被 work area 裁掉
    // 因此对称检测：任一方向变化超 ±25% 都重建。阈值刻意取大，拖窗口
    // 边界的连续小幅 resize 不会触发，保留用户细调手感。
    //
    // c9 补丁（v1.0.1）：c4 初版只检测收缩，restore → maximize 路径
    // panel 不恢复 default 比例。改为对称判断 |ratio - 1| > 0.25。
    {
        const ImVec2 vpSize = ImGui::GetMainViewport()->Size;
        if (mLastViewportSize.x > 0.0f && mLastViewportSize.y > 0.0f)
        {
            const float wRatio = vpSize.x / mLastViewportSize.x;
            const float hRatio = vpSize.y / mLastViewportSize.y;
            const bool wJumped = (wRatio < 0.75f) || (wRatio > 1.33f);
            const bool hJumped = (hRatio < 0.75f) || (hRatio > 1.33f);
            if (wJumped || hJumped)
            {
                ImGui::DockBuilderRemoveNode(dockspaceId);
            }
        }
        mLastViewportSize = vpSize;
    }

    BuildDefaultLayoutOnce(dockspaceId);
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
    if (mShowSettingsPanel)
    {
        DrawSettingsPanel();
    }
    if (mShowProfilerPanel)
    {
        DrawProfilerPanel(frame);
    }
    if (mShowRenderSettingsPanel)
    {
        DrawRenderSettingsPanel();
    }

    // v0.6 c2：未保存确认 popup —— 必须在 ApplyPendingSceneOp 之前，让
    // popup 的 Save 按钮设置的 pendingSceneOp 在同帧 ApplyPendingSceneOp
    // 内被消费；popup 自身用 BeginPopupModal 阻塞 ImGui 帧逻辑（背后
    // panels 仍画但不响应输入），用户点 Save/Discard/Cancel 后才继续。
    DrawUnsavedConfirmPopup();
    // 启动期残留 autosave 恢复 modal（崩溃恢复）。与 unsaved-confirm 互斥出现
    // （恢复发生在启动、unsaved 发生在编辑后），同款 BeginPopupModal 阻塞模式。
    DrawAutosaveRecoveryPopup();

    // 帧末统一 apply 场景级操作。放在 panel 绘制完之后、ImGui::Render
    // 之前 —— 文件对话框是模态阻塞窗口，它内部会 pump 一些消息但不
    // 影响本帧的 ImGui DrawData；swap world 之后的 selectedEntity /
    // renamingEntity / euler 缓存清理也在此发生，下一帧才用新状态画。
    ApplyPendingSceneOp();
    ApplyPendingPlayOp();
    ApplyPendingImports();

    // 材质球缩略图烘焙 —— 这是唯一安全的帧外点：DrawScenePanel 内 viewport
    // Render 已 WaitIdle（GPU 排空）、ImGui 尚未 Render（draw data 未提交）、
    // 引擎 BeginFrame 在下方才开始。FlushPending 在此每帧最多烘 3 张（含
    // RenderToTexture + ImGui_ImplVulkan_AddTexture/RemoveTexture，全部要求帧外）。
    // mpScenePipeline 为空（viewport 未就绪 / 初始化失败）时 thumbnails 内部
    // SetPipeline(nullptr) 已让 FlushPending 早退，这里再加一道 null 守卫。
    if (mpScenePipeline && mHost.thumbnails)
    {
        mHost.thumbnails->FlushPending(frame.time.frameIndex, 3);
    }

    ImGui::Render();

    // ---- engine frame：BeginFrame → (overlay callback fires
    //      ImGui_ImplVulkan_RenderDrawData) → EndFrame ----------------
    Orange::Renderer::FrameTimeInfo time{};
    time.mTotalTimeSeconds = frame.time.totalSeconds;
    time.mDeltaTimeSeconds = static_cast<float>(frame.time.deltaSeconds);
    if (Orange::Failed(mRenderer.BeginFrame(time))) {
        ORANGE_LOG_ERROR("[OrangeEditor] BeginFrame failed");
        mAppHost.RequestExit();
        return;
    }
    // 编辑器不渲染任何 SubmitItem 内容 —— 仅靠 overlay callback 内的
    // ImGui draw data。FrameLifecycle 在 hasDraw=false + overlay 已
    // 注册时会强制走 begin/end rendering 路径（FEATURE-2026-05-09 修
    // 复的 overlay-on-empty-frame bug），callback 仍能正常触发。
    if (Orange::Failed(mRenderer.EndFrame())) {
        ORANGE_LOG_ERROR("[OrangeEditor] EndFrame failed");
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
    ORANGE_LOG_INFO("[OrangeEditor] Esc 按下，请求退出");
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
    // v1.0.1 c8：用 WorkSize 替代 Size —— Size 包含 menu bar + toolbar 占据的
    // 高度，而 dockspace 实际只占 work area；用 Size 算 SplitNode 比例会让
    // 子节点 SizeRef 持有"超过 dock 实际容器"的绝对像素，导致 maximize →
    // restore 重建时 Entity Tree / Inspector 无法回到 default 20%/25% 比例
    // （Imgui dock 在 work area 内 fit 时把超出部分挤压）。WorkSize 等于
    // viewport.Size 减去 menu + toolbar 的高度，是真实 dock 容器尺寸。
    ImGui::DockBuilderSetNodeSize(dockspaceId,
                                  ImGui::GetMainViewport()->WorkSize);

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

bool EditorRenderLayer::HasUnsavedMaterial() const
{
    // 正在编辑某 .material 且有未写盘改动（GAP-2026-05-29 facet 1）。
    return !mHost.assets.editingMaterialPath.empty()
        && mHost.assets.editingMaterialDirty;
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
    const bool sceneDirty = mHost.scene.dirty;
    const bool matDirty   = HasUnsavedMaterial();  // facet 1：材质改动也纳入拦截
    if (!sceneDirty && !matDirty)
    {
        DispatchPendingCloseAction();  // 无未保存改动（含 Save 完成后）→ 静默继续
        return;
    }

    static constexpr const char* kPopupId = "##unsaved_confirm";
    ImGui::OpenPopup(kPopupId);
    if (ImGui::BeginPopupModal(kPopupId, nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        // 文案按"场景 / 材质 / 两者"自适应。
        if (sceneDirty && matDirty)
        {
            ImGui::TextUnformatted("场景和材质都有未保存的改动。");
        }
        else if (matDirty)
        {
            ImGui::TextUnformatted("当前材质有未保存的改动。");
        }
        else
        {
            ImGui::TextUnformatted("当前 scene 有未保存的改动。");
        }
        ImGui::TextDisabled("Save = 保存后继续  /  Discard = 丢弃改动继续  /  Cancel = 取消");
        ImGui::Separator();
        if (ImGui::Button("Save"))
        {
            // 材质同步存盘（立即清 editingMaterialDirty）；场景走 deferred
            // SceneOp::Save（下帧 ApplyPendingSceneOp 执行）。两者都 clean 后，
            // 下帧本函数早退分支 DispatchPendingCloseAction 继续原动作。
            if (matDirty)   { Orange::Editor::Plugin::SaveEditingMaterialToDisk(mHost); }
            if (sceneDirty) { mHost.scene.pendingSceneOp = SceneOp::Save; }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard"))
        {
            // 用户主动丢弃未保存改动 → autosave 持有的正是这些改动，一并删，
            // 否则下次启动会提示恢复已被丢弃的工作（自相矛盾）。Exit 路径靠
            // 此清；New/Open 路径帧末 ApplyPendingSceneOp 的 clean 基线清也会兜。
            // 材质 dirty 一并清（disk 不写；内存 override 保留到 reload）。
            mHost.assets.editingMaterialDirty = false;
            ClearAutosaveFile();
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
            if (mHost.scene.dirty || HasUnsavedMaterial()) {
                mHost.scene.pendingCloseAction = PendingCloseAction::NewScene;
            } else {
                mHost.scene.pendingSceneOp = SceneOp::New;
            }
        }
        if (ImGui::MenuItem("Open Scene...")) {
            mPendingOpenScenePath.clear();  // 常规 Open 走文件对话框（清 recent 残留）
            if (mHost.scene.dirty || HasUnsavedMaterial()) {
                mHost.scene.pendingCloseAction = PendingCloseAction::OpenScene;
            } else {
                mHost.scene.pendingSceneOp = SceneOp::Open;
            }
        }
        // Open Recent 子菜单：最近打开 / 另存的场景（front = 最近）。点击 → 设
        // mPendingOpenScenePath（跳过对话框）+ 走 Open 流程（含未保存确认）。
        // 空列表时整个子菜单 disabled。
        {
            const auto& recent = mHost.settings.recentScenes;
            if (ImGui::BeginMenu("Open Recent", !recent.empty())) {
                int recentIdx = 0;
                for (const std::string& sp : recent) {
                    const auto slash = sp.find_last_of("/\\");
                    const std::string shortName =
                        (slash == std::string::npos) ? sp : sp.substr(slash + 1);
                    // 显示用 basename，但不同目录下的同名场景 basename 相同会导致
                    // ImGui label 撞 ID（"2 visible items with conflicting ID"）。
                    // 追加 "##<index>" 隐藏后缀：显示名仍是 basename，ID 走全串
                    // 哈希（## 后内容不显示但参与 ID），逐项唯一。
                    const std::string label =
                        shortName + "##recent" + std::to_string(recentIdx++);
                    if (ImGui::MenuItem(label.c_str())) {
                        mPendingOpenScenePath = sp;
                        if (mHost.scene.dirty || HasUnsavedMaterial()) {
                            mHost.scene.pendingCloseAction =
                                PendingCloseAction::OpenScene;
                        } else {
                            mHost.scene.pendingSceneOp = SceneOp::Open;
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", sp.c_str());
                    }
                }
                ImGui::EndMenu();
            }
        }
        ImGui::Separator();
        // v1.1 T2：Import 外部 DCC 资产入口（与 Asset Browser OS drag-drop
        // 双路并存，参 ADR-008 议题 A3）。点击只标 flag，dialog 在 OnUpdate
        // 的 ApplyPendingImports 帧首弹出（与 ApplyPendingSceneOp 同款节奏，
        // 避免 dialog 模态阻塞与 ImGui frame 冲突）。
        if (ImGui::MenuItem("Import...")) {
            mPendingImportDialog = true;
        }
        // scene-level 导入（GAP-2026-05-28 G1/G3）：吃 .gltf/.glb，保留 node 层级 +
        // 每 mesh 单独不塌平 + KHR_lights_punctual 灯光，产出 assets/scenes/<name>.scene.json。
        // 区别于上面 "Import..."（asset import，整文件塌平成单 mesh）。
        if (ImGui::MenuItem("Import glTF Scene...")) {
            mPendingImportSceneDialog = true;
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
            if (mHost.scene.dirty || HasUnsavedMaterial()) {
                mHost.scene.pendingCloseAction = PendingCloseAction::OpenScene;
            } else {
                mHost.scene.pendingSceneOp = SceneOp::OpenSplit;
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) {
            // v0.6 c2：dirty 时拦截走未保存确认 popup。
            if (mHost.scene.dirty || HasUnsavedMaterial()) {
                mHost.scene.pendingCloseAction = PendingCloseAction::Exit;
            } else {
                mAppHost.RequestExit();
            }
        }
        ImGui::EndMenu();
    }

    // Edit 菜单：Undo / Redo。Ctrl+Z / Ctrl+Y 全局快捷键路径在 OnUpdate
    // 顶部独立实现（仅 Edit 态 + 无文本焦点时响应），本菜单项是同款
    // 入口的鼠标可达版本，enabled 条件保持一致。
    if (ImGui::BeginMenu("Edit"))
    {
        const bool canEditCmd = (mHost.scene.playState == PlayState::Edit)
                             && !ImGui::GetIO().WantTextInput;
        // 菜单项文案带上具体动作名（"Undo Rename Entity" / "Redo Translate
        // Drag" 等）——label 取自栈顶命令的 GetLabel；空栈时 Peek 返回 nullptr，
        // 退回纯 "Undo" / "Redo"。拼成临时 std::string，c_str() 仅在本次
        // MenuItem 调用内有效（Peek 返回的指针不跨帧缓存，见 CommandStack.h）。
        const char* undoLabel = mHost.cmdStack.PeekUndoLabel();
        const char* redoLabel = mHost.cmdStack.PeekRedoLabel();
        const std::string undoText =
            undoLabel != nullptr ? "Undo " + std::string(undoLabel) : "Undo";
        const std::string redoText =
            redoLabel != nullptr ? "Redo " + std::string(redoLabel) : "Redo";
        if (ImGui::MenuItem(undoText.c_str(), "Ctrl+Z", false,
                            canEditCmd && mHost.cmdStack.CanUndo()))
        {
            mHost.cmdStack.Undo();
            ValidateEntityHandles();
        }
        if (ImGui::MenuItem(redoText.c_str(), "Ctrl+Y", false,
                            canEditCmd && mHost.cmdStack.CanRedo()))
        {
            mHost.cmdStack.Redo();
            ValidateEntityHandles();
        }
        ImGui::Separator();
        // Duplicate / Delete 选中实体 —— Ctrl+D / Del 的菜单可发现入口。与
        // Hierarchy / viewport 快捷键走同一组 host 幂等标志（帧末统一处理）。
        const bool hasSel = (mHost.scene.playState == PlayState::Edit)
                         && mHost.selection.selectedEntity.IsValid();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSel)) {
            mHost.selection.pendingDuplicate = true;
        }
        if (ImGui::MenuItem("Delete", "Del", false, hasSel)) {
            mHost.selection.pendingDelete = mHost.selection.selectedEntity;
        }
        ImGui::EndMenu();
    }

    // View 菜单：Settings / 其它 UI toggles（v0.8 落地）。
    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Settings", nullptr, &mShowSettingsPanel);
        ImGui::MenuItem("Profiler", nullptr, &mShowProfilerPanel);
        ImGui::MenuItem("Render Settings", nullptr, &mShowRenderSettingsPanel);

        // 相机聚焦（菜单提供 F / Home 快捷键的可发现入口；快捷键本身在 viewport
        // 内已绑）。Frame Selected 需有选中实体才可点。
        ImGui::Separator();
        if (ImGui::MenuItem("Frame Selected", "F", false,
                            mHost.selection.selectedEntity.IsValid()))
        {
            FrameSelectedCamera(mHost);
        }
        if (ImGui::MenuItem("Frame All", "Home"))
        {
            FrameAllCamera(mHost);
        }

        // 标准视角（Front/Back/Left/Right/Top/Bottom）—— 把轨道相机角度吸附到
        // 沿世界轴看 pivot 的方向（保持 pivot + radius）。对 2.5D 对齐 / DCC
        // 摆位很有用。offset=(cosE·sinAz, sinEl, cosE·cosAz) 且看向 pivot，故
        // Front=az0/el0（相机在 +Z 看 -Z）。Top/Bottom 钳到 ±89° 避免 el=±90°
        // 的 gimbal 退化（与 EditorCameraControl 的 kMaxElev 一致）。
        if (ImGui::BeginMenu("Standard Views"))
        {
            auto& cam = mHost.camera;
            constexpr float kPi   = 3.14159265358979323846f;
            constexpr float kTopE = 1.5533430343f;  // radians(89°)
            auto setView = [&cam](float az, float el) {
                cam.azimuth   = az;
                cam.elevation = el;
            };
            if (ImGui::MenuItem("Front"))  { setView(0.0f,         0.0f);  }
            if (ImGui::MenuItem("Back"))   { setView(kPi,          0.0f);  }
            if (ImGui::MenuItem("Right"))  { setView(kPi * 0.5f,   0.0f);  }
            if (ImGui::MenuItem("Left"))   { setView(-kPi * 0.5f,  0.0f);  }
            if (ImGui::MenuItem("Top"))    { setView(0.0f,         kTopE); }
            if (ImGui::MenuItem("Bottom")) { setView(0.0f,        -kTopE); }
            ImGui::EndMenu();
        }

        // 相机书签（gap 报告 §4 #4）：快照 / 跳转 viewport 轨道相机视角。
        // in-memory 单 session（持久化留待后续）。
        ImGui::Separator();
        if (ImGui::BeginMenu("Camera Bookmarks"))
        {
            auto& cam       = mHost.camera;             // live 轨道相机
            auto& bookmarks = mHost.settings.cameraBookmarks;  // 持久化（随 settings 存盘）
            if (ImGui::BeginMenu("Save current to"))
            {
                for (int i = 0; i < EditorSettings::kCameraBookmarkSlots; ++i)
                {
                    const std::string label =
                        "Slot " + std::to_string(i + 1)
                        + (bookmarks[i].valid ? " (overwrite)" : "");
                    if (ImGui::MenuItem(label.c_str()))
                    {
                        auto& bm       = bookmarks[i];
                        bm.pivot       = cam.pivot;
                        bm.azimuth     = cam.azimuth;
                        bm.elevation   = cam.elevation;
                        bm.radius      = cam.radius;
                        bm.fovYDegrees = cam.fovYDegrees;
                        bm.zNear       = cam.zNear;
                        bm.zFar        = cam.zFar;
                        bm.valid       = true;
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Go to"))
            {
                for (int i = 0; i < EditorSettings::kCameraBookmarkSlots; ++i)
                {
                    const auto& bm = bookmarks[i];
                    const std::string label = "Slot " + std::to_string(i + 1);
                    if (ImGui::MenuItem(label.c_str(), nullptr, false, bm.valid))
                    {
                        // 只还原视图参数，不动 dragging / 灵敏度（live 输入态）。
                        cam.pivot       = bm.pivot;
                        cam.azimuth     = bm.azimuth;
                        cam.elevation   = bm.elevation;
                        cam.radius      = bm.radius;
                        cam.fovYDegrees = bm.fovYDegrees;
                        cam.zNear       = bm.zNear;
                        cam.zFar        = bm.zFar;
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    // Help 菜单：About。MenuItem 只设 flag，真正的 OpenPopup 走外层
    // EndMainMenuBar 之后调用 —— 否则 OpenPopup 在 BeginMenu("Help") 内
    // 触发时 ID 经过 "Help" hash，与外层 BeginPopupModal 的 ID（无 "Help"
    // 上下文）不匹配，popup 永远不开。
    static bool sPendingOpenAbout = false;
    if (ImGui::BeginMenu("Help"))
    {
        if (ImGui::MenuItem("About OrangeEditor"))
        {
            sPendingOpenAbout = true;
        }
        ImGui::EndMenu();
    }

    // 当前 scene 路径作为只读 indicator 显示在 File 菜单右侧。
    // v0.6.5 c0：Save / Play / Pause / Stop / [State] 已迁出本菜单栏到
    // 独立 toolbar 行（DrawMainToolbar，紧贴本 menu bar 下方）。menu bar
    // 退回纯 File/Edit/View/Help + scene path indicator。
    const std::string& path = mHost.scene.currentScenePath;
    const char* sceneLabel  = path.empty() ? "[Untitled]" : path.c_str();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", sceneLabel);
    ImGui::EndMainMenuBar();

    // About popup —— modal，居中。OpenPopup 必须在 EndMainMenuBar 之后
    // 调，让 ID stack 与下面 BeginPopupModal 一致（参 sPendingOpenAbout
    // 注释段）。
    if (sPendingOpenAbout)
    {
        ImGui::OpenPopup("AboutOrangeEditor");
        sPendingOpenAbout = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("AboutOrangeEditor", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize
                               | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TextUnformatted("OrangeEditor");
        ImGui::TextDisabled("v0.0.3 (development)");
        ImGui::Separator();
        ImGui::TextWrapped("Editor for OrangeEngine —— "
                           "2D / 2.5D game framework.");
        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// 检查 EditorHost 里各实体句柄是否仍在 registry 中存活；对已被 Undo
// 销毁的实体清零，防止后续帧在死实体上调 DestroySubtree / GetComponent。
// 与 ResetEntityLocalState 的区别：Reset 是全清（切场景用），Validate
// 是精准检查（每次 Undo/Redo 后用）。
void EditorRenderLayer::ValidateEntityHandles()
{
    if (mHost.scene.pWorld == nullptr) { return; }
    auto& w = *mHost.scene.pWorld;

    // 本函数只在 Undo/Redo 后调用（4 处调用点全是 cmdStack.Undo/Redo 之后）——
    // 是刷新 Transform.rotation Euler 显示缓存的正确时机：无条件失效，让下一帧
    // Quat case 从恢复后的 quat 重算 Euler。rotation 的 Euler 缓存在 live 拖动
    // 期间被故意保留（避开 glm::eulerAngles pitch ±90° 折返导致"转不过 90°"，
    // 见 SchemaInspector.cpp Quat case），故失效动作不能放在字段 apply lambda 里
    // （那条 lambda 会被 CommandStack::Push 每 tick 重 Execute）——而是收敛到这里。
    mHost.selection.transformEulerCacheEntity = Orange::Engine::Entity::Invalid();

    if (mHost.selection.selectedEntity.IsValid() && !w.IsValid(mHost.selection.selectedEntity)) {
        mHost.selection.selectedEntity            = Orange::Engine::Entity::Invalid();
        mHost.selection.transformEulerCacheEntity = Orange::Engine::Entity::Invalid();
    }
    // v0.8 多选：清除 additional 集合中失效的实体
    {
        auto& addl = mHost.selection.additionalSelectedEntities;
        addl.erase(std::remove_if(addl.begin(), addl.end(),
                                   [&w](Orange::Engine::Entity ent) {
                                       return !w.IsValid(ent);
                                   }),
                   addl.end());
    }
    if (mHost.selection.renamingEntity.IsValid() && !w.IsValid(mHost.selection.renamingEntity)) {
        CancelRename();
    }
    if (mHost.selection.transformEulerCacheEntity.IsValid()
        && !w.IsValid(mHost.selection.transformEulerCacheEntity)) {
        mHost.selection.transformEulerCacheEntity = Orange::Engine::Entity::Invalid();
    }
    // B2.6：被预览的 clip animator 若被 Undo / Redo 销毁，清预览态（避免持
    // 失效 entity 句柄继续 tick）。
    if (mHost.animPreview.previewEntity.IsValid()
        && !w.IsValid(mHost.animPreview.previewEntity)) {
        mHost.animPreview.Clear();
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
    // B2.6：切 world 后预览目标 entity 身份失效，清预览态（不归位——旧 world
    // 已销毁，无 animator 可 Seek）。
    mHost.animPreview.Clear();
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
            // v1.0：菜单 label "New Scene" 必须真的给用户一个空场景，不再
            // 顺手种 demo entity（程序员便利与零基础用户预期严重不符，v1.0
            // 验收脚本段 A 第 2 步首例 Critical fail）。需要 demo 资产时走
            // `Open Scene → demo.scene.json`。
            mHost.scene.pWorld = std::make_unique<Orange::Engine::World>();
            // partition 与 pWorld 同生命周期；空 World 时仅保留默认构造自
            // 动注册的 "default" layer。
            mHost.scene.partition = Orange::Engine::Scene::WorldPartition{};
            mHost.scene.currentScenePath.clear();
            mHost.scene.dirty = false;
            ResetEntityLocalState();
            mHost.cmdStack.Clear();
            ORANGE_LOG_INFO("[OrangeEditor] new empty scene");
            break;
        }
        case SceneOp::Open: {
            std::string path;
            // Open Recent：mPendingOpenScenePath 非空 → 直接用它（跳过文件对话框）。
            if (!mPendingOpenScenePath.empty()) {
                path = mPendingOpenScenePath;
                mPendingOpenScenePath.clear();
            } else if (!ShowSceneFileDialog(/*isSave=*/false, hwnd, path)) {
                break;
            }
            auto pNew = std::make_unique<Orange::Engine::World>();
            
            Orange::Engine::Scene::LoadOptions openLoadOpts;
            openLoadOpts.assetRegistry          = mHost.assets.pAssets.get();
            openLoadOpts.animatorRegistry       = mHost.assets.pAnimators.get();
            openLoadOpts.namedMaterialInstances = &mHost.assets.namedMaterialInstances;
            // 查表失败时按 .material 路径从磁盘 lazy-create 兜底，修复导入的
            // material 在新 session 重开场景时 Inspector 显示 None。
            openLoadOpts.materialResolver       =
                [this](const std::string& id) { return ::EnsureMaterialInstance(mHost, id); };
            openLoadOpts.extraSerializers       = mHost.extraSerializers;
            auto rc = Orange::Engine::Scene::Load(path, *pNew, openLoadOpts);
            if (rc.IsErr()) {
                ORANGE_LOG_ERROR("[OrangeEditor] Scene::Load failed: {} (code={})",
                                 path,
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
            mHost.settings.AddRecentScene(path);  // File → Open Recent
            mHost.scene.dirty = false;
            ResetEntityLocalState();
            mHost.cmdStack.Clear();
            ORANGE_LOG_INFO("[OrangeEditor] opened scene: {}", path);
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
                
                Orange::Engine::Scene::SaveOptions saveOpts;
                saveOpts.assetRegistry          = mHost.assets.pAssets.get();
                saveOpts.namedMaterialInstances = &mHost.assets.namedMaterialInstances;
                saveOpts.extraSerializers       = mHost.extraSerializers;
                auto rc = Orange::Engine::Scene::Save(
                    *mHost.scene.pWorld, mHost.scene.currentScenePath, saveOpts);
                if (rc.IsErr()) {
                    ORANGE_LOG_ERROR("[OrangeEditor] Scene::Save failed: {} (code={})",
                                     mHost.scene.currentScenePath,
                                     static_cast<unsigned>(rc.Error()));
                } else {
                    mHost.scene.dirty = false;
                    ORANGE_LOG_INFO("[OrangeEditor] saved scene: {}",
                                    mHost.scene.currentScenePath);
                }
            }
            break;
        }
        case SceneOp::SaveAs: {
            std::string path;
            if (!ShowSceneFileDialog(/*isSave=*/true, hwnd, path)) { break; }
            
            Orange::Engine::Scene::SaveOptions saveAsOpts;
            saveAsOpts.assetRegistry          = mHost.assets.pAssets.get();
            saveAsOpts.namedMaterialInstances = &mHost.assets.namedMaterialInstances;
            saveAsOpts.extraSerializers       = mHost.extraSerializers;
            auto rc = Orange::Engine::Scene::Save(*mHost.scene.pWorld, path, saveAsOpts);
            if (rc.IsErr()) {
                ORANGE_LOG_ERROR("[OrangeEditor] Scene::Save failed: {} (code={})",
                                 path,
                                 static_cast<unsigned>(rc.Error()));
                break;
            }
            mHost.scene.currentScenePath = std::move(path);
            mHost.settings.AddRecentScene(mHost.scene.currentScenePath);  // Open Recent
            mHost.scene.dirty = false;
            ORANGE_LOG_INFO("[OrangeEditor] saved scene as: {}",
                            mHost.scene.currentScenePath);
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
            
            Orange::Engine::Scene::SaveOptions saveOpts;
            saveOpts.assetRegistry          = mHost.assets.pAssets.get();
            saveOpts.namedMaterialInstances = &mHost.assets.namedMaterialInstances;
            saveOpts.extraSerializers       = mHost.extraSerializers;
            const auto rc = Orange::Engine::Scene::SaveSplit(
                *mHost.scene.pWorld, mHost.scene.partition, manifestPath, saveOpts);
            if (rc.IsErr()) {
                ORANGE_LOG_ERROR("[OrangeEditor] Scene::SaveSplit failed: {} (code={})",
                                 manifestPath,
                                 static_cast<unsigned>(rc.Error()));
                break;
            }
            // currentScenePath 此刻指向 manifest 文件；后续 File>Save 仍
            // 走单文件 Save 路径（覆盖 manifest 文件本身），与多文件 split
            // 路径**不**互通——用户后续要继续 split 落盘必须再走 Save
            // Split As。这条限制写进 v0.6 acceptance checklist 的"已知简化"。
            mHost.scene.currentScenePath = std::move(manifestPath);
            mHost.scene.dirty = false;
            ORANGE_LOG_INFO("[OrangeEditor] saved scene (split) as: {} ({} layers)",
                            mHost.scene.currentScenePath,
                            mHost.scene.partition.LayerCount());
            break;
        }
        case SceneOp::OpenSplit: {
            std::string manifestPath;
            if (!ShowManifestFileDialog(/*isSave=*/false, hwnd, manifestPath)) { break; }
            auto pNew = std::make_unique<Orange::Engine::World>();
            Orange::Engine::Scene::WorldPartition newPartition;
            
            Orange::Engine::Scene::LoadOptions splitLoadOpts;
            splitLoadOpts.assetRegistry          = mHost.assets.pAssets.get();
            splitLoadOpts.animatorRegistry       = mHost.assets.pAnimators.get();
            splitLoadOpts.namedMaterialInstances = &mHost.assets.namedMaterialInstances;
            // 同单文件 Open：查表失败按磁盘 lazy-create 兜底（透传到 per-layer Load）。
            splitLoadOpts.materialResolver       =
                [this](const std::string& id) { return ::EnsureMaterialInstance(mHost, id); };
            splitLoadOpts.extraSerializers       = mHost.extraSerializers;
            // LoadSplit 内部按 manifest.layers 顺序遍历每条 source，
            // 并通过 LoadOptions.assignLayerId 给本次新建且没挂
            // LayerComponent 的 entity 自动按归属 layer 兜底——不必再
            // 在 editor 侧扫一遍 entity 重建 partition（与单文件 Open 路径
            // 不同：LoadSplit 已经把 partition 灌入完整 manifest）。
            const auto rc = Orange::Engine::Scene::LoadSplit(
                manifestPath, *pNew, newPartition, splitLoadOpts);
            if (rc.IsErr()) {
                ORANGE_LOG_ERROR("[OrangeEditor] Scene::LoadSplit failed: {} (code={})",
                                 manifestPath,
                                 static_cast<unsigned>(rc.Error()));
                break;
            }
            mHost.scene.pWorld           = std::move(pNew);
            mHost.scene.partition        = std::move(newPartition);
            mHost.scene.currentScenePath = manifestPath;
            mHost.scene.dirty            = false;
            ResetEntityLocalState();
            mHost.cmdStack.Clear();
            ORANGE_LOG_INFO("[OrangeEditor] opened scene (split): {} ({} layers)",
                            manifestPath,
                            mHost.scene.partition.LayerCount());
            break;
        }
        case SceneOp::None:
            break;  // unreachable, 上面已 early return
    }

    // 任一 scene op 使场景回到 clean 基线（New / Open / Save 系列成功后
    // dirty=false）→ 删残留 autosave：它已过时，且避免下次启动误报"未正常退出"。
    // 失败 / Cancel 路径 dirty 不变（仍 dirty 则保留 autosave 不动）。
    if (!mHost.scene.dirty) { ClearAutosaveFile(); }
}

// ---------------------------------------------------------------------------
// Autosave —— GAP-2026-05-29-editor-autosave-wiring
//
// 引擎侧 Save::AutosaveScheduler 是纯时间逻辑（无 IO / 不读系统时钟），编辑器
// 这层负责：(1) 每帧喂 dt（仅 Edit 态）；(2) scheduler 触发时把 dirty 的 World
// 序列化到 temp 的 .autosave（复用 Scene::Save，与 Play 快照同款）；(3) 手动
// Save / New / Open 后删 autosave；(4) 启动时检测残留 autosave（= 上次未正常
// 退出）→ 弹恢复 modal。
//
// 恢复信号 = "autosave 文件存在"：正常路径 Save/New/Open 都删它，所以启动时还
// 在 = 上次崩溃 / 强杀没走到删除。不依赖 mtime 比较，简单稳健。
// ---------------------------------------------------------------------------
namespace
{
std::string AutosaveScenePathStr()
{
    namespace fs = std::filesystem;
    return (fs::temp_directory_path() / "OrangeEditor_autosave.scene.json").string();
}
std::string AutosaveOriginPathStr()
{
    namespace fs = std::filesystem;
    return (fs::temp_directory_path() / "OrangeEditor_autosave.origin.txt").string();
}
}  // namespace

void EditorRenderLayer::ClearAutosaveFile()
{
    std::error_code ec;
    std::filesystem::remove(AutosaveScenePathStr(), ec);
    std::filesystem::remove(AutosaveOriginPathStr(), ec);
}

void EditorRenderLayer::DoAutosave()
{
    // scheduler 触发的 callback。dirty gate 在此：clean 场景不写（省 IO + 不
    // 覆盖待恢复点）。autosave 不清 scene.dirty —— 它不等价于"用户已保存"。
    if (!mHost.scene.dirty || mHost.scene.pWorld == nullptr) { return; }

    Orange::Engine::Scene::SaveOptions saveOpts;
    saveOpts.assetRegistry          = mHost.assets.pAssets.get();
    saveOpts.namedMaterialInstances = &mHost.assets.namedMaterialInstances;
    saveOpts.extraSerializers       = mHost.extraSerializers;
    const auto rc = Orange::Engine::Scene::Save(
        *mHost.scene.pWorld, AutosaveScenePathStr(), saveOpts);
    if (rc.IsErr())
    {
        ORANGE_LOG_WARN("[autosave] 写盘失败 (code={})",
                        static_cast<unsigned>(rc.Error()));
        return;
    }
    // origin sidecar：记当前 scene 路径（可空=未命名），恢复时回填 currentScenePath。
    {
        std::ofstream originOut(AutosaveOriginPathStr(), std::ios::trunc);
        if (originOut) { originOut << mHost.scene.currentScenePath; }
    }
    ORANGE_LOG_INFO("[autosave] 自动存档 → {}", AutosaveScenePathStr());
}

void EditorRenderLayer::UpdateAutosave(float dt)
{
    // 首帧一次性：残留 autosave 检测（崩溃恢复，独立于 autosaveEnabled——即便
    // 现在关了 autosave，上次崩溃留下的存档仍应给用户恢复机会）。
    if (!mAutosaveInitChecked)
    {
        mAutosaveInitChecked = true;
        std::error_code ec;
        if (std::filesystem::exists(AutosaveScenePathStr(), ec))
        {
            mPendingAutosaveRecovery = true;
            std::ifstream originIn(AutosaveOriginPathStr());
            if (originIn) { std::getline(originIn, mAutosaveRecoverOrigin); }
        }
    }

    // 每帧把 scheduler 与 settings 对齐——让 Settings 面板的 Enable **和**
    // Interval 改动都 live 生效。（dogfood 实测：之前只 Enable live、Interval
    // 改了不重建 → scheduler 仍用启动时的 180s，"改 Interval 后等不到 autosave"
    // 的根因。）(std::max) 加括号抑制 windows.h 的 max 宏（本 TU 经 glfw3native.h
    // 引入 windows.h，裸 std::max( 会被宏展开破坏）。
    if (!mHost.settings.autosaveEnabled)
    {
        mpAutosave.reset();  // 关闭：销毁 scheduler（已 null 则 no-op）
    }
    else
    {
        const double wantInterval = (std::max)(
            10.0, static_cast<double>(mHost.settings.autosaveIntervalSeconds));
        const double wantMin = (std::max)(
            0.0, static_cast<double>(mHost.settings.autosaveMinIntervalSeconds));
        // 不存在 / interval / throttle 变了 → (重)建 scheduler。重建重置计时
        // （从 0 起算新周期），符合"改完 Interval 重新倒计时"直觉。比较稳定：
        // 同一 settings 值每帧得同一 double，未变则不重建（不会每帧重置计时）。
        if (mpAutosave == nullptr
            || mpAutosave->GetConfig().intervalSeconds   != wantInterval
            || mpAutosave->GetConfig().minSecondsBetween != wantMin)
        {
            Orange::Engine::Save::AutosaveScheduler::Config cfg;
            cfg.intervalSeconds   = wantInterval;
            cfg.minSecondsBetween = wantMin;
            mpAutosave = std::make_unique<Orange::Engine::Save::AutosaveScheduler>(
                cfg, [this] { DoAutosave(); });
        }
    }

    // 仅 Edit 态推进；Play/Paused 有独立快照机制不叠加。恢复 modal 未决前不
    // 推进（避免 autosave 覆盖待恢复文件）。
    if (mpAutosave != nullptr
        && mHost.scene.playState == PlayState::Edit
        && !mPendingAutosaveRecovery)
    {
        mpAutosave->Update(static_cast<double>(dt));
    }
}

void EditorRenderLayer::DrawAutosaveRecoveryPopup()
{
    if (!mPendingAutosaveRecovery) { return; }

    static constexpr const char* kPopupId = "##autosave_recover";
    ImGui::OpenPopup(kPopupId);
    if (ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("发现自动存档 —— 上次 OrangeEditor 可能未正常退出。");
        ImGui::TextDisabled("对应场景：%s",
                            mAutosaveRecoverOrigin.empty()
                                ? "(未命名)"
                                : mAutosaveRecoverOrigin.c_str());
        ImGui::Separator();
        ImGui::TextDisabled("恢复 = 加载自动存档（标记为未保存）  /  丢弃 = 删除自动存档");
        ImGui::Separator();

        if (ImGui::Button("恢复"))
        {
            auto pNew = std::make_unique<Orange::Engine::World>();
            Orange::Engine::Scene::LoadOptions loadOpts;
            loadOpts.assetRegistry          = mHost.assets.pAssets.get();
            loadOpts.animatorRegistry       = mHost.assets.pAnimators.get();
            loadOpts.namedMaterialInstances = &mHost.assets.namedMaterialInstances;
            loadOpts.materialResolver       =
                [this](const std::string& id) { return ::EnsureMaterialInstance(mHost, id); };
            loadOpts.extraSerializers       = mHost.extraSerializers;
            const auto rc = Orange::Engine::Scene::Load(
                AutosaveScenePathStr(), *pNew, loadOpts);
            if (rc.IsErr())
            {
                ORANGE_LOG_ERROR("[autosave] 恢复加载失败 (code={})，保留当前场景",
                                 static_cast<unsigned>(rc.Error()));
            }
            else
            {
                mHost.scene.pWorld = std::move(pNew);
                // 重建 partition（同单文件 Open 路径：扫 LayerComponent 自动 AddLayer）。
                mHost.scene.partition = Orange::Engine::Scene::WorldPartition{};
                {
                    auto& reg = mHost.scene.pWorld->Registry();
                    using LC  = Orange::Engine::Scene::LayerComponent;
                    for (auto e : reg.view<LC>())
                    {
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
                mHost.scene.currentScenePath = mAutosaveRecoverOrigin;
                mHost.scene.dirty = true;  // 恢复内容尚未真正写回原文件
                ResetEntityLocalState();
                mHost.cmdStack.Clear();
                ORANGE_LOG_INFO("[autosave] 已恢复自动存档（标记为未保存）");
            }
            mPendingAutosaveRecovery = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("丢弃"))
        {
            ClearAutosaveFile();
            mPendingAutosaveRecovery = false;
            ImGui::CloseCurrentPopup();
            ORANGE_LOG_INFO("[autosave] 用户丢弃自动存档");
        }
        ImGui::EndPopup();
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

            // B2.6：进 Play 前停掉编辑期 clip 预览并归位（Seek(0)）。预览与
            // Play 模式的全量 TickAnimators 互斥——绝不让两者并存双写同一
            // animator 的 elapsed。归位让 Play 从 t0 一致开始（且 Play 期对
            // ECS 的修改在 Stop 时由快照还原，与归位语义不冲突）。
            if (mHost.animPreview.previewEntity.IsValid()
                && mHost.scene.pWorld != nullptr)
            {
                using AC = Orange::Engine::Animation::AnimatorComponent;
                AC* pAc = mHost.scene.pWorld->GetComponent<AC>(
                    mHost.animPreview.previewEntity);
                auto* pClip = (pAc != nullptr && pAc->animator)
                    ? dynamic_cast<Orange::Engine::Animation::ClipAnimator*>(
                          pAc->animator.get())
                    : nullptr;
                if (pClip != nullptr) { pClip->Seek(0.0f); }
            }
            mHost.animPreview.Clear();

            // S2: World 快照落盘 —— Stop 时从此路径还原，保证 Play 期
            //     对 ECS 的所有修改（物理驱动 Transform / 粒子spawn）都
            //     能被丢弃，回到 Play 前的编辑状态。
            {
                namespace fs = std::filesystem;
                // 文件名带进程 id —— 兑现 EditorSceneContext.h "temp dir 下唯一
                // 文件名" 的承诺：多编辑器实例同时 Play 时各自快照不互相覆盖
                // （否则一个实例 Stop 会从另一个的快照还原错 World）。Stop 时
                // remove + editor 退出清理；崩溃残留按 pid 隔离，不污染新实例。
                mHost.scene.playSnapshotPath =
                    (fs::temp_directory_path() /
                     ("OrangeEditor_play_snapshot_"
                      + std::to_string(GetCurrentProcessId())
                      + ".scene.json")).string();
                
                Orange::Engine::Scene::SaveOptions saveOpts;
                saveOpts.assetRegistry          = mHost.assets.pAssets.get();
                saveOpts.namedMaterialInstances = &mHost.assets.namedMaterialInstances;
                saveOpts.extraSerializers       = mHost.extraSerializers;
                const auto rc = Orange::Engine::Scene::Save(
                    *mHost.scene.pWorld, mHost.scene.playSnapshotPath, saveOpts);
                if (rc.IsErr()) {
                    ORANGE_LOG_ERROR("[OrangeEditor] Play 快照落盘失败: {} (code={}) —— "
                                     "取消进入 Play",
                                     mHost.scene.playSnapshotPath,
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

            // Audio: Edit→Play 实例化所有挂 AudioSourceComponent 的实体的
            //     SoundInstance；playOnAwake=true 即刻 Start。loop /
            //     volume 通过 SoundInstance 公共面应用（pitch 公共面未暴露，
            //     mpImpl 内 ma_sound_set_pitch 由 Audio 模块下一版本扩展时
            //     接通；本期 pitch 字段持久化但运行时无效）。
            if (mHost.audioEngine.IsInitialized()) {
                using namespace Orange::Engine::Audio;
                using namespace Orange::Engine::Asset;
                auto& reg = mHost.scene.pWorld->Registry();
                auto* pAssets = mHost.assets.pAssets.get();
                for (auto e : reg.view<AudioSourceComponent>()) {
                    auto& as = reg.get<AudioSourceComponent>(e);
                    if (!as.sound.IsValid() || pAssets == nullptr) { continue; }
                    const auto* pSoundAsset = pAssets->Get<SoundAsset>(as.sound);
                    if (pSoundAsset == nullptr) { continue; }
                    auto inst = mHost.audioEngine.CreateInstance(*pSoundAsset);
                    if (!inst.IsValid()) { continue; }
                    inst.SetVolume(as.volume);
                    if (as.playOnAwake) {
                        inst.Start();
                    }
                    Orange::Engine::Entity eWrap{static_cast<std::uint64_t>(
                        static_cast<std::uint32_t>(e))};
                    mEntityToSoundInstance[eWrap] = std::make_unique<
                        SoundInstance>(std::move(inst));
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
                    ORANGE_LOG_ERROR("[OrangeEditor] VfxSystem::Initialize 失败 (code={})",
                                     static_cast<unsigned>(rc.Error()));
                    mpVfxSystem.reset();
                } else {
                    mpScenePipeline->SetVfxSystem(mpVfxSystem.get());
                    ORANGE_LOG_INFO("[play] VfxSystem 初始化成功，粒子 tick 已启动");
                }
            } else {
                ORANGE_LOG_WARN("[play] VfxSystem 跳过：Pipeline={}  Assets={}",
                                mpScenePipeline ? "ok" : "null",
                                mHost.assets.pAssets  ? "ok" : "null");
            }

            mHost.scene.playState = PlayState::Play;
            ORANGE_LOG_INFO("[play] Edit → Play");
            break;
        }
        case PlayOp::Pause: {
            if (mHost.scene.playState != PlayState::Play) { break; }
            mHost.scene.playState = PlayState::Paused;
            ORANGE_LOG_INFO("[play] Play → Paused");
            break;
        }
        case PlayOp::Resume: {
            if (mHost.scene.playState != PlayState::Paused) { break; }
            mHost.scene.playState = PlayState::Play;
            ORANGE_LOG_INFO("[play] Paused → Play");
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

            // Audio: 清表 → SoundInstance 析构 → ma_sound_uninit 自动停播
            mEntityToSoundInstance.clear();

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
                
                Orange::Engine::Scene::LoadOptions loadOpts;
                loadOpts.assetRegistry          = mHost.assets.pAssets.get();
                loadOpts.animatorRegistry       = mHost.assets.pAnimators.get();
                loadOpts.namedMaterialInstances = &mHost.assets.namedMaterialInstances;
                // 回放快照恢复同样走磁盘 lazy-create 兜底，保证 Stop 后 material 不丢。
                loadOpts.materialResolver       =
                    [this](const std::string& id) { return ::EnsureMaterialInstance(mHost, id); };
                loadOpts.extraSerializers       = mHost.extraSerializers;
                const auto rc = Orange::Engine::Scene::Load(
                    mHost.scene.playSnapshotPath, *pNew, loadOpts);
                if (rc.IsErr()) {
                    ORANGE_LOG_ERROR("[OrangeEditor] Play 快照还原失败 (code={})，"
                                     "保留 Play 后的 World",
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
            ORANGE_LOG_INFO("[play] {} → Edit", prevLabel);
            break;
        }
        case PlayOp::None:
            break;
    }
}

// ---------------------------------------------------------------------------
// v1.1 T2：外部 DCC 资产 import 路径
//
// 两条入站路径合并 drain：
//   (1) File→Import... 菜单 → mPendingImportDialog=true → 本函数帧首
//       ShowImportFileDialog 拿路径 push 到 host.pendingImports
//   (2) OS drag-drop → main.cpp glfwSetDropCallback → 直接 push 队列
// 然后逐条 ImportDispatcher::Dispatch 处理 + drain 完队列。
//
// dialog 模态阻塞与 ImGui frame 不冲突的理由同 ApplyPendingSceneOp：
// dialog 在独立 STA worker 线程跑，主线程 join 等结果——dialog 本身
// 就是模态阻塞 UX。
// ---------------------------------------------------------------------------
void EditorRenderLayer::ApplyPendingImports()
{
    if (mPendingImportDialog)
    {
        mPendingImportDialog = false;
        // 主窗口 HWND 取自 GLFW；当前编辑器只有一个主窗口。
        auto* glfwWin = static_cast<GLFWwindow*>(
            mAppHost.GetWindow().GetGlfwWindowHandle());
        void* hwnd = (glfwWin != nullptr) ? glfwGetWin32Window(glfwWin) : nullptr;
        std::string picked;
        if (ShowImportFileDialog(hwnd, picked) && !picked.empty())
        {
            mHost.pendingImports.push_back(std::move(picked));
        }
    }

    // scene-level 导入（GAP-2026-05-28 G1/G3）—— 与 asset import 不同路径：
    // 直接走 RunGltfSceneImportToRegistry（保留层级 + 每 mesh 不塌平 + 灯光），
    // 产出 assets/scenes/<name>.scene.json。不进 pendingImports 队列（那条是
    // Dispatch 的 asset import）。导入后用户从资产浏览器双击 .scene.json 打开
    // （或 File→Open），不在此自动 swap World（避免与未保存场景冲突）。
    if (mPendingImportSceneDialog)
    {
        mPendingImportSceneDialog = false;
        auto* glfwWin = static_cast<GLFWwindow*>(
            mAppHost.GetWindow().GetGlfwWindowHandle());
        void* hwnd = (glfwWin != nullptr) ? glfwGetWin32Window(glfwWin) : nullptr;
        std::string picked;
        if (ShowImportFileDialog(hwnd, picked) && !picked.empty())
        {
            if (mHost.assets.pAssets != nullptr)
            {
                const auto r = ::Orange::Editor::Import::RunGltfSceneImportToRegistry(
                    picked, *mHost.assets.pAssets);
                if (r.status == ::Orange::Editor::Import::ImportStatus::Success)
                {
                    ORANGE_LOG_INFO("Import glTF Scene: '{}' -> '{}' ({})",
                                    picked, r.destPath, r.message);
                }
                else
                {
                    ORANGE_LOG_ERROR("Import glTF Scene failed: '{}': {}",
                                     picked, r.message);
                }
            }
            else
            {
                ORANGE_LOG_ERROR("Import glTF Scene: AssetRegistry unavailable");
            }
        }
    }

    if (mHost.pendingImports.empty()) { return; }

    // 整段 drain 走完一帧；逐条 Dispatch 期间允许 callback 继续 push 进
    // 队列（drop 时机点是 glfwPollEvents，OnUpdate 之前；本帧只处理"截
    // 至帧首入队"的请求，新 drop 留下帧）。swap 出去再迭代避免迭代过程
    // 中 vector reallocate 引用失效。
    std::vector<std::string> drainList;
    drainList.swap(mHost.pendingImports);
    for (const auto& src : drainList)
    {
        ::Orange::Editor::Import::Dispatch(src, mHost);
    }
}

// ---------------------------------------------------------------------------
// 小面板（Assets / Console）—— 占位 + 帧统计
// ---------------------------------------------------------------------------

// v0.5 c3 Asset 浏览器实现 helpers（anonymous namespace 局部可见）。
namespace
{

// v1.1.1 · Create Material modal 跨帧状态。
//
// 状态机：BeginPopupContextWindow → Create → Material menu item 内只
// 设 sPendingOpenCreateMaterial = true（不直接 OpenPopup，因 menu 处
// 于 context popup 的 ID stack 内，嵌套 OpenPopup 会跟着 context popup
// 一起被关闭，与 AboutOrangeEditor 的 sPendingOpenAbout pattern 同源）；
// 下一帧 DrawAssetsPanel 内 ImGui::End() 之后消费 pending 标志位 → 调
// OpenPopup + BeginPopupModal 在全局 viewport-level ID stack 绘制。
//
// 文件名冲突走二级 modal：主 modal 的 Create 按钮检测 fs::exists 命中
// 时 set sPendingOpenOverwriteConfirm = true + 关主 modal；下一帧绘制
// overwrite 二级 modal 询问。
bool sPendingOpenCreateMaterial   = false;
bool sPendingOpenOverwriteConfirm = false;
char sNewMaterialFilenameBuf[128] = "new_material.material";
std::vector<std::string> sNewMaterialTemplateNames;
int  sNewMaterialTemplateIdx      = 0;
// 落盘目标完整路径（browserCurrentDir + "/" + filename），主 modal 与
// overwrite 二级 modal 共用，避免二级 modal 重复拼路径产生歧义。
std::string sNewMaterialTargetPath;

// Create Prefab modal 的跨帧状态（与 Create Material 同 pattern；prefab 专属
// 故另起一组 static，不复用材质组）。源根经 EditorPrefabActions 的跨 TU 请求
// 队列从 EntityTreePanel 传来，这里只持 modal 自身的 buffer + open 标志 +
// overwrite 二级 modal 跨帧状态。
bool        sPrefabModalOpen           = false;  // 主 modal 当前是否应打开
bool        sPendingOpenPrefabOverwrite = false; // 文件已存在 → 触发二级 modal
Orange::Engine::Entity sPrefabSourceRoot =
    Orange::Engine::Entity::Invalid();            // 源根（modal 期间持有）
char        sNewPrefabFilenameBuf[128] = "new.prefab.json";
std::string sNewPrefabTargetPath;                 // 主 + overwrite 二级共用

// 内部 helper：执行 WriteMaterialFile + 落盘成功时切 selectedAssetPath 让
// Material Inspector 子模式立刻接管；失败仅 log，不弹错误 modal（与
// MaterialFileIO 既有失败口径一致——stderr 已经记录）。
void CommitNewMaterialFile(EditorAssetContext& assets,
                           const std::string&  targetPath,
                           const std::string&  templateName)
{
    Orange::Editor::Material::MaterialFileData data;
    data.templateName = templateName;
    const bool ok = Orange::Editor::Material::WriteMaterialFile(
        targetPath, data);
    if (ok) {
        assets.selectedAssetPath = targetPath;
        ORANGE_LOG_INFO("Asset Browser: created material '{}' "
                        "(template '{}')",
                        targetPath, templateName);
    } else {
        ORANGE_LOG_ERROR("Asset Browser: failed to write material '{}'",
                         targetPath);
    }
}

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
            // 跳过点前缀目录（如软删除 .trash）—— 不在资产树里展示。
            if (entry.is_directory(ec)
                && entry.path().filename().string().rfind('.', 0) != 0)
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
// 资产类型分类（供类型过滤下拉用）。返回值对齐 kAssetTypeNames 索引：
// 0=All（占位，不用于文件）/ 1=Mesh / 2=Material / 3=Texture / 4=Sound /
// 5=Scene / 6=Animation / 7=Other。比 icon 分类粗（hdr/exr 归 Texture）——过滤够用。
int AssetCategoryOf(const std::string& name, const std::string& ext)
{
    if (ext == ".mesh" || ext == ".obj") { return 1; }
    if (ext == ".material") { return 2; }
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".ktx"
        || ext == ".hdr" || ext == ".exr") { return 3; }
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac") { return 4; }
    if (name.size() >= 11
        && name.compare(name.size() - 11, 11, ".scene.json") == 0) { return 5; }
    if (ext == ".anim") { return 6; }  // 关键帧动画 clip（ClipAnimator 的 clip 来源）
    return 7;
}

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

    // 资产名搜索过滤（大小写不敏感，复用 ContainsCaseInsensitive）。空串=不过滤。
    // 资产变多后按名查找用（gap 报告 §2.2）。buffer 文件级 static（单 Assets 面板）。
    static char sAssetSearchBuf[128] = {};
    static int  sAssetTypeFilter     = 0;  // 0=All；1..7 对齐 AssetCategoryOf
    static const char* const kAssetTypeNames[] = {
        "All", "Mesh", "Material", "Texture", "Sound", "Scene", "Animation", "Other" };
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("Material____").x);  // 容下最长项+箭头
    ImGui::Combo("##asset_type", &sAssetTypeFilter,
                 kAssetTypeNames, IM_ARRAYSIZE(kAssetTypeNames));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);  // 填满剩余列宽（禁像素字面量）
    ImGui::InputTextWithHint("##asset_search", "search assets...",
                             sAssetSearchBuf, sizeof(sAssetSearchBuf));
    const std::string_view assetSearch{sAssetSearchBuf};

    // 选中资产的引用计数（只读依赖扫描，删/改资产前看牵连——gap §2.2 依赖
    // 追踪第一步；写侧 rename/delete + 批量改引用留 focused session）。仅选中
    // 某文件时显示；hover 列出引用它的 (entity, component.field)。
    if (!assets.selectedAssetPath.empty())
    {
        const auto refs = Orange::Editor::FindAssetReferences(host, assets.selectedAssetPath);
        if (refs.empty())
        {
            ImGui::TextDisabled("selected asset: no entity references");
        }
        else
        {
            ImGui::TextDisabled("selected asset: referenced by %zu field(s)", refs.size());
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                for (const auto& r : refs)
                {
                    ImGui::Text("entity #%u — %s.%s",
                                static_cast<unsigned>(
                                    static_cast<std::uint32_t>(r.entity.Value())),
                                r.componentType, r.fieldName);
                }
                ImGui::EndTooltip();
            }
        }
        ImGui::Separator();
    }

    int shownCount = 0;

    for (const auto& f : files)
    {
        const std::string path = f.generic_string();
        const std::string name = f.filename().string();
        const std::string ext  = f.extension().string();
        if (!Orange::Editor::Util::ContainsCaseInsensitive(name, assetSearch)) { continue; }
        if (sAssetTypeFilter != 0
            && AssetCategoryOf(name, ext) != sAssetTypeFilter) { continue; }
        ++shownCount;

        const char* icon = "[?]";
        if      (ext == ".mesh" || ext == ".obj")   icon = "[M]";
        else if (ext == ".material")                icon = "[Mat]";
        else if (ext == ".png" || ext == ".jpg"
              || ext == ".jpeg" || ext == ".ktx")   icon = "[T]";
        else if (ext == ".hdr" || ext == ".exr")    icon = "[HDR]";
        else if (ext == ".wav" || ext == ".ogg"
              || ext == ".mp3" || ext == ".flac")   icon = "[SND]";
        else if (ext == ".anim")                    icon = "[Anim]";
        // .prefab.json 必须先于 .scene.json / .json 判定：三者 extension() 都
        // 返回 ".json"，按完整后缀 name 区分。
        else if (name.size() >= 12
              && name.compare(name.size() - 12, 12, ".prefab.json") == 0)
                                                    icon = "[Prefab]";
        else if (name.size() >= 11
              && name.compare(name.size() - 11, 11, ".scene.json") == 0)
                                                    icon = "[S]";
        else if (ext == ".json")                    icon = "[J]";

        const bool selected = (path == assets.selectedAssetPath);

        // .material：尝试取渲染缩略图（材质球 RT）。命中 → 在文件名左侧画
        // 64×64 缩略图 + SameLine，替代 "[Mat]" 文本 icon；未命中（首次见 /
        // Pipeline 未就绪 / 烘焙中）→ GetOrRequestThumbnail 已入 pending，本帧
        // 回退文本 icon，烘好后下一帧自动出图。DnD source / 右键 ContextItem /
        // 选中逻辑全部锚到下方 Selectable item，缩略图只是其左侧的视觉装饰。
        bool drewThumb = false;
        if (ext == ".material" && host.thumbnails)
        {
            const ImTextureID thumbId =
                host.thumbnails->GetOrRequestThumbnail(path);
            if (thumbId != 0)
            {
                ImGui::Image(thumbId, ImVec2(64.0f, 64.0f));
                ImGui::SameLine();
                drewThumb = true;
            }
        }
        // .prefab.json：与 .material 对位走 prefab 缩略图（实例化到 scratch
        // world → 算 AABB 框相机 → RT 预览）。命中 → 画 64×64 缩略图替代
        // "[Prefab]" 文本 icon；未命中（首次见 / Pipeline 未就绪 / 烘焙中）→
        // GetOrRequestPrefabThumbnail 已入 pending，本帧回退文本 icon。
        else if (host.thumbnails && name.size() >= 12
              && name.compare(name.size() - 12, 12, ".prefab.json") == 0)
        {
            const ImTextureID thumbId =
                host.thumbnails->GetOrRequestPrefabThumbnail(path);
            if (thumbId != 0)
            {
                ImGui::Image(thumbId, ImVec2(64.0f, 64.0f));
                ImGui::SameLine();
                drewThumb = true;
            }
        }
        // .mesh：与 .material / .prefab.json 对位走 mesh 缩略图（用默认 PBR 材质
        // 渲单 mesh → 算 local AABB 框相机 → RT 预览）。命中 → 画 64×64 缩略图
        // 替代 "[M]" 文本 icon；未命中（首次见 / Pipeline 未就绪 / 烘焙中）→
        // GetOrRequestMeshThumbnail 已入 pending，本帧回退文本 icon。.obj 是导入
        // 源（非引擎 .mesh 格式，AssetRegistry 不直接 Load），仍走文本 icon。
        else if (ext == ".mesh" && host.thumbnails)
        {
            const ImTextureID thumbId =
                host.thumbnails->GetOrRequestMeshThumbnail(path);
            if (thumbId != 0)
            {
                ImGui::Image(thumbId, ImVec2(64.0f, 64.0f));
                ImGui::SameLine();
                drewThumb = true;
            }
        }
        // .scene.json：与 .material / .prefab.json / .mesh 对位走 scene snapshot
        // 缩略图（整张场景 LoadFromString 到 scratch world → 算合并 AABB 框相机 →
        // RT 预览）。命中 → 画 64×64 缩略图替代 "[S]" 文本 icon；未命中（首次见 /
        // Pipeline 未就绪 / 烘焙中）→ GetOrRequestSceneThumbnail 已入 pending，
        // 本帧回退文本 icon。.scene.json 必须按完整后缀 name 判定（与上面 icon
        // 赋值同纪律——extension() 返回 ".json"，会被 .prefab.json / 普通 .json 撞）。
        else if (host.thumbnails && name.size() >= 11
              && name.compare(name.size() - 11, 11, ".scene.json") == 0)
        {
            const ImTextureID thumbId =
                host.thumbnails->GetOrRequestSceneThumbnail(path);
            if (thumbId != 0)
            {
                ImGui::Image(thumbId, ImVec2(64.0f, 64.0f));
                ImGui::SameLine();
                drewThumb = true;
            }
        }

        char labelBuf[512];
        std::snprintf(labelBuf, sizeof(labelBuf), "%s %s",
                      drewThumb ? "" : icon, name.c_str());
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
        // 双击 .scene.json → 请求打开该场景（Unity/Lumix 标准）。Selectable 单击
        // 已设 selectedAssetPath；这里叠加双击 = 打开。经 host.scene 桥接到
        // EditorRenderLayer 的 Open 流程（见 requestedOpenScenePath 注释）。
        if (ImGui::IsItemHovered()
            && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
            && name.size() >= 11
            && name.compare(name.size() - 11, 11, ".scene.json") == 0)
        {
            host.scene.requestedOpenScenePath = path;
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
        // 注意：BeginPopupContextItem 不传 str_id，让 ImGui 用 LastItemID
        // （即 Selectable 的 ID）作为 popup 唯一 ID。若传固定 str_id，loop
        // 内每个 file 共享同一个 popup ID，open 状态下所有 file 的 BeginPopup
        // 都返回 true → 菜单被画 N 次 → 同名 MenuItem ID 冲突报错。
        if (ImGui::BeginPopupContextItem())
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

            // "Add to Scene"：直接在相机焦点处建一个带该 mesh + 材质的新实体
            // （不需要先选中实体；对齐 Lumix/Unity 从 Asset 浏览器 instantiate）。
            // 与拖到 viewport 空白处（ScenePanel）同一条 CreateEntityFromMeshAsset。
            const bool canAddToScene = (ext == ".mesh" || ext == ".obj")
                                    && host.scene.pWorld != nullptr;
            ImGui::BeginDisabled(!canAddToScene);
            if (ImGui::MenuItem("Add to Scene"))
            {
                const Entity created = ::Orange::Editor::CreateEntityFromMeshAsset(
                    host, path, host.camera.pivot);
                if (created.IsValid())
                {
                    host.selection.selectedEntity = created;
                    host.selection.ClearAdditional();
                    host.assets.selectedAssetPath.clear();
                }
            }
            ImGui::EndDisabled();

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
                    if (p.empty()) { pRC->mesh = {}; }
                    else {
                        auto* pReg = pH->assets.pAssets.get();
                        if (pReg == nullptr) { return; }
                        auto lr = pReg->Load<
                            ::Orange::Engine::Asset::MeshAsset>(p);
                        if (lr.IsOk()) { pRC->mesh = lr.Value(); }
                    }
                    // 设 mesh 后同步多材质 slot → SubMeshMaterialsComponent（与
                    // viewport drop / Inspector 设 mesh 一致；单材质设
                    // materialInstance，空 path 撤组件）。
                    ::Orange::Editor::SyncSubMeshMaterialsForMesh(*pH, capE, p);
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

            // Pick to AudioSource.sound —— 选中实体挂 AudioSourceComponent
            // 时才启用；与 Pick to Renderable.mesh 同款命令栈 replay 路径
            // （SetFieldValueCommand<std::string> 持旧 / 新 path，Undo 回退）。
            using ::Orange::Engine::Audio::AudioSourceComponent;
            const auto* ac = selValid
                ? host.scene.pWorld->GetComponent<AudioSourceComponent>(selEntity)
                : nullptr;
            const bool canPickAudio = (ac != nullptr)
                                   && (ext == ".wav" || ext == ".ogg"
                                    || ext == ".mp3" || ext == ".flac");
            // v1.1 T5：Reimport 入口。当当前 asset 同目录存在 .meta sidecar
            // 时显示（说明这是 importer 产物）。点击读 .meta 拿 sourcePath，
            // push 到 host.pendingImports 队列让 ApplyPendingImports 帧末 drain。
            // 与 OS drag-drop / File→Import 走同款 Dispatch 路径，hash 增量短
            // 路自动生效（源文件没变 → log 'unchanged, skipped'）。
            {
                namespace fs = std::filesystem;
                std::error_code metaEc;
                const std::string metaPath =
                    ::Orange::Editor::Import::MetaPathFor(path);
                const bool hasMeta = fs::exists(metaPath, metaEc)
                                  && !metaEc;
                ImGui::Separator();
                ImGui::BeginDisabled(!hasMeta);
                if (ImGui::MenuItem("Reimport"))
                {
                    auto meta = ::Orange::Editor::Import::ReadTextureMeta(metaPath);
                    if (meta.has_value() && !meta->sourcePath.empty())
                    {
                        // 源 hash 若与 .meta 记录一致 → Dispatch 内 hash 短路
                        // 直接 return Success；不一致 → 完整重 import 路径。
                        // 用户主动 Reimport 时若源路径已失效，Dispatch 会在
                        // src 不存在分支返 SourceReadFailed + log ERROR。
                        host.pendingImports.push_back(meta->sourcePath);
                    }
                    else
                    {
                        ORANGE_LOG_ERROR("Asset Browser: reimport '{}' "
                                         "—— .meta missing sourcePath",
                                         path);
                    }
                }
                ImGui::EndDisabled();
            }

            ImGui::BeginDisabled(!canPickAudio);
            if (ImGui::MenuItem("Pick to AudioSource.sound"))
            {
                using ::Orange::Engine::Asset::SoundAsset;
                std::string oldPath;
                if (ac != nullptr && ac->sound.IsValid()
                    && host.assets.pAssets != nullptr)
                {
                    oldPath = std::string{host.assets.pAssets
                        ->PathOf<SoundAsset>(ac->sound)};
                }
                auto apply = [pH = &host, capE = selEntity]
                              (const std::string& p) {
                    auto* pW = pH->scene.pWorld.get();
                    if (pW == nullptr || !pW->IsValid(capE)) { return; }
                    auto* pAS = pW->GetComponent<AudioSourceComponent>(capE);
                    if (pAS == nullptr) { return; }
                    if (p.empty()) { pAS->sound = {}; return; }
                    auto* pReg = pH->assets.pAssets.get();
                    if (pReg == nullptr) { return; }
                    auto lr = pReg->Load<
                        ::Orange::Engine::Asset::SoundAsset>(p);
                    if (lr.IsOk()) { pAS->sound = lr.Value(); }
                };
                host.cmdStack.Push(
                    std::make_unique<SetFieldValueCommand<std::string>>(
                        selEntity, "AudioSource.sound",
                        oldPath, path, std::move(apply)));
            }
            ImGui::EndDisabled();

            // ---- Rename（仅 handle 类资产：mesh/texture/sound/普通 data）----
            // material 因 MaterialInstance ptr 跨 BuildNamedMaterialInstances
            // 重建身份会变、rename 不可靠 → 禁用；.scene.json 路径由场景系统
            // 管理也不在此 rename。可逆：cmdStack do = fs::rename 文件 + .meta +
            // RemapAssetReferences(old→new)（handle 类经 assetRefSet 内部 Load
            // 新 path 拿新 handle）；undo = 反向。filesystem mutation 但 reversible。
            ImGui::Separator();
            {
                namespace fs = std::filesystem;
                const bool isMaterial = (ext == ".material");
                const bool isScene = (name.size() >= 11
                    && name.compare(name.size() - 11, 11, ".scene.json") == 0);
                const bool renamable = !isMaterial && !isScene && !ext.empty();
                if (!renamable)
                {
                    ImGui::TextDisabled(isMaterial
                        ? "(rename: material not supported — instance remap)"
                        : "(rename: not supported for this asset)");
                }
                else
                {
                    static char sAssetRenameBuf[256] = {};
                    if (ImGui::IsWindowAppearing())
                    {
                        std::snprintf(sAssetRenameBuf, sizeof(sAssetRenameBuf),
                                      "%s", name.c_str());
                    }
                    ImGui::SetNextItemWidth(ImGui::CalcTextSize("MMMMMMMMMMMMMMMMMMMM").x);
                    ImGui::InputText("##asset_rename", sAssetRenameBuf,
                                     sizeof(sAssetRenameBuf));
                    const std::string newName = sAssetRenameBuf;
                    const auto        slash    = path.find_last_of('/');
                    const std::string dir      = (slash != std::string::npos)
                        ? path.substr(0, slash) : std::string{};
                    const std::string newPath  = dir.empty()
                        ? newName : (dir + "/" + newName);
                    std::error_code rec;
                    const bool sameExt = (fs::path(newName).extension().string() == ext);
                    const bool exists  = fs::exists(newPath, rec);
                    const bool valid   = !newName.empty() && newName != name
                                      && sameExt && !exists;

                    const auto refs = Orange::Editor::FindAssetReferences(host, path);
                    ImGui::TextDisabled("renames file + .meta + %zu reference(s)",
                                        refs.size());
                    if (!sameExt && !newName.empty())
                    {
                        ImGui::TextColored(Orange::Editor::Theme::Color::GetAlertWarn(),
                                           "keep extension %s", ext.c_str());
                    }
                    else if (exists)
                    {
                        ImGui::TextColored(Orange::Editor::Theme::Color::GetAlertWarn(),
                                           "target already exists");
                    }

                    ImGui::BeginDisabled(!valid);
                    if (ImGui::Button("Rename"))
                    {
                        const std::string oldP    = path;
                        const std::string newP    = newPath;
                        const std::string oldMeta = ::Orange::Editor::Import::MetaPathFor(oldP);
                        const std::string newMeta = ::Orange::Editor::Import::MetaPathFor(newP);
                        std::error_code   mec;
                        const bool        hasMeta = fs::exists(oldMeta, mec) && !mec;
                        auto*             pH      = &host;
                        host.cmdStack.Push(std::make_unique<LambdaCommand>(
                            "rename_asset",
                            [pH, oldP, newP, oldMeta, newMeta, hasMeta]() {
                                std::error_code ec;
                                std::filesystem::rename(oldP, newP, ec);
                                if (ec) { return; }  // rename 失败 → 不动引用
                                if (hasMeta) {
                                    std::error_code mec2;
                                    std::filesystem::rename(oldMeta, newMeta, mec2);
                                }
                                Orange::Editor::RemapAssetReferences(*pH, oldP, newP);
                                if (pH->assets.selectedAssetPath == oldP) {
                                    pH->assets.selectedAssetPath = newP;
                                }
                            },
                            [pH, oldP, newP, oldMeta, newMeta, hasMeta]() {
                                std::error_code ec;
                                std::filesystem::rename(newP, oldP, ec);
                                if (ec) { return; }
                                if (hasMeta) {
                                    std::error_code mec2;
                                    std::filesystem::rename(newMeta, oldMeta, mec2);
                                }
                                Orange::Editor::RemapAssetReferences(*pH, newP, oldP);
                                if (pH->assets.selectedAssetPath == newP) {
                                    pH->assets.selectedAssetPath = oldP;
                                }
                            }));
                        ORANGE_LOG_INFO("[OrangeEditor] renamed asset '{}' -> '{}'",
                                        oldP, newP);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndDisabled();
                }
            }

            // ---- Delete（软删除：move 到 <dir>/.trash/，可 Undo）----
            // 不真 fs::remove（不可逆）；move 到同目录 .trash 子目录 + 清空所有
            // 引用本资产的组件字段（避免悬空）。cmdStack 可 undo（move 回 + 还原
            // 引用）。适用所有类型——material 的 assetRefSet("") 也清 ptr，undo
            // 还原文件后 RestoreAssetReferences 经 assetRefSet(path) 重指。
            ImGui::Separator();
            {
                namespace fs = std::filesystem;
                const auto refsDel = Orange::Editor::FindAssetReferences(host, path);
                ImGui::TextDisabled("delete -> .trash, clears %zu reference(s) (undoable)",
                                    refsDel.size());
                if (ImGui::Button("Delete (move to .trash)"))
                {
                    const std::string oldP  = path;
                    const auto        slashD = oldP.find_last_of('/');
                    const std::string dirD   = (slashD != std::string::npos)
                        ? oldP.substr(0, slashD) : std::string{};
                    const std::string fname  = (slashD != std::string::npos)
                        ? oldP.substr(slashD + 1) : oldP;
                    const std::string trashDir = dirD.empty()
                        ? std::string(".trash") : (dirD + "/.trash");
                    const std::string trashP   = trashDir + "/" + fname;
                    const std::string oldMetaD   = ::Orange::Editor::Import::MetaPathFor(oldP);
                    const std::string trashMetaD = ::Orange::Editor::Import::MetaPathFor(trashP);
                    std::error_code   mecD;
                    const bool        hasMetaD = fs::exists(oldMetaD, mecD) && !mecD;
                    auto*             pH       = &host;
                    auto              clearedPtr =
                        std::make_shared<std::vector<Orange::Editor::ClearedAssetRef>>();
                    host.cmdStack.Push(std::make_unique<LambdaCommand>(
                        "delete_asset",
                        [pH, oldP, trashP, trashDir, oldMetaD, trashMetaD, hasMetaD, clearedPtr]() {
                            std::error_code ec;
                            std::filesystem::create_directories(trashDir, ec);
                            std::filesystem::rename(oldP, trashP, ec);
                            if (ec) { return; }
                            if (hasMetaD) {
                                std::error_code m2;
                                std::filesystem::rename(oldMetaD, trashMetaD, m2);
                            }
                            *clearedPtr = Orange::Editor::ClearAssetReferences(*pH, oldP);
                            if (pH->assets.selectedAssetPath == oldP) {
                                pH->assets.selectedAssetPath.clear();
                            }
                        },
                        [pH, oldP, trashP, oldMetaD, trashMetaD, hasMetaD, clearedPtr]() {
                            std::error_code ec;
                            std::filesystem::rename(trashP, oldP, ec);
                            if (ec) { return; }
                            if (hasMetaD) {
                                std::error_code m2;
                                std::filesystem::rename(trashMetaD, oldMetaD, m2);
                            }
                            Orange::Editor::RestoreAssetReferences(*pH, *clearedPtr, oldP);
                        }));
                    ORANGE_LOG_INFO("[OrangeEditor] soft-deleted asset '{}' -> '{}'",
                                    oldP, trashP);
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", path.c_str());
        }
    }

    // 搜索 / 类型过滤后无匹配（与"空目录"区分：空目录在函数顶部已早退）。
    if (shownCount == 0 && (!assetSearch.empty() || sAssetTypeFilter != 0))
    {
        ImGui::TextDisabled("(no assets match current filter)");
    }

    // v1.1.1 · 面板空白处右键 "Create" 菜单（关闭
    // GAP-2026-05-24-editor-asset-browser-create-material-missing G1）。
    // NoOpenOverItems：鼠标位于上面任何 Selectable 上时不打开本 popup，让
    // 单文件右键照旧走 BeginPopupContextItem（行 1362）的 Pick / Reimport
    // 菜单。两套右键互不打架。
    if (ImGui::BeginPopupContextWindow("##asset_list_ctx",
            ImGuiPopupFlags_MouseButtonRight
            | ImGuiPopupFlags_NoOpenOverItems))
    {
        if (ImGui::BeginMenu("Create"))
        {
            const bool canMakeMaterial = (assets.pMaterials != nullptr);
            ImGui::BeginDisabled(!canMakeMaterial);
            if (ImGui::MenuItem("Material"))
            {
                // 重置 buffer + 拉 template 列表 + 锁默认 pbr index。
                // pending 标志位下一帧由 DrawAssetsPanel 末尾消费。
                std::snprintf(sNewMaterialFilenameBuf,
                              sizeof(sNewMaterialFilenameBuf),
                              "new_material.material");
                sNewMaterialTemplateNames =
                    assets.pMaterials->GetTemplateNames();
                std::sort(sNewMaterialTemplateNames.begin(),
                          sNewMaterialTemplateNames.end());
                sNewMaterialTemplateIdx = 0;
                for (std::size_t i = 0;
                     i < sNewMaterialTemplateNames.size(); ++i)
                {
                    if (sNewMaterialTemplateNames[i] == "pbr")
                    {
                        sNewMaterialTemplateIdx = static_cast<int>(i);
                        break;
                    }
                }
                sPendingOpenCreateMaterial = true;
            }
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
}

// v1.1.1 · 主 Create Material modal。filename + templateName Combo +
// Create / Cancel。Create 命中既存文件时关本 modal、set
// sPendingOpenOverwriteConfirm，下一帧由二级 modal 接管。
void DrawCreateMaterialModal(EditorAssetContext& assets)
{
    constexpr const char* kPopupId = "Create Material##create_mat";
    if (sPendingOpenCreateMaterial)
    {
        ImGui::OpenPopup(kPopupId);
        sPendingOpenCreateMaterial = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kPopupId, nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
            | ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    // 宽度按字符数派生（v0.4.5 红线：禁字面像素）。约 30 个字符 + 余量
    // 够装下典型 .material 文件名 (`new_material.material` = 21 字符)。
    const float kInputW =
        ImGui::CalcTextSize("M").x * 30.0f;

    ImGui::TextUnformatted("Filename:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(kInputW);
    ImGui::InputText("##new_mat_filename",
                     sNewMaterialFilenameBuf,
                     sizeof(sNewMaterialFilenameBuf));

    ImGui::TextUnformatted("Template:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(kInputW);
    if (!sNewMaterialTemplateNames.empty())
    {
        const int clampedIdx = std::clamp<int>(
            sNewMaterialTemplateIdx, 0,
            static_cast<int>(sNewMaterialTemplateNames.size()) - 1);
        const char* curName = sNewMaterialTemplateNames[clampedIdx].c_str();
        if (ImGui::BeginCombo("##new_mat_template", curName))
        {
            for (std::size_t i = 0;
                 i < sNewMaterialTemplateNames.size(); ++i)
            {
                const bool sel =
                    (static_cast<int>(i) == sNewMaterialTemplateIdx);
                if (ImGui::Selectable(
                        sNewMaterialTemplateNames[i].c_str(), sel))
                {
                    sNewMaterialTemplateIdx = static_cast<int>(i);
                }
                if (sel) { ImGui::SetItemDefaultFocus(); }
            }
            ImGui::EndCombo();
        }
    }
    else
    {
        ImGui::TextDisabled("(no templates registered)");
    }

    // 完整目标路径预览（灰字）。
    const std::string targetPath = assets.browserCurrentDir
                                 + "/"
                                 + std::string{sNewMaterialFilenameBuf};
    ImGui::Separator();
    ImGui::TextDisabled("Path: %s", targetPath.c_str());
    ImGui::Separator();

    const bool nameNonEmpty =
        (std::strlen(sNewMaterialFilenameBuf) > 0);
    const bool templateValid =
        !sNewMaterialTemplateNames.empty()
        && (sNewMaterialTemplateIdx >= 0)
        && (sNewMaterialTemplateIdx
            < static_cast<int>(sNewMaterialTemplateNames.size()));
    const bool canCreate = nameNonEmpty && templateValid;

    ImGui::BeginDisabled(!canCreate);
    if (ImGui::Button("Create", ImVec2(120, 0)))
    {
        sNewMaterialTargetPath = targetPath;
        namespace fs = std::filesystem;
        std::error_code ec;
        const bool exists =
            fs::exists(sNewMaterialTargetPath, ec) && !ec;
        if (exists)
        {
            // 命中既存 → 关本 modal + 触发 overwrite 二级 modal。
            sPendingOpenOverwriteConfirm = true;
            ImGui::CloseCurrentPopup();
        }
        else
        {
            CommitNewMaterialFile(
                assets,
                sNewMaterialTargetPath,
                sNewMaterialTemplateNames[sNewMaterialTemplateIdx]);
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// v1.1.1 · 文件名冲突时的二级 modal。Overwrite 直接覆盖落盘；Cancel
// 返回（不重弹主 modal，让用户重新右键 Create——与 Cocos 一致）。
void DrawOverwriteConfirmModal(EditorAssetContext& assets)
{
    constexpr const char* kPopupId = "Overwrite?##overwrite_mat";
    if (sPendingOpenOverwriteConfirm)
    {
        ImGui::OpenPopup(kPopupId);
        sPendingOpenOverwriteConfirm = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kPopupId, nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
            | ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    ImGui::TextUnformatted("文件已存在：");
    ImGui::TextDisabled("%s", sNewMaterialTargetPath.c_str());
    ImGui::Separator();
    ImGui::TextWrapped("Overwrite 将覆盖现有 .material（不可 undo）；"
                       "Cancel 返回上一步。");
    ImGui::Separator();

    const bool templateValid =
        !sNewMaterialTemplateNames.empty()
        && (sNewMaterialTemplateIdx >= 0)
        && (sNewMaterialTemplateIdx
            < static_cast<int>(sNewMaterialTemplateNames.size()));

    ImGui::BeginDisabled(!templateValid);
    if (ImGui::Button("Overwrite", ImVec2(120, 0)))
    {
        CommitNewMaterialFile(
            assets,
            sNewMaterialTargetPath,
            sNewMaterialTemplateNames[sNewMaterialTemplateIdx]);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// prefab 创建 modal —— 仿 DrawCreateMaterialModal：filename InputText +
// Create / Cancel。承接 EntityTreePanel "Create Prefab..." 右键的跨 TU 请求
// （ConsumeCreatePrefabRequest）。Create 命中既存文件时关本 modal + 触发
// overwrite 二级 modal（复用同款二级 modal pattern）。
//
// 写盘走 EditorPrefabActions::CommitNewPrefabFile（纯 IO，不进命令栈，同
// CommitNewMaterialFile 口径）。源根在收到请求时锁存到 sPrefabSourceRoot。
void DrawCreatePrefabModal(EditorHost& host)
{
    namespace Prefab = Orange::Editor::Prefab;
    constexpr const char* kPopupId = "Create Prefab##create_prefab";

    // 跨帧请求：从 prefab TU 取出源根 + 初始化 buffer（默认文件名 = 源实体
    // NameComponent.name + ".prefab.json"）。在 OpenPopup 前消费，避免与
    // context popup ID stack 嵌套冲突（同 sPendingOpenCreateMaterial pattern）。
    {
        Orange::Engine::Entity reqRoot = Orange::Engine::Entity::Invalid();
        if (Prefab::ConsumeCreatePrefabRequest(&reqRoot))
        {
            sPrefabSourceRoot = reqRoot;
            std::string base = "new";
            auto* pWorld = host.scene.pWorld.get();
            if (pWorld != nullptr && pWorld->IsValid(reqRoot))
            {
                const auto* nc = pWorld->GetComponent<
                    Orange::Engine::Scene::NameComponent>(reqRoot);
                if (nc != nullptr && !nc->name.empty()) { base = nc->name; }
            }
            std::snprintf(sNewPrefabFilenameBuf, sizeof(sNewPrefabFilenameBuf),
                          "%s.prefab.json", base.c_str());
            sPrefabModalOpen = true;
        }
    }

    if (sPrefabModalOpen)
    {
        ImGui::OpenPopup(kPopupId);
        sPrefabModalOpen = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kPopupId, nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
            | ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    const float kInputW = ImGui::CalcTextSize("M").x * 30.0f;

    ImGui::TextUnformatted("Filename:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(kInputW);
    ImGui::InputText("##new_prefab_filename",
                     sNewPrefabFilenameBuf,
                     sizeof(sNewPrefabFilenameBuf));

    // 完整目标路径预览（灰字）。targetPath = browserCurrentDir + "/" + filename。
    const std::string targetPath = host.assets.browserCurrentDir
                                 + "/"
                                 + std::string{sNewPrefabFilenameBuf};
    ImGui::Separator();
    ImGui::TextDisabled("Path: %s", targetPath.c_str());
    ImGui::Separator();

    const bool nameNonEmpty = (std::strlen(sNewPrefabFilenameBuf) > 0);
    auto* pWorld = host.scene.pWorld.get();
    const bool srcValid = (pWorld != nullptr)
                       && sPrefabSourceRoot.IsValid()
                       && pWorld->IsValid(sPrefabSourceRoot);
    if (!srcValid)
    {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
                           "(source entity no longer valid)");
    }
    const bool canCreate = nameNonEmpty && srcValid;

    // prefabName = filename 去 .prefab.json 后缀（无后缀则用整名）。
    auto stripPrefabSuffix = [](const std::string& fn) -> std::string {
        constexpr const char* kSuffix = ".prefab.json";
        constexpr std::size_t kSuffixLen = 12;
        if (fn.size() > kSuffixLen
            && fn.compare(fn.size() - kSuffixLen, kSuffixLen, kSuffix) == 0)
        {
            return fn.substr(0, fn.size() - kSuffixLen);
        }
        return fn;
    };

    ImGui::BeginDisabled(!canCreate);
    if (ImGui::Button("Create", ImVec2(120, 0)))
    {
        sNewPrefabTargetPath = targetPath;
        namespace fs = std::filesystem;
        std::error_code ec;
        const bool exists = fs::exists(sNewPrefabTargetPath, ec) && !ec;
        if (exists)
        {
            sPendingOpenPrefabOverwrite = true;
            ImGui::CloseCurrentPopup();
        }
        else
        {
            Prefab::CommitNewPrefabFile(
                host, sPrefabSourceRoot, sNewPrefabTargetPath,
                stripPrefabSuffix(std::string{sNewPrefabFilenameBuf}));
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// prefab 文件名冲突时的二级 modal（仿 DrawOverwriteConfirmModal）。Overwrite
// 直接覆盖落盘；Cancel 返回（不重弹主 modal）。
void DrawPrefabOverwriteConfirmModal(EditorHost& host)
{
    namespace Prefab = Orange::Editor::Prefab;
    constexpr const char* kPopupId = "Overwrite?##overwrite_prefab";
    if (sPendingOpenPrefabOverwrite)
    {
        ImGui::OpenPopup(kPopupId);
        sPendingOpenPrefabOverwrite = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kPopupId, nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
            | ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    ImGui::TextUnformatted("文件已存在：");
    ImGui::TextDisabled("%s", sNewPrefabTargetPath.c_str());
    ImGui::Separator();
    ImGui::TextWrapped("Overwrite 将覆盖现有 .prefab.json；Cancel 返回上一步。");
    ImGui::Separator();

    auto* pWorld = host.scene.pWorld.get();
    const bool srcValid = (pWorld != nullptr)
                       && sPrefabSourceRoot.IsValid()
                       && pWorld->IsValid(sPrefabSourceRoot);

    auto stripPrefabSuffix = [](const std::string& fn) -> std::string {
        constexpr const char* kSuffix = ".prefab.json";
        constexpr std::size_t kSuffixLen = 12;
        if (fn.size() > kSuffixLen
            && fn.compare(fn.size() - kSuffixLen, kSuffixLen, kSuffix) == 0)
        {
            return fn.substr(0, fn.size() - kSuffixLen);
        }
        return fn;
    };

    ImGui::BeginDisabled(!srcValid);
    if (ImGui::Button("Overwrite", ImVec2(120, 0)))
    {
        Prefab::CommitNewPrefabFile(
            host, sPrefabSourceRoot, sNewPrefabTargetPath,
            stripPrefabSuffix(std::string{sNewPrefabFilenameBuf}));
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
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
    if (ImGui::SmallButton(Orange::Editor::Theme::Icon::GetArrowUp()))
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
                // 跳过点前缀目录（如软删除 .trash）。
                if (entry.is_directory(ec)
                    && entry.path().filename().string().rfind('.', 0) != 0)
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

    // v1.1.1 · Create Material modal + overwrite 二级 modal。放在 End()
    // 之后让 ID stack 处于 viewport-level（与 AboutOrangeEditor 同款），
    // 避免 popup 父级挂在 "Assets" 窗口而被其布局影响。
    DrawCreateMaterialModal(assets);
    DrawOverwriteConfirmModal(assets);
    // prefab 创建 modal + overwrite 二级 modal —— 与材质 modal 同位（End()
    // 之后，viewport-level ID stack）。承接 EntityTreePanel 右键 "Create
    // Prefab..." 的跨 TU 请求并写盘。
    DrawCreatePrefabModal(mHost);
    DrawPrefabOverwriteConfirmModal(mHost);
}

// DrawAnimationPanel 的实现已迁到 panels/AnimationTimelinePanel.cpp（B2.3
// timeline / dopesheet）。底部 tab 容器（Assets / Console / Animation 三 tab）
// 的 dock 布局仍由 BuildDefaultLayoutOnce 建。

// v0.8 Console 面板：接 Core::Log SetLogSink 路径，渲染 ring buffer 内
// 的日志条目；filter by level + search 字符串。Header 仍保留旧的 frame
// 信息 + Esc 退出按钮便于快速操作。
//
// 线程安全：mLogMutex 保护 mLogEntries，sink callback 在任意线程 push、
// Draw 在 ImGui 线程 read，两端持锁。
void EditorRenderLayer::DrawConsolePanel(const Orange::Engine::FrameContext& frame)
{
    ImGui::Begin("Console");

    ImGui::Text("OrangeEditor v0.0.3  frame=%llu  Δ=%.2fms",
                static_cast<unsigned long long>(frame.time.frameIndex),
                frame.time.deltaSeconds * 1000.0);

    // Filter row：level 下拉 + search 文本框 + Clear / Auto-scroll / Quit 按钮
    ImGui::Separator();
    {
        // widths 由 CalcTextSize 派生（v0.4.5 lint：禁止像素字面量）。
        const float levelW  = ImGui::CalcTextSize("Critical XX").x;
        const float searchW = ImGui::CalcTextSize("search........").x * 2.0f;
        ImGui::SetNextItemWidth(levelW);
        const char* kLevelLabels[] = {
            "Trace+", "Debug+", "Info+", "Warn+", "Error+", "Critical"
        };
        ImGui::Combo("##loglevel", &mConsoleMinLevel, kLevelLabels, IM_ARRAYSIZE(kLevelLabels));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(searchW);
        ImGui::InputTextWithHint("##search", "search...", mConsoleSearchBuf,
                                 IM_ARRAYSIZE(mConsoleSearchBuf));
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear"))
        {
            std::lock_guard<std::mutex> guard(mLogMutex);
            mLogEntries.clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto", &mConsoleAutoScroll);
        ImGui::SameLine();
        ImGui::Checkbox("Time", &mConsoleShowTimestamp);
        ImGui::SameLine();
        if (ImGui::SmallButton("Quit"))
        {
            mAppHost.RequestExit();
        }
    }
    ImGui::Separator();

    // 日志条目区
    ImGui::BeginChild("##logentries", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    {
        std::lock_guard<std::mutex> guard(mLogMutex);
        const auto minLevel = static_cast<Orange::Engine::Log::Level>(mConsoleMinLevel);
        const std::string_view searchView{mConsoleSearchBuf};
        for (const auto& e : mLogEntries)
        {
            if (static_cast<int>(e.level) < static_cast<int>(minLevel))
            {
                continue;
            }
            if (!Orange::Editor::Util::ContainsCaseInsensitive(e.message, searchView))
            {
                continue;
            }
            ImVec4 color;
            const char* tag = "?";
            switch (e.level)
            {
                // 走 EditorTheme tokens；Warn / Error 直接拿，Trace 走 TextDisabled，
                // Debug 走 Accent，Info / default 走 TextPrimary。Critical 复用
                // GetAlertError（更红的语义在 Theme 里没单独 token）。
                case Orange::Engine::Log::Level::Trace:    color = Orange::Editor::Theme::Color::GetTextDisabled();  tag = "TRC"; break;
                case Orange::Engine::Log::Level::Debug:    color = Orange::Editor::Theme::Color::GetAccentPrimary(); tag = "DBG"; break;
                case Orange::Engine::Log::Level::Info:     color = Orange::Editor::Theme::Color::GetTextPrimary();   tag = "INF"; break;
                case Orange::Engine::Log::Level::Warn:     color = Orange::Editor::Theme::Color::GetAlertWarn();     tag = "WRN"; break;
                case Orange::Engine::Log::Level::Error:    color = Orange::Editor::Theme::Color::GetAlertError();    tag = "ERR"; break;
                case Orange::Engine::Log::Level::Critical: color = Orange::Editor::Theme::Color::GetAlertError();    tag = "CRT"; break;
                default:                                   color = Orange::Editor::Theme::Color::GetTextPrimary();   break;
            }
            // 时间戳列（可关）：dim 前缀 + SameLine 接彩色 [TAG] message。
            // HH:MM:SS 定宽，列天然对齐。
            if (mConsoleShowTimestamp && !e.timestamp.empty())
            {
                ImGui::TextDisabled("%s", e.timestamp.c_str());
                ImGui::SameLine();
            }
            ImGui::TextColored(color, "[%s] %s", tag, e.message.c_str());
        }
    }
    if (mConsoleAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
    {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    ImGui::End();
}

// Core::Log SetLogSink 注册的静态 callback —— userData 是 EditorRender
// Layer* 指针；callback 在任意线程触发，push 日志到 ring buffer，超过
// cap 时丢最早条目。所有写入都在 mLogMutex 保护下。
void EditorRenderLayer::LogSinkCallback(Orange::Engine::Log::Level level,
                                       std::string_view           message,
                                       void*                      userData)
{
    auto* pLayer = static_cast<EditorRenderLayer*>(userData);
    if (pLayer == nullptr) { return; }
    std::lock_guard<std::mutex> guard(pLayer->mLogMutex);
    if (pLayer->mLogEntries.size() >= kLogBufferCap)
    {
        pLayer->mLogEntries.pop_front();
    }
    pLayer->mLogEntries.push_back({level, std::string{message}, FormatWallClockNow()});
}

// v0.8 Settings 面板 —— gizmo 视觉常量集中编辑入口（消除 L13）。窗口浮动
// 在主 viewport 之外，可拖动 / 关闭。所有编辑直写 host.settings，
// 下一帧 gizmo 立即应用；持久化在 main.cpp 进程退出时统一写盘。
void EditorRenderLayer::DrawSettingsPanel()
{
    if (!ImGui::Begin("Settings##editor", &mShowSettingsPanel))
    {
        ImGui::End();
        return;
    }
    auto& s = mHost.settings;

    if (ImGui::CollapsingHeader("Gizmo", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // 参考系只读显示（gap §3 P0 Local/World）。X 键在 viewport 切换。
        ImGui::Text("Space: %s",
            mHost.gizmo.space == EditorGizmoState::Space::Local ? "Local" : "World");
        ImGui::SameLine();
        ImGui::TextDisabled("(viewport 内按 X 切换；作用 translate/rotate)");
        ImGui::SeparatorText("Line Width (px)");
        ImGui::DragFloat("Translate idle",      &s.gizmoLineWidthTranslateIdle,      0.1f, 0.5f, 12.0f);
        ImGui::DragFloat("Translate highlight", &s.gizmoLineWidthTranslateHighlight, 0.1f, 0.5f, 12.0f);
        ImGui::DragFloat("Rotate idle",         &s.gizmoLineWidthRotateIdle,         0.1f, 0.5f, 12.0f);
        ImGui::DragFloat("Rotate highlight",    &s.gizmoLineWidthRotateHighlight,    0.1f, 0.5f, 12.0f);
        ImGui::DragFloat("Scale idle",          &s.gizmoLineWidthScaleIdle,          0.1f, 0.5f, 12.0f);
        ImGui::DragFloat("Scale highlight",     &s.gizmoLineWidthScaleHighlight,     0.1f, 0.5f, 12.0f);

        ImGui::SeparatorText("Handle / Hit Test");
        ImGui::DragFloat("Handle screen length (px)", &s.gizmoHandleScreenLengthPx, 1.0f, 30.0f, 300.0f);
        ImGui::DragFloat("Hit threshold (px)",        &s.gizmoHitThresholdPx,       0.5f, 1.0f, 32.0f);

        ImGui::SeparatorText("Axis Colors");
        ImGui::ColorEdit4("X idle",      &s.gizmoColorXIdle.x);
        ImGui::ColorEdit4("X highlight", &s.gizmoColorXHighlight.x);
        ImGui::ColorEdit4("Y idle",      &s.gizmoColorYIdle.x);
        ImGui::ColorEdit4("Y highlight", &s.gizmoColorYHighlight.x);
        ImGui::ColorEdit4("Z idle",      &s.gizmoColorZIdle.x);
        ImGui::ColorEdit4("Z highlight", &s.gizmoColorZHighlight.x);

        if (ImGui::Button("Reset to defaults"))
        {
            s = EditorSettings{};
        }
    }

    if (ImGui::CollapsingHeader("Gizmo Snap", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Enable snap", &s.snapEnabled);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "拖动 gizmo 时量化到步长：translate=世界网格 / rotate=增量角 /\n"
                "scale=各轴比例（clamp ≥step）。默认关=零回归。");
        }
        ImGui::BeginDisabled(!s.snapEnabled);
        ImGui::DragFloat("Translate grid (m)", &s.snapTranslateStep,
                         0.05f, 0.01f, 100.0f, "%.2f");
        ImGui::DragFloat("Rotate step (deg)",  &s.snapRotateStepDeg,
                         1.0f, 1.0f, 180.0f, "%.0f");
        ImGui::DragFloat("Scale step",         &s.snapScaleStep,
                         0.01f, 0.01f, 10.0f, "%.2f");
        ImGui::EndDisabled();
    }

    if (ImGui::CollapsingHeader("Autosave", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Enable autosave", &s.autosaveEnabled);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Edit 态下场景有未保存改动时周期写 .autosave；\n"
                "崩溃 / 异常退出后下次启动可恢复。开关 live 生效。");
        }
        ImGui::BeginDisabled(!s.autosaveEnabled);
        ImGui::DragFloat("Interval (s)", &s.autosaveIntervalSeconds,
                         5.0f, 10.0f, 1800.0f, "%.0f");
        ImGui::DragFloat("Min between (s)", &s.autosaveMinIntervalSeconds,
                         1.0f, 0.0f, 600.0f, "%.0f");
        ImGui::EndDisabled();
        ImGui::TextDisabled("Enable / Interval 改动即时生效（改 Interval 会重置当前倒计时）。");
    }

    if (ImGui::CollapsingHeader("Keybindings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto& kb = mHost.keybindings;

        // 每条 binding 一行：label | 当前键名 | [Rebind] 按钮。点 Rebind 后
        // 等下一个键按下即写入；按 Esc 取消。
        auto drawBind = [&](const char* label, ImGuiKey* pKey, const char* slot)
        {
            ImGui::PushID(slot);
            ImGui::TextUnformatted(label);
            ImGui::SameLine(180.0f);
            ImGui::TextDisabled("[%s]", ImGui::GetKeyName(*pKey));
            ImGui::SameLine(280.0f);

            const bool waiting = (mRebindActive == slot);
            if (waiting)
            {
                ImGui::TextColored(Orange::Editor::Theme::Color::GetAlertWarn(),
                                   "press a key (Esc = cancel)");
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                {
                    mRebindActive.clear();
                }
                else
                {
                    // 扫所有 named key；第一个本帧 just-pressed 的写回。
                    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
                    {
                        const auto key = static_cast<ImGuiKey>(k);
                        if (key == ImGuiKey_Escape) { continue; }
                        if (ImGui::IsKeyPressed(key, false))
                        {
                            *pKey = key;
                            mRebindActive.clear();
                            break;
                        }
                    }
                }
            }
            else
            {
                if (ImGui::SmallButton("Rebind"))
                {
                    mRebindActive = slot;
                }
            }
            ImGui::PopID();
        };

        drawBind("Gizmo Translate",  &kb.gizmoTranslate, "gizmoTranslate");
        drawBind("Gizmo Rotate",     &kb.gizmoRotate,    "gizmoRotate");
        drawBind("Gizmo Scale",      &kb.gizmoScale,     "gizmoScale");
        drawBind("Rename Entity",    &kb.renameEntity,   "renameEntity");
        drawBind("Delete Entity",    &kb.deleteEntity,   "deleteEntity");
        drawBind("Frame Selected",   &kb.frameSelected,  "frameSelected");

        if (ImGui::Button("Reset keybindings to defaults"))
        {
            kb = EditorKeybindings{};
            mRebindActive.clear();
        }
    }
    ImGui::End();
}

// v0.9 Profiler 面板 —— 帧耗时柱状图 + sample bin 树（Performance tab）+
// Memory 计数（Memory tab）。数据源：Core::Profiler::Snapshot()（AppHost
// 主循环帧末 FinalizeFrame 写入）+ Core::Memory::Snapshot()（模块 opt-in
// 调 AddBytes/SubBytes）+ host 直接 introspection（World::Size /
// AssetRegistry::Size / Pipeline::TemplatePipelineCount 等）。
void EditorRenderLayer::DrawProfilerPanel(const Orange::Engine::FrameContext& frame)
{
    namespace Profiler = ::Orange::Engine::Core::Profiler;
    namespace Memory   = ::Orange::Engine::Core::Memory;

    if (!ImGui::Begin("Profiler##editor", &mShowProfilerPanel))
    {
        ImGui::End();
        return;
    }

    if (!ImGui::BeginTabBar("##profilertabs"))
    {
        ImGui::End();
        return;
    }

    // ======================== Performance tab ========================
    if (ImGui::BeginTabItem("Performance"))
    {

    // 1. 帧耗时柱状图 ----------------------------------------------------
    // 把本帧 delta 推入 ring buffer。ring 用 write-index + count 实现，避免
    // 每帧 std::deque pop/push 的分配；PlotLines 接受 stride / offset 直接
    // 绘 ring 起点 = oldest sample。
    const float deltaMs = frame.time.deltaSeconds * 1000.0f;
    mProfilerFrameMs[mProfilerFrameWriteIdx] = deltaMs;
    mProfilerFrameWriteIdx = (mProfilerFrameWriteIdx + 1) % kProfilerFrameRingCap;
    if (mProfilerFrameCount < kProfilerFrameRingCap)
    {
        ++mProfilerFrameCount;
    }

    ImGui::Text("Frame %llu  Δ=%.2f ms (%.1f FPS)",
                static_cast<unsigned long long>(frame.time.frameIndex),
                deltaMs,
                (deltaMs > 0.001f) ? (1000.0f / deltaMs) : 0.0f);

    // PlotLines values_offset = write_idx 当 ring 满了等价于 "oldest 在 buffer
    // 起点的逻辑视图"。scale_min/max 自动；用 50ms 给 plot 一个稳定 y 上限避
    // 免单帧尖刺把 plot 压扁。
    const float plotMax = 50.0f;
    ImGui::PlotLines("##frametime",
                     mProfilerFrameMs.data(),
                     static_cast<int>(mProfilerFrameCount),
                     static_cast<int>(mProfilerFrameWriteIdx),
                     nullptr,
                     0.0f, plotMax,
                     ImVec2(0.0f, 80.0f));

    ImGui::Separator();

    // 2. Sample bin 树形 -------------------------------------------------
    // Profiler::Snapshot 返回稳定顺序的 SampleBinSnapshot 数组（按 DeclareSampleBin
    // 调用次序）。按 parentName 关系递归展开为 tree。
    auto snapshot = Profiler::Snapshot();
    if (snapshot.empty())
    {
        ImGui::TextDisabled("(no sample bins declared)");
        ImGui::EndTabItem();
        ImGui::EndTabBar();
        ImGui::End();
        return;
    }

    if (ImGui::BeginTable("##profilerBins", 4,
                           ImGuiTableFlags_BordersInner | ImGuiTableFlags_RowBg |
                           ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupColumn("Name",        ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Inclusive",   ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Exclusive",   ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Calls",       ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        // 递归绘 bin：先画自己一行，再递归画 children。child 关系 O(N²)
        // 扫描；v0.9 bin 数十量级足够，未来 bin 数十万再上 child 索引。
        std::function<void(const char*)> drawSubtree = [&](const char* parentName) {
            for (const auto& bin : snapshot)
            {
                const bool isRoot = (bin.parentName == nullptr);
                const bool match  = (parentName == nullptr)
                    ? isRoot
                    : (!isRoot && std::string_view{bin.parentName} == std::string_view{parentName});
                if (!match) { continue; }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                // 数 children 决定 TreeNode 是否 leaf
                bool hasChildren = false;
                for (const auto& other : snapshot)
                {
                    if (other.parentName != nullptr &&
                        std::string_view{other.parentName} == std::string_view{bin.name})
                    {
                        hasChildren = true;
                        break;
                    }
                }
                const ImGuiTreeNodeFlags flags = hasChildren
                    ? ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth
                    : ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                      ImGuiTreeNodeFlags_SpanAvailWidth;
                const bool open = ImGui::TreeNodeEx(bin.name, flags);

                ImGui::TableNextColumn();
                ImGui::Text("%.3f", bin.inclusiveMs);
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", bin.exclusiveMs);
                ImGui::TableNextColumn();
                ImGui::Text("%u", bin.callCount);

                if (open && hasChildren)
                {
                    drawSubtree(bin.name);
                    ImGui::TreePop();
                }
            }
        };
        drawSubtree(nullptr);

        ImGui::EndTable();
    }

    ImGui::EndTabItem();
    }  // Performance tab

    // ======================== Memory tab ========================
    if (ImGui::BeginTabItem("Memory"))
    {
        // 1. Tracked categories（模块 opt-in Memory::AddBytes/SubBytes）。
        ImGui::TextDisabled("Tracked Categories (Core::Memory opt-in)");
        ImGui::Separator();
        auto memSnap = Memory::Snapshot();
        if (memSnap.empty())
        {
            ImGui::TextDisabled("(no categories registered; modules opt in via "
                                "Core::Memory::RegisterCategory + AddBytes/SubBytes)");
        }
        else if (ImGui::BeginTable("##memcats", 4,
                                    ImGuiTableFlags_BordersInner | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Category",   ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Current KB", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Peak KB",    ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Alloc/Free", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableHeadersRow();
            for (const auto& c : memSnap)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(c.name);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", static_cast<double>(c.bytesCurrent) / 1024.0);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", static_cast<double>(c.bytesHighWater) / 1024.0);
                ImGui::TableNextColumn();
                ImGui::Text("%llu / %llu",
                            static_cast<unsigned long long>(c.allocCount),
                            static_cast<unsigned long long>(c.freeCount));
            }
            ImGui::EndTable();
        }

        // 2. Module Counts —— 走现有 introspection API。这是非 byte-accurate 但
        //    立即可用的"每模块状态"概览（v0.9 c6 deliverable）。
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::TextDisabled("Module Counts (logical introspection)");
        ImGui::Separator();
        if (ImGui::BeginTable("##modulecounts", 2,
                               ImGuiTableFlags_BordersInner | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Module / Metric", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Count",           ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableHeadersRow();

            auto row = [](const char* label, std::size_t value) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(label);
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(value));
            };

            if (mHost.scene.pWorld != nullptr)
            {
                row("Scene::Entity count", mHost.scene.pWorld->Size());
            }
            if (mHost.assets.pAssets != nullptr)
            {
                row("Asset::Registry size (all kinds)", mHost.assets.pAssets->Size());
            }
            if (mpScenePipeline != nullptr)
            {
                row("Render::Pipeline cache (templates)",
                    mpScenePipeline->TemplatePipelineCount());
                row("Render::Bloom mip count",
                    mpScenePipeline->BloomMipCount());
            }
            row("Profiler::Sample bin count", Profiler::BinCount());
            row("Memory::Category count",     Memory::CategoryCount());

            ImGui::EndTable();
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::TextDisabled("Note: byte-accurate per-allocator tracking is future work\n"
                            "(heap-level hook is non-trivial; see ADR-003 Consequences)");

        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
    ImGui::End();
}
