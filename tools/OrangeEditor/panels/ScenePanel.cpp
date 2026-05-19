// Scene 面板（off-screen pipeline + sampler + descriptor set + ImGui::Image）。
// 与 EditorRenderLayer.cpp 主 TU 共享同一类声明 EditorRenderLayer。

#include "../EditorRenderLayer.h"

#include "../EditorCameraControl.h"
#include "../EditorPicking.h"
#include "../EditorRotateGizmo.h"
#include "../EditorScaleGizmo.h"
#include "../EditorTranslateGizmo.h"
#include "../plugin/GizmoContext.h"
#include "../plugin/IEditorGizmoPlugin.h"
#include "../schema/ComponentSchema.h"
#include "../schema/ComponentSchemaRegistry.h"

#include <orange/engine/render/BuiltinPostProcessChain.h>
#include <orange/engine/render/DebugDrawScene.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>
#include <orange/renderer/VulkanInterop.h>
#include <orange/rhi/RHITexture.h>

#include <backends/imgui_impl_vulkan.h>

#include <glm/vec2.hpp>

#include <algorithm>
#include <cstdio>
#include <initializer_list>

// viewport 工具栏 grid / sky 开关持久状态。本期只用 file-static（不进 Editor
// Settings 持久化），与 ScenePanel.cpp 的 toolbar 局部 UI 状态同节奏；后续
// v0.8 EditorSettings 整骨 milestone 若要在重启间保留，再迁移到 settings。
// Grid 默认开（地面参考线对编辑器主战场最有用）；Sky 默认开（cubemap 未烘焙
// 时 Pipeline 内部 fallback 到原 clear color，开关也不会让 viewport 黑屏）。
// Debug Draw 默认关 —— 是 opt-in 的 v0.9 调试工具，普通编辑器用户大多数
// 时间不需要，开启后才会画原点坐标轴 + selected entity 位置 sphere。
static bool sViewportGridEnabled      = true;
static bool sViewportSkyEnabled       = true;
static bool sViewportDebugDrawEnabled = false;

void EditorRenderLayer::DrawScenePanel()
{
    ImGui::Begin("Scene");

    // ---- v0.4 c5：Viewport 工具栏 ---------------------------------------
    // 参 Cocos Creator 3.6.0 截图的"Scene 面板顶部工具栏"布局（editor-roadmap
    // .md D5 节）。本期落 3 个能消费的功能 + 3 个占位（依赖引擎能力，登记
    // engine-known-gaps 后独立 session 处理）。
    //
    //   * **Gizmos**（checkbox）—— 全部 gizmo overlay 总开关；写
    //     `host.gizmo.visible`，c2/c3 内置 Translate/Rotate/Scale 早退 +
    //     c4 plugin dispatch gate 都消费本字段
    //   * **View Mode**（placeholder disabled）—— Persp / 2D Lock 切换；
    //     依赖编辑器相机引入"锁定 XY 平面 + 关闭 elevation"模式（当前
    //     EditorCameraControl 只支持自由轨道）
    //   * **Shading**（placeholder disabled）—— Shaded / Wireframe；依赖
    //     OrangeRender 提供 wireframe pass（按 engine-known-gaps 工作流
    //     推到 OrangeRender incoming_feature 单独 session 处理）
    //   * **Camera Mode**（placeholder disabled）—— Design Resolution 锁定；
    //     依赖引擎 CameraDesc + viewport resolution lock 概念
    //
    // disabled 项的 tooltip 用 `ImGuiHoveredFlags_AllowWhenDisabled` 让 hover
    // 在 disabled 状态下仍弹出，向用户解释"为什么不可用 + 哪里登记了"。
    ImGui::Checkbox("Gizmos", &mHost.gizmo.visible);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Viewport gizmo overlay 总开关（Translate / Rotate / Scale\n"
                          "内置 gizmo + Light / ParticleEmitter / Camera frustum plugin）");
    }

    ImGui::SameLine();
    ImGui::Checkbox("Grid", &sViewportGridEnabled);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("地面参考网格（Y=0 平面，每 1 m 细线 + 每 10 m 粗线）\n"
                          "走 fullscreen-quad + PristineGrid + depth test，被几何遮挡");
    }

    ImGui::SameLine();
    ImGui::Checkbox("Sky", &sViewportSkyEnabled);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("天空盒背景（采 EnvironmentComponent.cubemap）\n"
                          "未挂 EnvironmentComponent / cubemap 未烘焙 → 显示深蓝灰 fallback");
    }

    ImGui::SameLine();
    ImGui::Checkbox("Debug Draw", &sViewportDebugDrawEnabled);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("v0.9 调试几何 overlay：原点坐标轴 + selected entity 位置 sphere\n"
                          "走 OrangeRender DebugDraw immediate-mode（line / triangle）\n"
                          "always-on-top，不被场景几何遮挡");
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Combo 列宽从 CalcTextSize 派生 —— v0.4 期硬编码 70 / 80 像素在
    // 1680×1120 × 150% scale 上会被字体撑出 combo 控件框（Segoe UI 24px 下
    // "Shaded" 文本宽 ~58px，加 framePad + arrow 按钮 ~24px 远超 80px）。
    // FramePadding / arrow 按钮宽由 main.cpp ScaleAllSizes(dpiScale) 同步缩放。
    // 用 (std::max)(...) 圆括号包装绕开 windows.h max 宏污染（本 TU 通过
    // imgui_impl_vulkan / VulkanInterop 间接拉 windows.h，没 #define NOMINMAX）。
    auto comboItemWidth = [&](std::initializer_list<const char*> items) {
        const ImGuiStyle& s = ImGui::GetStyle();
        float maxW = 0.0f;
        for (const char* it : items) {
            const float w = ImGui::CalcTextSize(it).x;
            if (w > maxW) { maxW = w; }
        }
        // arrow button 宽 ≈ FrameHeight；framePadding 左右各一份。
        return maxW + s.FramePadding.x * 2.0f + ImGui::GetFrameHeight();
    };

    {
        ImGui::BeginDisabled();
        const char* kViewModes[] = {"Persp"};
        int curView = 0;
        ImGui::SetNextItemWidth(comboItemWidth({"Persp", "2D Lock"}));
        ImGui::Combo("##ViewMode", &curView, kViewModes, IM_ARRAYSIZE(kViewModes));
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("View Mode（Persp / 2D Lock）未实现\n"
                              "依赖 EditorCameraControl 引入 2D 锁定模式");
        }
    }

    ImGui::SameLine();
    {
        ImGui::BeginDisabled();
        const char* kShadingModes[] = {"Shaded"};
        int curShading = 0;
        ImGui::SetNextItemWidth(comboItemWidth({"Shaded", "Wireframe", "Shaded+Wireframe"}));
        ImGui::Combo("##Shading", &curShading, kShadingModes, IM_ARRAYSIZE(kShadingModes));
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("Shading (Shaded / Wireframe / Shaded+Wireframe) 未实现\n"
                              "依赖 OrangeRender wireframe pass —— 按 engine-known-gaps\n"
                              "工作流推到 OrangeRender incoming_feature 单独 session");
        }
    }

    ImGui::SameLine();
    {
        ImGui::BeginDisabled();
        // v0.6.5 c4：SmallButton → Button，让 disabled 占位与左侧 Persp /
        // Shaded combo 等高（SmallButton 无 FramePadding.y 比 Combo 矮一档，
        // 视觉上突兀）。
        ImGui::Button("Camera Mode");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("Camera Mode (Design Resolution 锁定) 未实现\n"
                              "依赖引擎 CameraDesc + viewport resolution lock —— 见\n"
                              "engine-known-gaps GAP-2026-05-15-camera-editor-vs-runtime-separation");
        }
    }

    ImGui::Separator();

    // S3：相机输入捕获 + push 编辑器轨道相机给 Pipeline override（GAP-2026-
    // 05-15 落地后路径——不再 mutate World ECS Camera，CameraFrustumGizmo
    // 等读 ECS Camera 的下游路径拿到的是游戏侧原始数据）。
    const ImVec2 region = ImGui::GetContentRegionAvail();
    const float  aspect = (region.y > 0.0f) ? (region.x / region.y) : 1.0f;
    UpdateEditorCameraFromInput(mHost);
    mEditorCameraOverride = BuildEditorCamera(mHost.camera, aspect);

    // S4：把 Scene 面板接到 Pipeline::InitializeOffscreen 上 ——
    // viewport-sized off-screen RT 渲染场景 → Interop::GetVulkanImageView
    // → ImGui_ImplVulkan_AddTexture → ImGui::Image。
    //
    // 尺寸退化（面板折叠 / 极小 < 1px）走占位文案分支：Pipeline 既不
    // 占资源也不渲染，避免反复 Resize 抖动。常规阈值 1px 已足够，编
    // 辑器实际可用尺寸至少几十像素。
    const std::uint32_t panelW =
        (region.x > 1.0f) ? static_cast<std::uint32_t>(region.x) : 0u;
    const std::uint32_t panelH =
        (region.y > 1.0f) ? static_cast<std::uint32_t>(region.y) : 0u;

    bool drewImage = false;
    if (EnsureScenePipeline(panelW, panelH) && mHost.scene.pWorld != nullptr) {
        // v0.6 c4：每帧 wire partition 给 Pipeline —— Pipeline 内部按
        // partition.IsEntityVisible(world, e) 过滤 drawable + shadow caster；
        // hide 的 layer 立即从 viewport 消失，无需 mutate ECS。
        // pointer 非拥有，partition 与 EditorSceneContext 同生命周期，
        // 始终 valid，无需 null 检查。
        mpScenePipeline->SetWorldPartition(&mHost.scene.partition);
        mpScenePipeline->SetEditorCameraOverride(&mEditorCameraOverride);
        // viewport toolbar toggle → Pipeline 状态：每帧 push（开销极小，
        // 避免在 toggle 改变时维护额外 dirty 标记）。
        mpScenePipeline->SetEditorGridEnabled(sViewportGridEnabled);
        mpScenePipeline->SetSkyEnabled(sViewportSkyEnabled);

        // v0.9 c2 DebugDraw 接通：toggle → wrap.SetEnabled；启用时本帧提交
        // origin 坐标轴 + selected entity 位置 wireframe sphere（黄色，
        // 半径 0.5），证明 editor → wrap → OR DebugDraw → HDR pass 端到端。
        if (auto* dbg = mpScenePipeline->GetDebugDrawScene())
        {
            dbg->SetEnabled(sViewportDebugDrawEnabled);
            if (sViewportDebugDrawEnabled)
            {
                // 原点 3 轴坐标（X 红 / Y 绿 / Z 蓝，长度 1.5）—— ABGR
                // packed：低 8 位 R，高 8 位 A。
                constexpr std::uint32_t kRed   = 0xFF0000FFu;
                constexpr std::uint32_t kGreen = 0xFF00FF00u;
                constexpr std::uint32_t kBlue  = 0xFFFF0000u;
                constexpr std::uint32_t kHi    = 0xFF00FFFFu;  // 选中实体高亮黄
                dbg->AddLine(glm::vec3(0.0f), glm::vec3(1.5f, 0.0f, 0.0f), kRed);
                dbg->AddLine(glm::vec3(0.0f), glm::vec3(0.0f, 1.5f, 0.0f), kGreen);
                dbg->AddLine(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.5f), kBlue);

                // 选中 entities 位置 wireframe sphere —— primary + additional
                // 都画，让多选可视化。
                auto drawSelected = [&](Orange::Engine::Entity e) {
                    if (!e.IsValid()) { return; }
                    auto* xf = mHost.scene.pWorld->GetComponent<
                        Orange::Engine::Scene::TransformComponent>(e);
                    if (xf == nullptr) { return; }
                    dbg->AddSphere(xf->position, 0.5f, kHi, 16);
                };
                drawSelected(mHost.selection.selectedEntity);
                for (const auto& e : mHost.selection.additionalSelectedEntities)
                {
                    drawSelected(e);
                }
            }
        }

        // Pipeline::RenderOffscreen 内部 WaitIdle —— 本帧返回时 GPU 已
        // 空，之后 RemoveTexture(旧 descriptor) + AddTexture(新) 才安全。
        mpScenePipeline->Render(*mHost.scene.pWorld);
        RebindSceneDescriptorSetIfNeeded();
        if (mSceneDescSet != VK_NULL_HANDLE) {
            ImGui::Image(reinterpret_cast<ImTextureID>(mSceneDescSet),
                         ImVec2(static_cast<float>(panelW),
                                static_cast<float>(panelH)));
            drewImage = true;

            const ImVec2 itemMin  = ImGui::GetItemRectMin();
            const glm::vec2 imageOrigin(itemMin.x, itemMin.y);
            const glm::vec2 imageSize(static_cast<float>(panelW),
                                      static_cast<float>(panelH));

            // viewport gizmo —— v0.4 c2 translate；c3 起 W/E/R 切换 +
            // rotate / scale。必须在 ImGui::Image 之后、picking 触发之前
            // 调：让 gizmo 先消费 LMB / hover，picking 仅在 gizmo 没接管
            // 时触发，避免"拖完 gizmo 松手又触发 picking"。
            //
            // 模式切换：拖动期间不切换（保 mid-drag 一致性，按 v0.2.5 c13
            // 的"BeginGroup 期间不交叉"惯例对偶）。键盘 W/E/R 不依赖 viewport
            // hover（与 Lumix / Unity 同款全局快捷键约定；但要求 ImGui 无
            // 文本输入 active，否则会拦截字母键）。
            if (!mHost.gizmo.IsDragging() && !ImGui::IsAnyItemActive())
            {
                // v0.8 keybinding：从 EditorKeybindings 读绑定的 key（默认
                // W/E/R，可在 Settings 面板内 rebind）。
                const auto& kb = mHost.keybindings;
                if (ImGui::IsKeyPressed(kb.gizmoTranslate, false))
                {
                    mHost.gizmo.mode = EditorGizmoState::Mode::Translate;
                }
                else if (ImGui::IsKeyPressed(kb.gizmoRotate, false))
                {
                    mHost.gizmo.mode = EditorGizmoState::Mode::Rotate;
                }
                else if (ImGui::IsKeyPressed(kb.gizmoScale, false))
                {
                    mHost.gizmo.mode = EditorGizmoState::Mode::Scale;
                }
            }

            bool gizmoActive = false;
            switch (mHost.gizmo.mode)
            {
                case EditorGizmoState::Mode::Translate:
                    gizmoActive = DrawAndHandleTranslateGizmo(
                        mHost, imageOrigin, imageSize, aspect);
                    break;
                case EditorGizmoState::Mode::Rotate:
                    gizmoActive = DrawAndHandleRotateGizmo(
                        mHost, imageOrigin, imageSize, aspect);
                    break;
                case EditorGizmoState::Mode::Scale:
                    gizmoActive = DrawAndHandleScaleGizmo(
                        mHost, imageOrigin, imageSize, aspect);
                    break;
            }

            // ---- c4：IEditorGizmoPlugin 调度（v0.2.5 c12 抽象首批真实消费）
            //
            // Edit Mode 且选中实体有效时遍历 schema 注册表，对每个匹配
            // (plugin.CanHandle && schema.has(world, entity)) 的 (plugin,
            // schema) 对调 plugin.Draw。所有返回 true 的 plugin 全画（**不**
            // 互斥；多 plugin 可同时叠加 overlay）。
            //
            // Plugin Draw 是纯装饰 overlay（c4 设计：DirectionalLight 方向
            // 箭头 / ParticleEmitter spawn box + velocity 向量），不参与
            // gizmoActive 判定——Inspector 仍是字段编辑唯一入口，plugin
            // 不接管 LMB / picking 不被 gate。
            //
            // 调用约定见 plugin/IEditorGizmoPlugin.h 头注释"调用约定"段第 1
            // 条："对 selected entity 遍历 host.gizmoPlugins 调 CanHandle，
            // 对所有返回 true 的 plugin 依次调 Draw"。
            if (mHost.gizmo.visible
                && mHost.scene.playState == PlayState::Edit
                && mHost.selection.selectedEntity.IsValid()
                && !mHost.gizmoPlugins.empty())
            {
                const auto      cam      = BuildEditorCamera(mHost.camera, aspect);
                const glm::mat4 viewProj = cam.projection * cam.view;

                Orange::Editor::Plugin::GizmoContext ctx{};
                ctx.viewProj    = viewProj;
                ctx.imageOrigin = imageOrigin;
                ctx.imageSize   = imageSize;
                ctx.drawList    = ImGui::GetWindowDrawList();

                auto& reg = Orange::Editor::Schema::ComponentSchemaRegistry::Instance();
                for (auto& pPlugin : mHost.gizmoPlugins)
                {
                    if (pPlugin == nullptr) { continue; }
                    for (const auto& schema : reg.All())
                    {
                        if (!pPlugin->CanHandle(schema)) { continue; }
                        if (schema.has == nullptr || schema.get == nullptr) { continue; }
                        if (!schema.has(*mHost.scene.pWorld,
                                        mHost.selection.selectedEntity))
                        {
                            continue;
                        }
                        void* component = schema.get(*mHost.scene.pWorld,
                                                     mHost.selection.selectedEntity);
                        if (component == nullptr) { continue; }
                        pPlugin->Draw(mHost, mHost.selection.selectedEntity,
                                      schema, component, ctx);
                    }
                }
            }

            // viewport picking —— LMB 释放且累积 drag 距离 < 阈值 → 视为
            // 单击（区分于轨道相机拖动）。屏幕坐标 → image-local → NDC
            // ([-1,1]) → world ray → ECS hit-test → 命中实体写
            // EditorSelection.selectedEntity。Entity Tree 走既有 selection
            // 联动机制自动高亮，无需双向写。
            //
            // 阈值 4px：Cocos / Unreal 内典型 click 容差。drag 大于阈值的
            // 释放视为相机轨道旋转结束，不触发 picking。
            //
            // 命中失败（点空白）= Entity::Invalid → 清当前选中，与
            // Cocos / Unity / Unreal 工业惯例一致。
            //
            // gizmoActive gate：本帧 gizmo 处理了 LMB（hover handle 或正在
            // 拖动）→ 跳过 picking。否则 gizmo 拖动结束时的 LMB-release 会
            // 同时触发 picking，把选中实体改成 gizmo 下方的物体，破坏 UX。
            if (!gizmoActive
                && ImGui::IsItemHovered()
                && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                const ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
                constexpr float kClickThresholdPx = 4.0f;
                if (drag.x * drag.x + drag.y * drag.y
                        <= kClickThresholdPx * kClickThresholdPx)
                {
                    const ImVec2 mousePos = ImGui::GetMousePos();
                    const float lx = mousePos.x - itemMin.x;
                    const float ly = mousePos.y - itemMin.y;
                    // 屏幕坐标 → NDC：Vulkan NDC y-down 与 ImGui 屏幕 y-down
                    // 同向，不再额外翻转
                    const float ndcX = (lx / static_cast<float>(panelW)) * 2.0f - 1.0f;
                    const float ndcY = (ly / static_cast<float>(panelH)) * 2.0f - 1.0f;
                    const Orange::Engine::Entity picked =
                        PickEntityAt(mHost, glm::vec2(ndcX, ndcY), aspect);
                    mHost.selection.selectedEntity = picked;
                    // 切实体 → Euler 编辑缓存失效（与 Quat case 行为一致）
                    mHost.selection.transformEulerCacheEntity =
                        Orange::Engine::Entity::Invalid();
                    // B3 修：选了 valid 实体 → 清 asset 选中，让 Inspector 从
                    // Material 子模式切回实体模式（互斥选择，匹配 Cocos/Unity
                    // 惯例：viewport 点选总是把焦点拉回实体 Inspector）。
                    if (picked.IsValid())
                    {
                        mHost.assets.selectedAssetPath.clear();
                    }
                }
            }
        }
    }

    if (!drewImage) {
        ImGui::TextDisabled("scene viewport 未就绪 —— 面板太小或 Pipeline 初始化失败");
        const auto& ec = mHost.camera;
        ImGui::Text("viewport %.0fx%.0f  aspect=%.2f", region.x, region.y, aspect);
        ImGui::Text("camera pivot=(%.2f, %.2f, %.2f)  az=%.2f  el=%.2f  r=%.2f",
                    ec.pivot.x, ec.pivot.y, ec.pivot.z,
                    ec.azimuth, ec.elevation, ec.radius);
        ImGui::TextDisabled("LMB 拖动 旋转  滚轮 缩放");
    }

    ImGui::End();
}

// 创建 Scene 面板 ImGui::Image 用的 VkSampler。失败仅 log，DrawScenePanel
// 后续会让该面板退化为占位文案。
void EditorRenderLayer::CreateScenePanelSampler()
{
    auto* pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        Orange::Renderer::Interop::GetVulkanGetInstanceProcAddr());
    if (pfnGetInstanceProcAddr == nullptr) { return; }
    const auto handles = Orange::Renderer::Interop::GetVulkanDeviceHandles(mRenderDevice);
    if (handles.vkInstance == nullptr || handles.vkDevice == nullptr) { return; }
    auto pfnGetDeviceProcAddrFn = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        pfnGetInstanceProcAddr(static_cast<VkInstance>(handles.vkInstance), "vkGetDeviceProcAddr"));
    if (pfnGetDeviceProcAddrFn == nullptr) { return; }
    auto pfnCreate = reinterpret_cast<PFN_vkCreateSampler>(
        pfnGetDeviceProcAddrFn(static_cast<VkDevice>(handles.vkDevice), "vkCreateSampler"));
    if (pfnCreate == nullptr) { return; }

    VkSamplerCreateInfo s{};
    s.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    s.magFilter    = VK_FILTER_LINEAR;
    s.minFilter    = VK_FILTER_LINEAR;
    s.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.minLod       = 0.0f;
    s.maxLod       = 0.0f;
    if (pfnCreate(static_cast<VkDevice>(handles.vkDevice), &s, nullptr, &mSceneSampler) != VK_SUCCESS) {
        mSceneSampler = VK_NULL_HANDLE;
        std::fprintf(stderr, "[OrangeEditor] vkCreateSampler (scene panel) 失败\n");
    }
}

void EditorRenderLayer::DestroyScenePanelSampler()
{
    if (mSceneSampler == VK_NULL_HANDLE) { return; }
    auto* pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        Orange::Renderer::Interop::GetVulkanGetInstanceProcAddr());
    if (pfnGetInstanceProcAddr == nullptr) { return; }
    const auto handles = Orange::Renderer::Interop::GetVulkanDeviceHandles(mRenderDevice);
    if (handles.vkDevice == nullptr) { return; }
    auto pfnGetDeviceProcAddrFn = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        pfnGetInstanceProcAddr(static_cast<VkInstance>(handles.vkInstance), "vkGetDeviceProcAddr"));
    if (pfnGetDeviceProcAddrFn == nullptr) { return; }
    auto pfnDestroy = reinterpret_cast<PFN_vkDestroySampler>(
        pfnGetDeviceProcAddrFn(static_cast<VkDevice>(handles.vkDevice), "vkDestroySampler"));
    if (pfnDestroy != nullptr) {
        pfnDestroy(static_cast<VkDevice>(handles.vkDevice), mSceneSampler, nullptr);
    }
    mSceneSampler = VK_NULL_HANDLE;
}

// 按当前 Scene 面板的 content region 尺寸初始化 / 重建 Pipeline 的
// off-screen RT。返回 true 时 mpScenePipeline 可用、mSceneDescSet 已绑
// 当帧 viewportColor 的 VkImageView。
//
// 行为：
//   * 首次调用且 w/h > 0：lazy 创建 Pipeline + InitializeOffscreen + 走
//     一次 Render + AddTexture → 拿到 descriptor set；
//   * 后续帧 w/h 与上次相同：no-op，复用 mSceneDescSet；
//   * 尺寸变化：ResizeOffscreen + 下一次 Pipeline.Render → view 句柄轮
//     换，先 RemoveTexture(旧) → AddTexture(新)；
//   * 任一步失败：把 mScenePipelineFailed 置 true，后续 DrawScenePanel
//     不再 retry（避免每帧重复输出 error log），UI 退化到占位文案。
bool EditorRenderLayer::EnsureScenePipeline(std::uint32_t width, std::uint32_t height)
{
    if (mScenePipelineFailed)              { return false; }
    if (width == 0 || height == 0)         { return false; }
    if (mHost.assets.pAssets == nullptr)         { return false; }
    if (mSceneSampler == VK_NULL_HANDLE)   { return false; }

    // lazy init
    if (mpScenePipeline == nullptr) {
        mpScenePipeline = std::make_unique<Orange::Engine::Render::Pipeline>();
        auto r = mpScenePipeline->InitializeOffscreen(
            mRenderDevice, *mHost.assets.pAssets, width, height);
        if (r.IsErr()) {
            std::fprintf(stderr,
                         "[OrangeEditor] Pipeline::InitializeOffscreen 失败 (code=%u) —— "
                         "Scene 视口退化为占位文案\n",
                         static_cast<unsigned>(r.Error()));
            mpScenePipeline.reset();
            mScenePipelineFailed = true;
            return false;
        }
        // 默认 PostProcessChain（Bloom + Tonemap + LUT）—— sample 14_pbr_ibl /
        // 13_pbr_direct 同款。让 HDR pipeline 走完整路径：emissive HDR > 1
        // 像素经 bloom 柔化扩散、PBR 物体经 ACES tonemap 曲线提亮。漏接时
        // 视觉症状：emissive 物体硬边 clamp 像染色周围像素 + PBR cube 偏暗
        // （没 tonemap 曲线把 linear 0.3-0.4 提到 ~0.5 显示）。
        mpScenePostProcessChain = std::make_unique<
            Orange::Engine::Render::PostProcessChain>(
                Orange::Engine::Render::BuiltinPostProcessChain::CreateDefault());
        mpScenePipeline->SetPostProcessChain(mpScenePostProcessChain.get());
        mScenePanelWidth  = width;
        mScenePanelHeight = height;
    }
    else if (width != mScenePanelWidth || height != mScenePanelHeight) {
        mpScenePipeline->ResizeOffscreen(width, height);
        mScenePanelWidth  = width;
        mScenePanelHeight = height;
        // 旧 viewportColor view 在下一次 Pipeline.Render 内重建后失效，
        // descriptor set 也要相应失效；标记下来，等本帧 Render 后重绑。
        mSceneDescSetDirty = true;
    }

    return true;
}

// 在 EnsureScenePipeline + Pipeline.Render 之后调，确保 mSceneDescSet 指
// 向最新 viewportColor view。首帧或 resize 后会经 RemoveTexture(旧) +
// AddTexture(新) 路径轮换；其余情况复用。
void EditorRenderLayer::RebindSceneDescriptorSetIfNeeded()
{
    if (mpScenePipeline == nullptr) { return; }
    if (mSceneSampler == VK_NULL_HANDLE) { return; }

    const auto* tex = mpScenePipeline->GetOffscreenColor();
    if (tex == nullptr) { return; }

    if (mSceneDescSet != VK_NULL_HANDLE && !mSceneDescSetDirty) {
        return;  // 复用
    }

    // 旧 set 释放 —— Pipeline.RenderOffscreen 末尾的 WaitIdle 已经把上
    // 一帧 ImGui 采样旧 view 的 GPU 工作排空，free 安全。
    if (mSceneDescSet != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(mSceneDescSet);
        mSceneDescSet = VK_NULL_HANDLE;
    }

    auto* rawView = Orange::Renderer::Interop::GetVulkanImageView(*tex);
    if (rawView == nullptr) { return; }
    mSceneDescSet = ImGui_ImplVulkan_AddTexture(
        mSceneSampler,
        static_cast<VkImageView>(rawView),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    mSceneDescSetDirty = false;
}
