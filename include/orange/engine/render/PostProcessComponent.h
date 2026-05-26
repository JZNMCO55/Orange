#ifndef ORANGE_ENGINE_RENDER_POST_PROCESS_COMPONENT_H
#define ORANGE_ENGINE_RENDER_POST_PROCESS_COMPONENT_H

// ---------------------------------------------------------------------------
// PostProcessComponent —— 场景里的屏幕空间后处理 + 阴影质量配置（数据驱动）。
//
// 设计意图：把此前 hardcode 在 ScenePanel / sample 里的 post 参数变成**可序列化
// 组件**，编辑器 schema 驱动 Inspector 自动出控件、随场景存盘。对标 Godot
// `Environment` / Unity Volume Profile / Unreal `FPostProcessSettings`，但与既有
// `EnvironmentComponent`（管 IBL）并列、职责分开。
//
// 覆盖范围：SSAO（含 GTAO）/ SSR / 接触阴影 / 景深 / TAA / 色彩分级 + PCSS 软
// 阴影质量。**不含 bloom / tonemap** —— 那两个是 HDR→LDR 收尾、与 PostProcessChain
// 的 stage-A/B 划分耦合，仍由默认 chain（CreateDefault）管理。
//
// 全局 vs 局部：v1 作为**全局单例**消费（Pipeline find-first，mode 字段保留但只走
// Global 分支）。下面的 mode / localExtent / priority / blendDistance 是 **volume-
// ready** 占位 —— v2 升级 Pipeline 到 "collect-all + 相机位置混合" 解锁局部 volume
// 时**零 schema 改动**。多数屏幕空间 post 的"局部"= 相机进体积时参数 blend（效果
// 仍全屏），而非逐像素遮罩。
//
// 字段刻意**扁平**（前缀分组而非嵌套子结构）：编辑器 ComponentSchemaBuilder 用
// `&T::member` 指针-成员声明字段，只支持直接成员。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <glm/vec3.hpp>

#include <cstdint>

namespace Orange::Engine::Render
{

struct PostProcessComponent
{
    // —— volume 容器（v2 局部混合用；v1 Pipeline 只走 Global）——
    enum class Mode : std::uint8_t { Global = 0, Local = 1 };
    Mode      mode          = Mode::Global;
    glm::vec3 localExtent   = {5.0f, 5.0f, 5.0f};  // Local：实体 Transform 处的半尺寸盒
    float     priority      = 0.0f;                // 多 volume 重叠时谁压谁
    float     blendDistance = 1.0f;                // 边界外这段距离线性淡入

    // —— SSAO / GTAO（环境光遮蔽）——
    bool  ssaoEnabled  = true;
    bool  ssaoUseGtao  = true;   // true = GTAO（horizon-based）；false = 半球 kernel
    float ssaoRadius   = 0.6f;
    float ssaoStrength = 1.0f;
    float ssaoPower    = 1.8f;

    // —— SSR（屏幕空间反射）——
    bool  ssrEnabled     = true;
    float ssrMaxDistance = 12.0f;
    float ssrThickness   = 0.6f;
    float ssrStrength    = 0.6f;

    // —— 接触阴影 ——
    bool  contactEnabled   = false;
    float contactLength    = 0.15f;
    float contactThickness = 0.3f;
    float contactStrength  = 0.9f;

    // —— 景深 ——
    bool  dofEnabled       = false;
    float dofFocusDistance = 10.0f;
    float dofFocusRange    = 10.0f;
    float dofMaxCoCRadius  = 0.012f;

    // —— TAA（时序抗锯齿）——
    bool  taaEnabled  = false;
    float taaFeedback = 0.9f;

    // —— 色彩分级 ——
    bool  gradeEnabled     = false;
    float gradeExposure    = 0.0f;
    float gradeContrast    = 1.0f;
    float gradeSaturation  = 1.0f;
    float gradeTemperature = 0.0f;
    float gradeTint        = 0.0f;

    // —— 阴影质量（PCSS 软阴影 + shadow map 分辨率）——
    float         pcssLightSize       = 0.0f;   // 0 = 固定 PCF；>0 = PCSS 软阴影
    std::uint32_t shadowMapResolution = 1024;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_POST_PROCESS_COMPONENT_H
