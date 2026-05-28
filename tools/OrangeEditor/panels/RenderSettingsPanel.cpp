// Render Settings 面板 —— Pipeline 全局渲染配置编辑入口（v1.3.1，
// GAP-2026-05-28-editor-render-settings-panel）。
//
// 现期范围：ShadowConfig 全字段
//   * Cascaded Shadow Maps（cascadeCount 1~4 / debugCascadeTint）
//   * PCSS（pcssLightSize）
//   * PCF（pcfKernelRadius 0/1/2）
//   * 阴影贴图分辨率（mapResolution 1024/2048/4096 下拉）
//   * Bias（depthBias / normalBias）
//
// 编辑直写 EditorRenderLayer::mShadowConfig；DrawScenePanel 每帧把当前
// mShadowConfig push 到 mpScenePipeline.SetShadowConfig，因此编辑控件之后
// 下一帧 viewport 立即可见效果。
//
// PostProcessComponent 覆盖说明：Pipeline 内部
// `GatherShadowParamsFromComponents` 会在场景存在 PostProcessComponent 时用
// 组件的 `pcssLightSize` / `shadowMapResolution` 字段覆盖 mShadowConfig 对
// 应字段（src/render/Pipeline.cpp:2117-2123），因此当场景内有组件时本面板
// 调这两个值不会生效——tooltip 内显式标注。其余字段（cascadeCount /
// debugCascadeTint / pcfKernelRadius / depthBias / normalBias）不被组件覆盖，
// 本面板是它们的唯一编辑入口。
//
// 设计参考：
//   * Unity Project Settings → Quality → Shadows（Cascade Count 1/2/4 下拉 +
//     Cascade Splits 比例 + Shadow Distance + Shadow Resolution 全局档）
//   * Godot Project Settings → Rendering → Lights and Shadows
//   * vendor/LumixEngine 没有对应面板（Lumix shadow config 走 component），
//     架构差异：Orange 是 per-pipeline ShadowConfig，Lumix 走 per-light prop

#include "../EditorRenderLayer.h"

#include <imgui.h>

void EditorRenderLayer::DrawRenderSettingsPanel()
{
    if (!ImGui::Begin("Render Settings##editor", &mShowRenderSettingsPanel))
    {
        ImGui::End();
        return;
    }
    auto& sc = mShadowConfig;

    // ---- Cascaded Shadow Maps ----------------------------------------
    if (ImGui::CollapsingHeader("Shadow · Cascaded Shadow Maps",
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        // SliderInt 区间 [1, 4]，对齐 ShadowConfig 注释（kMaxCascades = 4，
        // 1 = 退化为单 cascade 历史 ±10 ortho box）。
        int cascadeCount = static_cast<int>(sc.cascadeCount);
        if (ImGui::SliderInt("Cascade Count", &cascadeCount, 1, 4))
        {
            if (cascadeCount < 1) { cascadeCount = 1; }
            if (cascadeCount > 4) { cascadeCount = 4; }
            sc.cascadeCount = static_cast<std::uint32_t>(cascadeCount);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Cascaded Shadow Maps 级联数（directional light 阴影）。\n"
                "  1 = 单 cascade（退化为历史 ±10 ortho box，调试用）\n"
                "  3 (default) / 4 = 真 CSM：按相机视锥分段，近段锐 / 远段覆盖大\n"
                "上限由 PipelineImpl::kMaxCascades = 4 锁定。\n"
                "影响 spot/point 阴影：无（各自独立 shadow map / cubemap）。");
        }

        ImGui::Checkbox("Debug · Cascade Tint Overlay", &sc.debugCascadeTint);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "调试：pbr.frag 在最终输出上 mix 一层 per-cascade 颜色\n"
                "  cascade 0 = 红 / 1 = 绿 / 2 = 蓝 / 3 = 黄\n"
                "让 cascade 边界直观可见——shipping 永远关。\n"
                "对照 sample 18 `--tint` 视觉验证。");
        }
    }

    // ---- PCSS / PCF --------------------------------------------------
    if (ImGui::CollapsingHeader("Shadow · Filter (PCSS / PCF)",
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::DragFloat("PCSS Light Size (texel)", &sc.pcssLightSize,
                         0.1f, 0.0f, 32.0f);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "0 = 固定 PCF；>0 = PCSS 软阴影（接触硬、远处软）。\n"
                "  作用 directional + spot 阴影；point cubemap 走固定 PCF\n"
                "  8–16 在 2048 分辨率下是可见的柔和档位\n"
                "  CSM cascade 间按 orthoExtent ratio 自动缩放保 world-space\n"
                "  半影宽度一致（cascadePcssScales[i]）\n"
                "⚠ 若场景含 PostProcessComponent，本值会被组件 pcssLightSize 覆盖。");
        }

        int pcfRadius = static_cast<int>(sc.pcfKernelRadius);
        if (ImGui::SliderInt("PCF Kernel Radius", &pcfRadius, 0, 2))
        {
            if (pcfRadius < 0) { pcfRadius = 0; }
            if (pcfRadius > 2) { pcfRadius = 2; }
            sc.pcfKernelRadius = static_cast<std::uint32_t>(pcfRadius);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "PCF box filter 半径，采样次数 = (2r+1)^2。\n"
                "  0 = 1×1（无 PCF，硬阴影）\n"
                "  1 = 3×3（default，9 次采样）\n"
                "  2 = 5×5（25 次采样，更柔但 fragment 成本明显涨）\n"
                "PCSS 启用时（pcssLightSize > 0），PCF 半径作为下限。");
        }
    }

    // ---- Shadow Map Resolution + Bias --------------------------------
    if (ImGui::CollapsingHeader("Shadow · Resolution & Bias",
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        // 下拉：1024 / 2048 / 4096 三档常用 2 的幂；非 2 的幂虽然引擎也支
        // 持，但 GPU 分块布局可能浪费带宽，编辑器只暴露推荐档位。
        constexpr std::uint32_t kResOptions[] = {1024, 2048, 4096};
        constexpr const char*   kResLabels[]  = {"1024", "2048", "4096"};
        int currentIdx = 1;  // 默认 2048
        for (int i = 0; i < 3; ++i)
        {
            if (kResOptions[i] == sc.mapResolution) { currentIdx = i; break; }
        }
        // 非典型分辨率（用户外部 SetShadowConfig 写入）下 currentIdx 命中
        // default 2048 行，preview 文本下方再用 SmallText 提示真值。
        if (ImGui::Combo("Map Resolution", &currentIdx, kResLabels, 3))
        {
            sc.mapResolution = kResOptions[currentIdx];
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "阴影贴图边长（正方形）。Pipeline 检测到分辨率变化时下一帧\n"
                "EnsureShadowMap 重建 shadow target；切换中可能丢 1 帧阴影。\n"
                "directional / spot 共享本分辨率档；point cubemap 走固定 512×512。\n"
                "⚠ 若场景含 PostProcessComponent 且其 shadowMapResolution != 0，本值会被覆盖。");
        }
        if (sc.mapResolution != kResOptions[currentIdx])
        {
            ImGui::TextDisabled("(actual: %u)", sc.mapResolution);
        }

        ImGui::DragFloat("Depth Bias",  &sc.depthBias,  0.0005f, 0.0f, 0.1f, "%.4f");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "深度偏移：避免 z-fighting 引起的 shadow acne。\n"
                "  值的尺度与场景的 light-space 深度范围耦合\n"
                "  默认 0.005 适合 ~50 单位深度的 PCG 主光\n"
                "  scene scale 不同（厘米/公里）需要按比例调整");
        }

        ImGui::DragFloat("Normal Bias", &sc.normalBias, 0.001f,  0.0f, 0.2f, "%.4f");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "法线偏移：沿表面法线推一段，进一步抑制 acne。\n"
                "  配合 Depth Bias 使用，对斜面阴影特别有效\n"
                "  默认 0.01；过大会让小物件阴影脱离表面（peter-panning）");
        }
    }

    if (ImGui::Button("Reset Shadow Settings to Editor Defaults"))
    {
        // 与 mShadowConfig 字段声明同款 designated init —— 与历史 hardcode
        // 等价：mapResolution=2048 + pcssLightSize=12 + 其余 ShadowConfig 默认。
        sc = Orange::Engine::Render::ShadowConfig{
            .mapResolution = 2048,
            .pcssLightSize = 12.0f,
        };
    }

    ImGui::End();
}
