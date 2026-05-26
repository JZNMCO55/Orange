#ifndef ORANGE_ENGINE_RENDER_SHADOW_CONFIG_H
#define ORANGE_ENGINE_RENDER_SHADOW_CONFIG_H

// ---------------------------------------------------------------------------
// ShadowConfig —— Pipeline 阴影路径的全局配置。
//
// 这是 per-pipeline 配置（不是 per-light），决定阴影贴图分辨率、PCF 采
// 样核、深度偏移量。Pipeline 暴露 `SetShadowConfig` by-value
// 接收这个结构体。
//
// 默认值是卡通 / PCG 类游戏的常见基线：
//   * 1024×1024 阴影贴图 —— 在 1080p 主屏幕下硬边阴影看起来 OK，2K+
//     场景下调到 2048；
//   * 3×3 PCF（pcfKernelRadius = 1）—— 微羽化边沿但仍保持卡通风格的
//     硬阴影感；
//   * depthBias 0.005 + normalBias 0.01 —— 抑制 shadow acne 的常用起
//     点，按场景尺度调。
//
// 字段都是 plain value——Pipeline 不持有 ShadowConfig 引用，每次 Set
// 时直接拷贝。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstdint>

namespace Orange::Engine::Render
{

struct ShadowConfig
{
    // 阴影贴图边长（正方形）。1024 / 2048 / 4096 是常见档位；非 2 的幂
    // 也能用，但部分硬件分块布局会浪费带宽。
    std::uint32_t mapResolution{1024};

    // PCF box filter 的半径（采样次数 = (2r+1)^2）。
    //   * 0 = 1×1（无 PCF，纯硬阴影 —— 仍走 sampler2DShadow 路径，但单
    //     样本，性能上等价于无 PCF）；
    //   * 1 = 3×3（默认，9 次采样）；
    //   * 2 = 5×5（25 次采样，更柔但 fragment 成本明显涨）。
    std::uint32_t pcfKernelRadius{1};

    // 深度偏移：避免 z-fighting 引起的 shadow acne。值的尺度与场景的
    // light-space 深度范围耦合——PCG 主光投影 50 单位深度时 0.005 是
    // 合适的；scene scale 不同（厘米 / 公里）需要按比例调整。
    float depthBias{0.005f};

    // 法线偏移：沿表面法线推一段，进一步抑制 acne。配合 depthBias 使
    // 用，对斜面阴影特别有效。
    float normalBias{0.01f};

    // PCSS 软阴影的光源半影尺度（shadow map texel 单位）。
    //   * 0（默认）= 关闭 PCSS，走固定半径 PCF（= pcfKernelRadius，行为不变）；
    //   * > 0 = 开启 percentage-closer soft shadows：受影体离遮挡面越远半影越
    //     宽（接触处硬、远处软），lightSize 同时作 blocker search 半径与最大
    //     filter 半径。8~16 在 1024 分辨率下是可见的柔和档位。
    // 仅 directional + spot 阴影消费（point cubemap 仍走固定 PCF）。
    float pcssLightSize{0.0f};
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_SHADOW_CONFIG_H
