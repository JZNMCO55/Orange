#ifndef ORANGE_ENGINE_RENDER_POST_PROCESS_PASSES_H
#define ORANGE_ENGINE_RENDER_POST_PROCESS_PASSES_H

// ---------------------------------------------------------------------------
// PostProcessPasses —— 引擎内置 4 个 IPostProcessPass 具体类。
//
// 默认链顺序：HDR → Bloom → Tonemap → LUT。每类的 Setup / Execute 当前
// 是空 stub（Phase 3 / Task 03 仅交付接口与默认链描述符；Pipeline 真跑
// 后处理链由后续 task 在不破公共面的前提下接通），但每类各自的 public
// 参数字段已经稳定可用——调用方拿到 pass 后直接修改成员即可调参。
//
// 取出参数：`chain.PassAt(1)` 拿 `IPostProcessPass*`，再 dynamic_cast
// 落到 `BloomPass*` / `TonemapPass*` 等。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/TextureAsset.h>
#include <orange/engine/render/IPostProcessPass.h>

#include <glm/vec3.hpp>

namespace Orange::Engine::Render
{

// HDR 渲染目标 marker pass。本身无参——它的存在表示 "本链头部走 HDR
// 线性空间"。Pipeline 真接通后处理链时识别 chain[0] 是 HdrPass 就把主
// 渲染目标切到 RGBA16F off-screen color target；不是 HdrPass 就走
// LDR swap-chain 直出。
class ORANGE_ENGINE_API HdrPass final : public IPostProcessPass
{
public:
    const char* Name() const noexcept override;
    void        Setup(PostProcessSetupContext& ctx) override;
    void        Execute(PostProcessExecuteContext& ctx) override;
};

class ORANGE_ENGINE_API BloomPass final : public IPostProcessPass
{
public:
    // 亮度阈值。color > threshold 的像素进入 bloom 累加。1.0 = 1.0 线
    // 性亮度门槛——LDR 输入下大部分场景不会触发，HDR 输入下仅高光部
    // 分参与 bloom，与 Karis 式 mip-chain bloom 同一思路。
    float threshold{1.0f};

    // 与原 color 混合的强度。0.5 = 50% 原色 + 50% bloom。
    float intensity{0.5f};

    const char* Name() const noexcept override;
    void        Setup(PostProcessSetupContext& ctx) override;
    void        Execute(PostProcessExecuteContext& ctx) override;
};

class ORANGE_ENGINE_API TonemapPass final : public IPostProcessPass
{
public:
    // 曝光乘子。1.0 = 原 HDR 输入直接喂给 tonemap 算子；线性 stop 调整
    // 走 `exposure *= 2.0^stops`。Tonemap 算子（Reinhard / ACES / 自定
    // 义）当前固定，等到 Task 06 写实际 tonemap shader 时再决定是否引
    // 入 enum 选项。
    float exposure{1.0f};

    const char* Name() const noexcept override;
    void        Setup(PostProcessSetupContext& ctx) override;
    void        Execute(PostProcessExecuteContext& ctx) override;
};

class ORANGE_ENGINE_API LutPass final : public IPostProcessPass
{
public:
    // 颜色查找表 texture handle。Phase 3 / Task 06 与 sample 一并接入
    // 真正的 3D LUT（17×17×17 unrolled to 2D atlas）；Task 03 阶段允
    // 许这个 handle 无效，pass 将被识别为 "no-op LUT"。
    Asset::AssetHandle<Asset::TextureAsset> lut{};

    // LUT 与原 color 的混合强度。1.0 = 完全用 LUT 输出，0.0 = 完全保留
    // 原 color。
    float strength{1.0f};

    const char* Name() const noexcept override;
    void        Setup(PostProcessSetupContext& ctx) override;
    void        Execute(PostProcessExecuteContext& ctx) override;
};

// 屏幕空间 god rays（Mitchell 2007 径向模糊）。可选 pass —— `enabled`
// 默认 false 让既有 sample 视觉不变；调用方主动开启再调参。Pipeline
// 在 bloom 之后 / tonemap 之前调一次本 pass，把"沿 sun 方向沉积的体积
// 光柱"加性写到 HDR target，再交给 tonemap 一并 ACES 压回 LDR。
//
// occlusion 来源：Pipeline 已维护的 sceneDepth (D32Float)。Pipeline 在
// 进入本 pass 前把 sceneDepth transition 到 ShaderReadOnly，shader 端
// 用 `depth ≈ 1.0 (far plane) → sun-visible` 作判定——天空 / 粒子云 /
// emissive 等不写 depth 的几何天然成为光柱穿透物，写 depth 的几何成为
// 遮挡物。
//
// **已知限制（沿用屏幕空间 god rays 的天然 trade-off）**：
//   * sun 投影到屏幕外时 sample loop 几乎处处采到非 far depth → 视觉上
//     god rays 自然消失（参考 vendor/Orange-Wiki/wiki/techniques/rendering/
//     volumetric-lighting.md "屏幕空间 god rays 角度太掠"那条限制）；
//   * 不含真正的 volumetric shadow / 异质介质——更高质量路径留给极线
//     采样 / Froxel 等后续 task。
class ORANGE_ENGINE_API GodRaysPass final : public IPostProcessPass
{
public:
    // 整体开关。false 时 Pipeline 跳过本 pass，与 chain 里没挂这个
    // pass 同效果——避免调用方为"临时关 god rays"频繁拆装 chain。
    bool enabled{false};

    // 主光源传播方向（指向"光的去处"，与 DirectionalLight.direction 同
    // 约定）。Pipeline 把它取负 + 映到屏幕 NDC 算 sunScreenPos，越接近
    // 屏幕中心光柱越正面，越偏屏幕边缘光柱越斜。投到屏外 → god rays
    // 自然消失（见类注释里的已知限制）。
    glm::vec3 sunWorldDir{0.3f, -1.0f, 0.4f};

    // 光柱颜色——通常与 DirectionalLight.color 一致或稍偏暖。频域累加
    // 后再乘 density × exposure，所以 sunColor.rgb 不需要预乘强度。
    glm::vec3 sunColor{1.0f, 0.95f, 0.80f};

    // 整体亮度乘子。粒子流 / emissive cube 等 HDR 像素本身可能就够亮，
    // density > 1 让 god rays 视觉上压住它们；< 1 则克制。
    float density{1.2f};

    // 沿 ray 每跳的能量衰减。<1 让远端 sample 贡献递减，给出"光柱前
    // 端最亮、远端淡出"的感觉。decay = 1 给均匀 ray，0.95 是常用值。
    float decay{0.97f};

    // 单 tap 的累加权重。numSamples × weight ≈ 整体放大倍率—— numSamples
    // 调大时同步降 weight 保持视觉量级稳定。
    float weight{0.04f};

    // 累加结果在 HDR 输出前再乘的倍率，与 BloomPass.intensity / Tonemap.
    // exposure 同思路——给调参留个独立旋钮。
    float exposure{1.0f};

    // ray sample 数量。32 / 64 / 128 三档常用：32 视觉略硬、64 默认、
    // 128 在 1080p+ 上更平滑。GPU 受 numSamples × resolution 影响。
    std::int32_t numSamples{64};

    const char* Name() const noexcept override;
    void        Setup(PostProcessSetupContext& ctx) override;
    void        Execute(PostProcessExecuteContext& ctx) override;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_POST_PROCESS_PASSES_H
