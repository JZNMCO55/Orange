// 4 个内置 IPostProcessPass 具体类的方法体。Setup / Execute 当前是空
// stub——Phase 3 / Task 03 仅交付接口与默认链描述符；Pipeline 真跑后
// 处理链由后续 task 在不破公共面的前提下接通，那时回到这里给 Setup /
// Execute 真正的内容。
//
// Name() 返回 string literal 指针——生命周期与可执行体一致，调用方按
// `std::string_view(pass->Name())` 即时拷贝即可。

#include "orange/engine/render/PostProcessPasses.h"

namespace Orange::Engine::Render
{

// ---------- HdrPass ----------------------------------------------------------

const char* HdrPass::Name() const noexcept
{
    return "hdr";
}

void HdrPass::Setup(PostProcessSetupContext& /*ctx*/)
{
    // 空 stub：Phase 3 / Task 03 仅交付接口；Pipeline 真接通时在这里把
    // 主渲染目标切到 RGBA16F off-screen color target。
}

void HdrPass::Execute(PostProcessExecuteContext& /*ctx*/)
{
    // 空 stub：HdrPass 通常不下发 fullscreen quad，只作为"链头标记"
    // 影响 Pipeline 的 render target 选型。Execute 留作 hook，未来如
    // 果加 HDR 直方图 / luminance histogram 等步骤再补。
}

// ---------- BloomPass --------------------------------------------------------

const char* BloomPass::Name() const noexcept
{
    return "bloom";
}

void BloomPass::Setup(PostProcessSetupContext& /*ctx*/)
{
    // 空 stub：Phase 3 / Task 03 仅交付接口；后续 task 在这里声明 mip-
    // chain（typically 6 levels downsample + 6 levels upsample）作为
    // RenderGraph resource。
}

void BloomPass::Execute(PostProcessExecuteContext& /*ctx*/)
{
    // 空 stub：后续 task 在这里下发 bright-pass + downsample chain +
    // upsample chain + composite，按 threshold / intensity 调参。
}

// ---------- TonemapPass ------------------------------------------------------

const char* TonemapPass::Name() const noexcept
{
    return "tonemap";
}

void TonemapPass::Setup(PostProcessSetupContext& /*ctx*/)
{
}

void TonemapPass::Execute(PostProcessExecuteContext& /*ctx*/)
{
    // 后续 task：fullscreen quad + tonemap shader（Reinhard / ACES，
    // 当前固定 ACES——见 PostProcessPasses.h 注释）；按 exposure 喂
    // push-constant。
}

// ---------- LutPass ----------------------------------------------------------

const char* LutPass::Name() const noexcept
{
    return "lut";
}

void LutPass::Setup(PostProcessSetupContext& /*ctx*/)
{
}

void LutPass::Execute(PostProcessExecuteContext& /*ctx*/)
{
    // 后续 task：fullscreen quad + 3D LUT 采样 shader（17×17×17
    // unrolled to 2D atlas），按 strength 在原 color 与 LUT 输出之间
    // 做 lerp。lut handle 无效时 pass 应早退（no-op LUT）。
}

}  // namespace Orange::Engine::Render
