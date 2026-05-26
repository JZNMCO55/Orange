#ifndef ORANGE_ENGINE_RENDER_POST_PROCESS_PASSES_H
#define ORANGE_ENGINE_RENDER_POST_PROCESS_PASSES_H

// ---------------------------------------------------------------------------
// PostProcessPasses —— 引擎内置 4 个 IPostProcessPass 具体类。
//
// 默认链顺序：HDR → Bloom → Tonemap → LUT。每类的 Setup / Execute 当前
// 是空 stub（仅交付接口与默认链描述符；Pipeline 真跑
// 后处理链后续在不破公共面的前提下接通），但每类各自的 public
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
    // 义）当前固定，等到写实际 tonemap shader 时再决定是否引
    // 入 enum 选项。
    float exposure{1.0f};

    const char* Name() const noexcept override;
    void        Setup(PostProcessSetupContext& ctx) override;
    void        Execute(PostProcessExecuteContext& ctx) override;
};

class ORANGE_ENGINE_API LutPass final : public IPostProcessPass
{
public:
    // 颜色查找表 texture handle。后续与 sample 一并接入
    // 真正的 3D LUT（17×17×17 unrolled to 2D atlas）；当前阶段允
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

// SSAO（屏幕空间环境光遮蔽）。前向渲染下从 sceneDepth
// 重建 view-space pos/normal，半球 kernel 采样估遮蔽 → 4×4 box 模糊 → 乘法
// blend 进 HDR。与 god rays 同款"挂进 chain 由 Pipeline 走专用 RecordSsaoPass"
// 模式（Setup/Execute 是空壳，真实工作在 Pipeline 侧）。
//
// 法线取自法线预通道的 normalBuffer（真实几何法线），非深度差分重建。
//
// 两种算法（useGtao 切换）：默认半球 kernel SSAO（Crytek 风格）；useGtao=true
// 走 GTAO（Ground Truth AO，Jimenez 2016）——horizon-based + 余弦加权弧积分，
// 更接近真值、噪声更低、曲面接触更准。两者共用同一 UBO / descriptor / 模糊 /
// 合成路径，仅 pass-1 pipeline 不同。
//
// 已知简化：AO 乘到的是"已含直接光的 HDR"而非仅环境项（无 G-buffer 的前向
// 取舍），凹处直接光会被轻微压暗；要严格只作用 ambient 需 G-buffer 分离。
// window + 编辑器 offscreen 两路径均生效。
class ORANGE_ENGINE_API SsaoPass final : public IPostProcessPass
{
public:
    // 整体开关。false 等同 chain 里没挂本 pass。
    bool enabled{true};

    // 采样半径（view 空间，单位米）。越大遮蔽范围越广、越"软"，过大易
    // 跨物体串扰。典型 0.3–1.0。
    float radius{0.5f};

    // 深度比较偏置，抑制平坦面自遮蔽 acne。典型 0.02–0.05。
    float bias{0.025f};

    // 整体强度：最终 ao = mix(1, ao, strength)。0=无效果，1=全量。
    float strength{1.0f};

    // 对比 power：ao = pow(ao, power)。>1 加深暗部、收紧过渡。典型 1–3。
    float power{1.8f};

    // 半球样本数。16 偏噪、32 默认（× 分辨率影响 GPU）。上限 32（与
    // Pipeline::Impl::kSsaoKernelSize / shader ORANGE_SSAO_MAX_KERNEL 对齐；
    // 超出由 Pipeline 端 clamp）。
    std::int32_t kernelSize{32};

    // 切换到 GTAO（Ground Truth AO）。false（默认）= 半球 kernel SSAO；
    // true = horizon-based GTAO（共用 radius/bias/strength/power，kernelSize
    // 忽略，改用下面的 slice/step 数）。
    bool useGtao{false};

    // GTAO slice 数（屏幕空间方向数，每条 slice 覆盖 PI 弧）。3–4 常见；
    // 越多越平滑、越贵。仅 useGtao 时生效。
    std::int32_t gtaoSliceCount{3};

    // GTAO 每条 slice 单侧的 horizon march 步数。4–8 常见。仅 useGtao 时生效。
    std::int32_t gtaoStepsPerSlice{4};

    const char* Name() const noexcept override;
    void        Setup(PostProcessSetupContext& ctx) override;
    void        Execute(PostProcessExecuteContext& ctx) override;
};

// SSR（屏幕空间反射）。前向渲染下从 sceneDepth 重建 view-space pos/normal，
// 沿反射方向 view-space 射线步进，命中时采 hdrColor 作反射色，按 fresnel(NoV)
// + 边缘淡出加权，加性合成进 HDR（分层 fallback 的第一层；屏外/未命中处无
// 反射 = 缺省回退到既有 IBL，不做探针/平面层）。
//
// 已知简化（无 G-buffer 的前向取舍）：① 无 per-pixel roughness/F0 → 用统一
// F0=0.04 的 fresnel 权重（grazing 角反射强，适合地面），非材质驱动；②
// 加性合成（非能量守恒的 lerp），反射叠加为"光泽 sheen"而非物理替换 —
// 适合 stylized 湿表面（流体史莱姆）。严格物理需 G-buffer + 分层探针。
// 法线取自法线预通道的 normalBuffer。window + 编辑器 offscreen 两路径均生效。
class ORANGE_ENGINE_API SsrPass final : public IPostProcessPass
{
public:
    bool enabled{true};

    // view-space 最大射线长度（米）。决定反射能"看多远"，过大增步进成本。
    float maxDistance{12.0f};

    // 射线步进数。每步推进 maxDistance/maxSteps；越多越准越慢。典型 24–48。
    float maxSteps{32.0f};

    // 命中厚度容差（view-space 米）。z-buffer 只存最近面，厚度内才算命中，
    // 太小漏命中、太大穿透。典型 0.3–1.0。
    float thickness{0.6f};

    // 反射强度整体乘子（叠加到 fresnel 权重上）。0=无反射，1=满。
    float strength{0.6f};

    const char* Name() const noexcept override;
    void        Setup(PostProcessSetupContext& ctx) override;
    void        Execute(PostProcessExecuteContext& ctx) override;
};

// 接触阴影（contact shadows）。屏幕空间向光源 view-space 射线步进，命中遮挡则
// 输出 <1 阴影因子乘进 HDR。补 shadow map + PCSS 在接触处因 depthBias 抬起留下
// 的"漏光缝隙"（sub-texel 接触细节），AAA 引擎常用（UE Contact Shadows）。与
// SSAO/SSR 同款 Pipeline 内部 RecordContactShadowPass 录制（Setup/Execute 空壳）。
//
// 仅对 directional light 生效（无 directional light 时整 pass 跳过）。已知简化：
// 阴影因子乘进整个 HDR（含 ambient/其他光），非仅 directional——与 SSAO 同款前向
// 取舍；length 取接触尺度（短）使串扰可忽略。window + 编辑器 offscreen 两路径均生效。
class ORANGE_ENGINE_API ContactShadowPass final : public IPostProcessPass
{
public:
    bool enabled{true};

    // view-space 最大射线长度（米）。接触阴影应"短"——只补接触尺度的缝隙，
    // 过长会变成廉价全局阴影且串扰。典型 0.1–0.5。
    float length{0.25f};

    // 射线步进数。短射线下 8–16 足够。
    float maxSteps{16.0f};

    // 命中厚度容差（view-space 米）。z-buffer 只存最近面，厚度内才算命中。
    float thickness{0.5f};

    // 起点沿光向的偏移（view-space 米），防自遮蔽 acne。典型 0.01–0.05。
    float bias{0.02f};

    // 阴影强度。1=命中处全黑，<1 半透。
    float strength{1.0f};

    const char* Name() const noexcept override;
    void        Setup(PostProcessSetupContext& ctx) override;
    void        Execute(PostProcessExecuteContext& ctx) override;
};

// 景深（depth of field）。从 sceneDepth 算 circle-of-confusion，按 CoC 半径在
// hdrColor 上圆盘 gather 模糊 → 独立 dofColor → composite 写回 HDR。对焦面锐利，
// 离焦渐糊。与 SSAO/SSR 同款 Pipeline 内部 RecordDofPass 录制（Setup/Execute 空壳）。
//
// 已知简化：单层 gather（不分 near/far）+ 圆盘均匀权重（无 bokeh 形状 / 前景
// 散射），对焦边界轻微 bleeding——stylized 够用。window + 编辑器 offscreen 两路径。
class ORANGE_ENGINE_API DofPass final : public IPostProcessPass
{
public:
    bool enabled{true};

    // 对焦距离（view-space 米，正值 = 离相机距离）。该深度处最锐利。
    float focusDistance{6.0f};

    // 对焦范围（米）。|深度 - focusDistance| 超过它即达最大模糊；越小景深越浅。
    float focusRange{4.0f};

    // 最大模糊半径（uv 单位，约屏幕比例）。典型 0.005–0.02。
    float maxCoCRadius{0.012f};

    const char* Name() const noexcept override;
    void        Setup(PostProcessSetupContext& ctx) override;
    void        Execute(PostProcessExecuteContext& ctx) override;
};

// TAA（temporal anti-aliasing）。每帧投影做 sub-pixel jitter，resolve 把当前帧与
// 重投影后的上一帧历史按 feedback 混合 + 邻域 clamp。多帧累积 = 超采样 → 边缘
// 抗锯齿 + 把 GTAO / 接触阴影 / 景深的 jitter 噪点去噪。与 SSAO/SSR 同款 Pipeline
// 内部 RecordTaaResolve 录制（Setup/Execute 空壳）。
//
// 已知简化：仅相机重投影（无 per-object 运动矢量，运动物体靠 clamp 兜底）。静态
// 相机下完美收敛；相机运动靠重投影；快速运动物体可能轻微 ghost。window + 编辑器
// offscreen 两路径。
class ORANGE_ENGINE_API TaaPass final : public IPostProcessPass
{
public:
    bool enabled{true};

    // 历史 feedback 权重（历史占比）。0.9 = 90% 历史 + 10% 当前；越高越稳越糊、
    // 收敛越慢。典型 0.85–0.95。
    float feedback{0.9f};

    const char* Name() const noexcept override;
    void        Setup(PostProcessSetupContext& ctx) override;
    void        Execute(PostProcessExecuteContext& ctx) override;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_POST_PROCESS_PASSES_H
