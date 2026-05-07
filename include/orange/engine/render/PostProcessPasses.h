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

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_POST_PROCESS_PASSES_H
