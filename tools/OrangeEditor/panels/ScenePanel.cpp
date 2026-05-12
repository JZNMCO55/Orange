// Scene 面板（off-screen pipeline + sampler + descriptor set + ImGui::Image）。
// 与 EditorRenderLayer.cpp 主 TU 共享同一类声明 EditorRenderLayer。

#include "../EditorRenderLayer.h"

#include "../EditorCameraControl.h"

#include <orange/engine/scene/World.h>
#include <orange/renderer/VulkanInterop.h>
#include <orange/rhi/RHITexture.h>

#include <backends/imgui_impl_vulkan.h>

#include <cstdio>

void EditorRenderLayer::DrawScenePanel()
{
    ImGui::Begin("Scene");

    // S3：相机输入捕获 + 应用到 World Camera 组件。
    const ImVec2 region = ImGui::GetContentRegionAvail();
    const float  aspect = (region.y > 0.0f) ? (region.x / region.y) : 1.0f;
    UpdateEditorCameraFromInput(mState.camera);
    ApplyEditorCameraToWorld(mState, aspect);

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
    if (EnsureScenePipeline(panelW, panelH) && mState.scene.pWorld != nullptr) {
        // Pipeline::RenderOffscreen 内部 WaitIdle —— 本帧返回时 GPU 已
        // 空，之后 RemoveTexture(旧 descriptor) + AddTexture(新) 才安全。
        mpScenePipeline->Render(*mState.scene.pWorld);
        RebindSceneDescriptorSetIfNeeded();
        if (mSceneDescSet != VK_NULL_HANDLE) {
            ImGui::Image(reinterpret_cast<ImTextureID>(mSceneDescSet),
                         ImVec2(static_cast<float>(panelW),
                                static_cast<float>(panelH)));
            drewImage = true;
        }
    }

    if (!drewImage) {
        ImGui::TextDisabled("scene viewport 未就绪 —— 面板太小或 Pipeline 初始化失败");
        const auto& ec = mState.camera;
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
    if (mState.assets.pAssets == nullptr)         { return false; }
    if (mSceneSampler == VK_NULL_HANDLE)   { return false; }

    // lazy init
    if (mpScenePipeline == nullptr) {
        mpScenePipeline = std::make_unique<Orange::Engine::Render::Pipeline>();
        auto r = mpScenePipeline->InitializeOffscreen(
            mRenderDevice, *mState.assets.pAssets, width, height);
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
