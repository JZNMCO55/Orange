// Scene 面板（off-screen pipeline + sampler + descriptor set + ImGui::Image）。
// 与 EditorRenderLayer.cpp 主 TU 共享同一类声明 EditorRenderLayer。

#include "../EditorRenderLayer.h"

#include "../EditorCameraControl.h"
#include "../EditorPicking.h"
#include "../EditorRotateGizmo.h"
#include "../EditorScaleGizmo.h"
#include "../EditorTranslateGizmo.h"

#include <orange/engine/scene/World.h>
#include <orange/renderer/VulkanInterop.h>
#include <orange/rhi/RHITexture.h>

#include <backends/imgui_impl_vulkan.h>

#include <glm/vec2.hpp>

#include <cstdio>

void EditorRenderLayer::DrawScenePanel()
{
    ImGui::Begin("Scene");

    // S3：相机输入捕获 + 应用到 World Camera 组件。
    const ImVec2 region = ImGui::GetContentRegionAvail();
    const float  aspect = (region.y > 0.0f) ? (region.x / region.y) : 1.0f;
    UpdateEditorCameraFromInput(mHost.camera);
    ApplyEditorCameraToWorld(mHost, aspect);

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
                if (ImGui::IsKeyPressed(ImGuiKey_W, false))
                {
                    mHost.gizmo.mode = EditorGizmoState::Mode::Translate;
                }
                else if (ImGui::IsKeyPressed(ImGuiKey_E, false))
                {
                    mHost.gizmo.mode = EditorGizmoState::Mode::Rotate;
                }
                else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
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
