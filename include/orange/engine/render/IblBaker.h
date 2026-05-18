#ifndef ORANGE_ENGINE_RENDER_IBL_BAKER_H
#define ORANGE_ENGINE_RENDER_IBL_BAKER_H

// ---------------------------------------------------------------------------
// IblBaker —— IBL（Image-Based Lighting）三件套的烘焙工具骨架。
//
// 三件套：
//   * irradiance cubemap     —— Lambertian 半球积分（漫反射 IBL）
//   * prefiltered specular   —— GGX importance sampling 多 mip 卷积（镜面 IBL）
//   * BRDF LUT（2D R16G16F） —— split-sum 第二项预积分（全局共享）
//
// 三件套共享一条 "HDR equirect → cubemap resample" 输入预处理：站点常用
// 的 IBL HDR 资产是 `.hdr` Radiance 等距长方形（equirectangular）2D 贴
// 图，烘焙前必须先重采样到 6-face cube map。本工具的第一个 entry point
// `BakeEquirectToCube` 就承担这一步——后续 irradiance / prefilter 把它
// 的输出作为各自卷积路径的源。
//
// **当前阶段**：暴露 `BakeEquirectToCube` + `BakeBrdfLut` + `BakeIrradiance`。
// prefilter entry point 在后续 commit 增量引入；公共面按需扩，避免一次
// 性暴露半成品 API（与 PBR-04 commit-plan 的 c1 → c4 节奏对齐）。
//
// 设计参考：
//   * Lumix `data/shaders/ibl_filter.hlsl` —— 单文件多 entry point 的
//     IBL filter shader（prefilter 路径，c4 复用）
//   * Orange-Wiki `concepts/rendering/environment-lighting.md` § IBL
//     split-sum 数学
//   * Orange-Wiki `techniques/rendering/environment-mapping.md` §
//     equirect ↔ cube 坐标变换
//   * OrangeRender `docs/api_guide.md §6.10` —— cube view / 子资源
//     view / storage image compute 端到端配方
//
// **生命周期**：本工具持有 compute pipeline / descriptor pool 等瞬时资
// 源，typical 用法是启动期 IBL 烘焙完毕后立即析构；返回的 cube map 所
// 有权交给调用方（典型由 EnvironmentComponent 接管，与场景同寿）。
//
// **header isolation**：公共面前向声明 `Orange::Rhi::RHIDevice` /
// `Orange::Rhi::RHITexture`（不 include `<orange/...>`），消费者 .cpp
// 内 include 完整 RHI 头后即可调用。这条约束源自 CLAUDE.md 的 Header
// isolation 规则（公共头不能漏 OrangeRender 类型给下游）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstdint>
#include <memory>

namespace Orange::Rhi
{
class RHIDevice;
class RHITexture;
}  // namespace Orange::Rhi

namespace Orange::Engine::Asset
{
class AssetRegistry;
}  // namespace Orange::Engine::Asset

namespace Orange::Engine::Render
{

class ORANGE_ENGINE_API IblBaker
{
public:
    // 构造：保留 device / registry 引用，但**不**立即创建 compute
    // pipeline / descriptor pool——所有 GPU 资源在第一次 Bake* 调用时
    // 懒加载。析构释放所有内部资源。
    //
    // `registry` 负责把内置 SPIR-V（`shaders/orange_engine/ibl_*.comp.spv`）
    // 注册到 AssetRegistry 走 dedup；与 BuiltinShadowShaders / BuiltinMaterials
    // 同节奏（.exe-相对路径作 key，重复调用幂等）。
    IblBaker(Orange::Rhi::RHIDevice& device, Asset::AssetRegistry& registry);
    ~IblBaker();

    IblBaker(const IblBaker&)            = delete;
    IblBaker& operator=(const IblBaker&) = delete;

    IblBaker(IblBaker&&) noexcept;
    IblBaker& operator=(IblBaker&&) noexcept;

    // 把 HDR equirect 2D texture 重采样到 6-face cube map。
    //
    // 输入约束：
    //   * `equirectHdr` 必须是 Tex2D + 浮点 format（典型 RGBA16Float /
    //     RGBA32Float）+ 已含 `Sampled` usage；caller 负责保证调用前 texture
    //     处于 `ShaderResource` 状态。本函数不修改 input 状态。
    //   * `cubeFaceSize` 必须 ≥ 8 且为 8 的倍数（compute workgroup 8×8 对齐）；
    //     典型值 512（与 Lumix / Filament 默认相同）。
    //
    // 输出：
    //   * 新 cube map，`mWidth == mHeight == cubeFaceSize`，`mArrayLayers == 6`，
    //     `mFormat == RGBA16Float`（HDR + storage image 全 desktop GPU 稳定），
    //     `mUsage == Sampled | Storage | TransferSrc`；返回时已 transition 到
    //     `ShaderResource` 状态，可直接绑给下游 sampling pass / 后续 IBL
    //     卷积路径作输入。
    //   * 失败返回 `nullptr`，并通过 `Core::Log` 输出诊断（compute pipeline 创建
    //     失败、descriptor 分配失败、SPV 加载失败等）。
    std::unique_ptr<Orange::Rhi::RHITexture>
    BakeEquirectToCube(Orange::Rhi::RHITexture& equirectHdr,
                       std::uint32_t            cubeFaceSize);

    // 烘焙 split-sum 第二项预积分 BRDF LUT —— 2D R16G16F 纹理，(scale, bias)
    // 两通道、索引 (NoV, roughness)。全局共享一次性烘焙（与具体 environment
    // 无关，所有 EnvironmentComponent 共用同一份 LUT）。
    //
    // 参数约束：
    //   * `lutSize`：每边像素数，必须 ≥ 8 且为 8 的倍数；典型 256（与
    //     Filament / UE / glTF reference renderer 一致）。
    //   * `sampleCount`：GGX importance sampling 样本数；典型 1024（收敛足
    //     够、~64ms 一次性 GPU 开销可接受）。低于 128 视觉上 LUT 表面会出
    //     现 banding；这里不做下限强制，调用方自行权衡。
    //
    // 输出：
    //   * 新 2D 纹理，`mFormat == RG16Float`，`mUsage == Sampled | Storage |
    //     TransferSrc`；返回时已 transition 到 `ShaderResource` 状态。
    //   * 失败返回 `nullptr`（compute pipeline 创建失败、RG16F storage 不被
    //     当前 GPU 支持等）。
    std::unique_ptr<Orange::Rhi::RHITexture>
    BakeBrdfLut(std::uint32_t lutSize     = 256u,
                std::uint32_t sampleCount = 1024u);

    // 烘焙 Lambertian 漫反射 IBL —— 输入环境 cubemap（c1 输出），输出
    // 32×32×6 RGBA16Float irradiance cubemap。本函数输出**已 fold 1/π**
    // 的形式（即 `E/π` 而非 E），与 `pbr.frag.glsl` 端 `iblDiffuse =
    // kDibl * irradiance * baseColor`（不再除 π）约定一致；这是
    // LearnOpenGL / glTF reference / Filament 通用做法。
    //
    // 参数约束：
    //   * `envCube` 必须是 TexCube + 浮点 format + arrayLayers=6，处于
    //     `ShaderResource` 状态；本函数不修改 input 状态。
    //   * `cubeFaceSize`：≥ 8 且为 8 的倍数；典型 32（irradiance 是低
    //     频信号，32 分辨率已经充分；与 LearnOpenGL / Khronos glTF
    //     reference renderer 默认一致）。
    //   * `sampleCount`：cos-weighted Hammersley 样本数；典型 512。低
    //     于 128 视觉上会出现 banding；这里不做下限强制。
    //
    // 输出：
    //   * RGBA16Float cubemap，`mUsage == Sampled | Storage | TransferSrc`；
    //     返回时已 transition 到 `ShaderResource` 状态。
    //   * 失败返回 `nullptr`。
    std::unique_ptr<Orange::Rhi::RHITexture>
    BakeIrradiance(Orange::Rhi::RHITexture& envCube,
                   std::uint32_t            cubeFaceSize = 32u,
                   std::uint32_t            sampleCount  = 512u);

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_IBL_BAKER_H
