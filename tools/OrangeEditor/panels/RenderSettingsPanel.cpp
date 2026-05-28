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

    // ---- Color · Tonemap (GAP-2026-05-27-tonemap-operator-selection) ----
    // 直接编辑 chain 内 TonemapPass 的 op / exposure 字段，编辑后下一帧
    // Pipeline 录制 stage B tonemap pass 时按新 op 写 push constant，
    // viewport 立即生效。mpTonemapPassRef nullptr 时（chain 还没初始化 / 不
    // 含 TonemapPass）整段 disabled 显示。
    if (ImGui::CollapsingHeader("Color · Tonemap",
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (mpTonemapPassRef == nullptr)
        {
            ImGui::TextDisabled("(chain 不含 TonemapPass —— 编辑器视口尚未初始化或 chain 被外部替换)");
        }
        else
        {
            namespace R = Orange::Engine::Render;
            auto& tp = *mpTonemapPassRef;

            // Combo 4 选项 —— index 与 R::TonemapOperator enum 一一对应
            // （ACES_Narkowicz=0 / AgX=1 / Reinhard=2 / Linear=3）。
            constexpr const char* kOpLabels[] = {
                "ACES Narkowicz",
                "AgX (Troy Sobotka)",
                "Reinhard",
                "Linear (debug, no curve)",
            };
            int currentOp = static_cast<int>(tp.op);
            if (ImGui::Combo("Operator", &currentOp, kOpLabels, 4))
            {
                tp.op = static_cast<R::TonemapOperator>(currentOp);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "Tonemap 算子 —— HDR linear → LDR [0,1] 的曲线选择：\n"
                    "  ACES Narkowicz：2015 5-系数拟合 ACES filmic\n"
                    "                  AAA 2015-2020 事实标准；饱和色偏色压暗\n"
                    "  AgX           ：Troy Sobotka 设计，Blender 4.0+ 默认\n"
                    "                  分段 sigmoid + 色彩空间矩阵；饱和色处理优于 ACES\n"
                    "  Reinhard      ：经典 x/(1+x)；学术 baseline，HDR 高光被强压\n"
                    "  Linear        ：clamp 到 [0,1]，无曲线\n"
                    "                  调试用，emissive 物体硬边亮带染色\n"
                    "默认 ACES Narkowicz（与历史固定行为视觉等价）。");
            }

            ImGui::DragFloat("Exposure", &tp.exposure, 0.01f, 0.0f, 10.0f, "%.3f");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "曝光乘子（multiplicative）。1.0 = 原 HDR 输入直接喂给算子；\n"
                    "  线性 stop 调整：exposure *= 2.0^stops\n"
                    "  按算子映射前作用（与 ColorGradePass.exposure 在 tonemap 前\n"
                    "  作用同位置，但 grade 是 stops 单位、tonemap 是线性乘子）");
            }

            if (ImGui::Button("Reset Tonemap to Editor Defaults"))
            {
                tp.op       = R::TonemapOperator::ACES_Narkowicz;
                tp.exposure = 1.0f;
            }
        }
    }

    ImGui::End();
}
