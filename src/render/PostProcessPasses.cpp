// 4 个内置 IPostProcessPass 具体类的方法体。Setup / Execute 当前是空
// stub——仅交付接口与默认链描述符；Pipeline 真跑后
// 处理链由后续在不破公共面的前提下接通，那时回到这里给 Setup /
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
    // 空 stub：仅交付接口；Pipeline 真接通时在这里把
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
    // 空 stub：仅交付接口；后续在这里声明 mip-
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

// ---------- GodRaysPass ------------------------------------------------------

const char* GodRaysPass::Name() const noexcept
{
    return "god_rays";
}

void GodRaysPass::Setup(PostProcessSetupContext& /*ctx*/)
{
    // 真录制走 Pipeline.cpp 内部 RecordGodRaysPass —— 与 BloomPass 同模
    // 式（Pipeline 自管 cmd list、需要直接接 sceneDepth 与 HDR target，
    // 不走 IPostProcessPass 通用 Setup/Execute 通道）。
}

void GodRaysPass::Execute(PostProcessExecuteContext& /*ctx*/)
{
    // 见 Setup 注释：实际录制由 Pipeline 主流程持。本函数保持空 stub，
    // 让 PostProcessChain 的"逐 pass 调 Execute"循环不漏调任何 pass。
}

// ---------- SsaoPass ---------------------------------------------------------

const char* SsaoPass::Name() const noexcept
{
    return "ssao";
}

void SsaoPass::Setup(PostProcessSetupContext& /*ctx*/)
{
    // 同 GodRaysPass：实际录制走 Pipeline 内部 RecordSsaoPass（需直接接
    // sceneDepth / noise / HDR target，不走通用 Setup/Execute 通道）。
}

void SsaoPass::Execute(PostProcessExecuteContext& /*ctx*/)
{
    // 空 stub，见 Setup 注释。
}

// ---------- SsrPass ----------------------------------------------------------

const char* SsrPass::Name() const noexcept
{
    return "ssr";
}

void SsrPass::Setup(PostProcessSetupContext& /*ctx*/)
{
    // 同 SsaoPass：实际录制走 Pipeline 内部 RecordSsrPass。
}

void SsrPass::Execute(PostProcessExecuteContext& /*ctx*/)
{
    // 空 stub，见 Setup 注释。
}

// ---------- ContactShadowPass ------------------------------------------------

const char* ContactShadowPass::Name() const noexcept
{
    return "contact_shadow";
}

void ContactShadowPass::Setup(PostProcessSetupContext& /*ctx*/)
{
    // 同 SsaoPass：实际录制走 Pipeline 内部 RecordContactShadowPass。
}

void ContactShadowPass::Execute(PostProcessExecuteContext& /*ctx*/)
{
    // 空 stub，见 Setup 注释。
}

// ---------- DofPass ----------------------------------------------------------

const char* DofPass::Name() const noexcept
{
    return "dof";
}

void DofPass::Setup(PostProcessSetupContext& /*ctx*/)
{
    // 同 SsaoPass：实际录制走 Pipeline 内部 RecordDofPass。
}

void DofPass::Execute(PostProcessExecuteContext& /*ctx*/)
{
    // 空 stub，见 Setup 注释。
}

// ---------- TaaPass ----------------------------------------------------------

const char* TaaPass::Name() const noexcept
{
    return "taa";
}

void TaaPass::Setup(PostProcessSetupContext& /*ctx*/)
{
    // 同 SsaoPass：实际录制走 Pipeline 内部 RecordTaaResolve（+ Pipeline 端
    // per-frame jitter 注入 viewProj）。
}

void TaaPass::Execute(PostProcessExecuteContext& /*ctx*/)
{
    // 空 stub，见 Setup 注释。
}

// ---------- ColorGradePass ---------------------------------------------------

const char* ColorGradePass::Name() const noexcept
{
    return "color_grade";
}

void ColorGradePass::Setup(PostProcessSetupContext& /*ctx*/)
{
    // 同 SsaoPass：实际录制走 Pipeline 内部 RecordColorGradePass。
}

void ColorGradePass::Execute(PostProcessExecuteContext& /*ctx*/)
{
    // 空 stub，见 Setup 注释。
}

// ---------- MotionBlurPass ---------------------------------------------------

const char* MotionBlurPass::Name() const noexcept
{
    return "motion_blur";
}

void MotionBlurPass::Setup(PostProcessSetupContext& /*ctx*/)
{
    // 同 SsaoPass：实际录制走 Pipeline 内部 RecordMotionBlurPass。
}

void MotionBlurPass::Execute(PostProcessExecuteContext& /*ctx*/)
{
    // 空 stub，见 Setup 注释。
}

// ---------- LensPass ---------------------------------------------------------

const char* LensPass::Name() const noexcept
{
    return "lens";
}

void LensPass::Setup(PostProcessSetupContext& /*ctx*/)
{
    // 同 SsaoPass：实际录制走 Pipeline 内部 RecordLensPass。
}

void LensPass::Execute(PostProcessExecuteContext& /*ctx*/)
{
    // 空 stub，见 Setup 注释。
}

// ---------- SharpenPass ------------------------------------------------------

const char* SharpenPass::Name() const noexcept
{
    return "sharpen";
}

void SharpenPass::Setup(PostProcessSetupContext& /*ctx*/)
{
    // 同 SsaoPass：实际录制走 Pipeline 内部 RecordSharpenPass。
}

void SharpenPass::Execute(PostProcessExecuteContext& /*ctx*/)
{
    // 空 stub，见 Setup 注释。
}

}  // namespace Orange::Engine::Render
