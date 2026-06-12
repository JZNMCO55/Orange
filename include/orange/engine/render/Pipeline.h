#ifndef ORANGE_ENGINE_RENDER_PIPELINE_H
#define ORANGE_ENGINE_RENDER_PIPELINE_H

// ---------------------------------------------------------------------------
// Pipeline —— "World → 一帧画面"的根入口。
//
// 当前仅交付公共面：
//   * 生命周期（默认构造、移动、析构）；
//   * 单一驱动方法 `Render(World&)` —— 后续把 World 翻译为
//     drawable list，用 OrangeRender RenderGraph 真正下发。
//
// 公共头**不**包含任何 OrangeRender / Vulkan 头：依据 CLAUDE.md 的
// "Header isolation" 不变量，`<orange/...>` 只允许出现在
// `src/render/**`。Pipeline 走 PIMPL 把 OrangeRender 的 RHI / RenderGraph
// 类型完全藏在 .cpp 一侧。
//
// `InsertPass` 当前会出现但暂未真正接通；当前不
// 提前 stub 它（避免一个仅 assert(false) 的占位接口污染公共面）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Result.h>
#include <orange/engine/render/IRenderPass.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace Orange::Engine
{
class World;
}

namespace Orange::Engine::Render
{
class IAuxPassProvider;
}

namespace Orange::Engine::Asset
{
class AssetRegistry;
}

namespace Orange::Engine::Platform
{
class Window;
}

namespace Orange::Engine::Scene
{
class WorldPartition;
}

// OrangeRender 公共面前向声明 —— 公共头**不**包含 `<orange/...>`，但前向
// 声明指针 / 引用类型是允许的（与 OrangeRender 自家 `VulkanInterop.h` 前
// 向声明 `RHITexture` / `RHICommandList` 同节奏）。消费者真正取用时再
// `#include <orange/renderer/RenderDevice.h>` / `<orange/rhi/RHITexture.h>`。
namespace Orange::Renderer
{
class RenderDevice;
}
namespace Orange::Rhi
{
class RHITexture;
}

namespace Orange::Engine::Render
{

struct Camera;
class DebugDrawScene;
class MaterialSystem;
class PostProcessChain;
class VfxSystem;
struct ShadowConfig;

// 渲染调试视图模式（debug render views）。Lit = 正常渲染（默认，零回归）；其余
// 为诊断模式，渲染时替换着色 / 光栅化方式：
//   * Wireframe —— polygonMode=LINE（需 OrangeRender fillModeNonSolid device
//     feature，7bc8c57 起可用；SDK 须含该 commit）；
//   * Unlit     —— 忽略光照，直出 base color；
//   * Normals   —— world-space normal 映射到 RGB；
//   * Overdraw  —— 加性 blend 计数 overdraw 热图。
// 本次仅落地公共 API + mode 状态（Lit 生效）；各 mode 渲染实现逐个后续接入。
enum class DebugViewMode
{
    Lit,
    Wireframe,
    Unlit,
    Normals,
    Overdraw,
};

class ORANGE_ENGINE_API Pipeline
{
public:
    Pipeline();
    ~Pipeline();

    Pipeline(const Pipeline&)            = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    Pipeline(Pipeline&&) noexcept;
    Pipeline& operator=(Pipeline&&) noexcept;

    // 在指定 window 上初始化 OrangeRender 渲染栈：RenderDevice、
    // Renderer（双缓冲 in-flight）、内置 GraphicsPipeline、textured-
    // mesh shader、UploadContext（mesh 顶点 / 索引上传用）。
    //
    // `assets` 用于在 Render() 时按 AssetHandle 反查 MeshAsset 的
    // CPU 数据；其生存期必须长于 Pipeline 的活跃期。重复 Initialize
    // 返回 AlreadyInitialized。`window` 的生存期同样必须长于 Pipeline。
    Result<void, ResultCode> Initialize(::Orange::Engine::Platform::Window&        window,
                                        ::Orange::Engine::Asset::AssetRegistry&    assets);

    // 离屏模式初始化：不绑定 swap-chain，渲染结果落到 Pipeline 内部持有
    // 的 `viewportColor` RT（BGRA8Unorm，SRGB-compatible），调用方通过
    // `GetOffscreenColor()` 拿到 `Orange::Rhi::RHITexture*` 后用
    // `Orange::Renderer::Interop::GetVulkanImageView()` 取 `VkImageView`
    // 喂给 `ImGui_ImplVulkan_AddTexture` —— 这是编辑器 Scene 面板视口的
    // 标准路径。
    //
    // 与 `Initialize(window, ...)` 互斥：同一 Pipeline 实例两条 Initialize
    // 入口只能选一条，第二次（不论同模式还是异模式）返回
    // `AlreadyInitialized`。`device` 借用语义（不取所有权）：调用方负责
    // 保证 device 活到 `Shutdown()` 之前；典型路径是宿主（编辑器）自己
    // 创建 `RenderDevice` 给 ImGui Vulkan backend 用，同一份借给 Pipeline。
    //
    // S1 范围（当前实现）：本路径**只**跑主 pass（含 shadow），bloom /
    // tonemap / godrays / `RequestCapture` / `InsertPass` 一律走 fallback
    // 或 silent-ignore——HDR 域 raw color 经一次 passthrough 直写到
    // viewportColor。若编辑器需要 LDR / tonemap 视觉效果，后续子任务再
    // 把后处理接进来；S1 目标是先把"渲染 → 拿 native view"链路打通。
    //
    // `width × height` 必须 > 0；为 0 返回 `InvalidArgument`。后续运行期
    // 调 `ResizeOffscreen` 改尺寸。
    Result<void, ResultCode> InitializeOffscreen(
        ::Orange::Renderer::RenderDevice&        device,
        ::Orange::Engine::Asset::AssetRegistry&  assets,
        std::uint32_t                            width,
        std::uint32_t                            height);

    // 通知 Pipeline 离屏目标尺寸需要变化（编辑器 Scene 面板 resize 路径）。
    // 与 `OnResize(width, height)` 同语义：标记 dirty，真正重建发生在下一
    // 次 `Render()` 顶部。未 `InitializeOffscreen` 时 no-op。`width × height
    // == 0`（面板折叠）允许，Pipeline 跳过下一帧 Render 的主 pass 直到尺
    // 寸再变正。
    void ResizeOffscreen(std::uint32_t width, std::uint32_t height) noexcept;

    // 当前离屏 final output color RT。仅 `InitializeOffscreen` 模式下且
    // 完成至少一次 `Render()` 后非空；window 模式 / 未 Initialize / 当前
    // 尺寸为 0 → 返回 nullptr。
    //
    // 生命周期：返回的 `RHITexture` 由 Pipeline 拥有，`Shutdown()` 或
    // `ResizeOffscreen` 触发重建时句柄失效——消费者拿到 ImGui descriptor
    // set 后**必须**在每次 resize 时重新调本接口并重新绑 view。
    //
    // 取 `VkImageView` 标准路径（编辑器侧）：
    // ```
    // auto* tex  = pipeline.GetOffscreenColor();
    // auto* view = Orange::Renderer::Interop::GetVulkanImageView(*tex);
    // VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(sampler,
    //     static_cast<VkImageView>(view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    // ImGui::Image(reinterpret_cast<ImTextureID>(ds), ...);
    // ```
    const ::Orange::Rhi::RHITexture* GetOffscreenColor() const noexcept;

    // 释放 OrangeRender 资源。在调用 Window 析构之前必须调用——
    // RenderDevice 会先 WaitIdle 再释放 swap-chain 上挂的资源。
    // 幂等：未初始化或重复调用都是 no-op。
    void Shutdown();

    bool IsInitialized() const noexcept;

    // 通知渲染器宿主窗口可绘制区域发生变化（resize / DPI 切换 / 显示器切
    // 换）。消费者应在 Platform 层的 WindowResizeEvent 路径上调一次。
    // 重建是延迟的（OrangeRender 内部在下一次 BeginFrame 顶部统一执行），
    // 所以调用方不需要担心 thread / frame 状态。
    //
    // 未 Initialize 时 OnResize 是 no-op；framebuffer 0×0（窗口最小化）
    // 同样允许——OrangeRender 那侧会在 extent 退化时跳过重建、把 dirty
    // 标记保留到下次有效 extent 出现。
    void OnResize(std::uint32_t width, std::uint32_t height);

    // 渲染一帧。当前阶段：
    //   * RenderScene 收集（World → drawable list）；
    //   * 接通 OrangeRender RenderGraph：每帧 BeginFrame +
    //     提交一个内置 minimal-mesh draw（gl_VertexIndex 走的硬编码
    //     triangle）+ EndFrame。drawable list 已收集，但当前内置
    //     pipeline 不读其几何——后续把 mesh upload 路径接进
    //     来时再切到按 drawable 驱动。
    //
    // 未 Initialize 时 Render 是 no-op；让"在 main loop 顶层无脑
    // 调一发"成为受支持的退化状态。
    void Render(::Orange::Engine::World& world);

    // 安装 / 卸载 PostProcessChain（非拥有指针；nullptr 走 fallback：
    // chain 缺失时 Pipeline 内置一条 passthrough fullscreen pass，把离
    // 屏 HDR 直接采样到 swap-chain，与 06.02 完成态在 LDR 域字节级一致）。
    // chain 必须活到 Pipeline 析构 / 下次 SetPostProcessChain 之前。
    void SetPostProcessChain(PostProcessChain* chain) noexcept;

    // 安装 / 卸载 MaterialSystem（非拥有指针）。当前阶段（06.02 / 06.03）
    // Pipeline 不消费 system——drawable 直接持 MaterialInstance* 自己路
    // 由；本接口预留给后续子任务（per-frame uniform / shadow descriptor
    // 等需要 system 协作的路径）。nullptr 时 Pipeline 走 fallback：
    // drawable.materialInstance == nullptr 时落到内置 textured Material。
    void SetMaterialSystem(MaterialSystem* system) noexcept;

    // 安装 / 卸载 WorldPartition（非拥有指针）。安装后 Render() 在
    // RenderScene 收集阶段会按 partition 的 layer 可见性过滤 drawable
    // ——隐藏 layer 的实体既不进主 pass 也不进 shadow pass。partition
    // 必须活到 Pipeline 析构 / 下次 SetWorldPartition 之前；nullptr 退
    // 化到"不按 layer 过滤"行为，与 v1.x 之前完全等价。
    void SetWorldPartition(const ::Orange::Engine::Scene::WorldPartition* partition) noexcept;

    // 安装 / 卸载编辑器 viewport 相机覆写（非拥有指针）。安装后 Render()
    // 在 RenderScene::Collect 之后把 main camera 替换为本 override 的
    // view/projection；ECS 内挂的 Render::Camera 组件**不被修改**——
    // CameraFrustumGizmoPlugin / 多相机调度等读 ECS Camera 的下游路径
    // 拿到的是用户在场景里摆位的游戏侧相机数据，与编辑器轨道相机解耦。
    //
    // 典型用法（参 `tools/OrangeEditor/panels/ScenePanel.cpp`）：
    //   editorCam = BuildEditorCamera(host.camera, aspect);
    //   pipeline.SetEditorCameraOverride(&editorCam);
    //   pipeline.Render(world);   // 用 override，不 mutate world
    //
    // override 必须活到 Pipeline 析构 / 下次 SetEditorCameraOverride 之
    // 前；nullptr 退化到"读 ECS 首个 Camera 组件"行为（与 v1.x 之前完
    // 全等价，sample / 非编辑器消费者无任何视觉差异）。
    //
    // 这是 GAP-2026-05-15-camera-editor-vs-runtime-separation 引擎侧落
    // 地路径（path A，最小侵入）。
    void SetEditorCameraOverride(const Camera* camera) noexcept;

    // 安装 / 卸载 VfxSystem（非拥有指针）。安装后 Render() 在主 pass 与
    // bloom 之间插一段 instanced additive billboard pass，把 VfxSystem
    // 当前所有 emitter 的粒子绘制到 HDR target——粒子颜色 a > 1 时自动
    // 喂到 bloom。`system` 必须活到 Pipeline 析构 / 下次 SetVfxSystem
    // 之前；nullptr 跳过粒子 pass，sample 不 break。
    //
    // 调用方仍需要自己每帧调 `VfxSystem::Tick(world, dt)` 推进 sim—
    // Pipeline 不接管 sim，避免把 frame budget 耦合到 sim 时序。
    void SetVfxSystem(VfxSystem* system) noexcept;

    // 把游戏侧自定义 IRenderPass 挂到指定 stage 的 hook 点上。Pipeline
    // 立即调一次 pass 的 Setup（让 pass 建 GPU 资源），之后每帧到达该
    // stage 时按 Insert 顺序逐个调 Execute。
    //
    // 同 stage 多次 InsertPass → 按调用顺序排队（第一次 Insert 的 pass
    // 第一个 Execute）。
    //
    // pass 必须非空；nullptr silent-ignore——与 PostProcessChain::AddPass
    // 同行为，避免调用方 forgot-init 时 crash。
    //
    // 详见 docs/extension-points.md §4 与 IRenderPass.h。
    void InsertPass(PipelineStage stage, std::unique_ptr<IRenderPass> pass);

    // 清掉某个 stage 上所有已注册 pass。pass 的 unique_ptr 析构发生在
    // 本调用内（通常会触发 pass 的 destructor 释放 GPU 资源）。
    void RemovePassesAt(PipelineStage stage);

    // 清掉所有 stage 上的所有 pass。Pipeline::Shutdown 会自动做这件事，
    // 调用方一般不需要手动调；提供入口主要给 hot-reload / 编辑器
    // 切换 chain 等场景。
    void ClearInsertedPasses();

    // 当前 stage 上已注册的 pass 数（诊断 / 测试用）。
    std::size_t InsertedPassCount(PipelineStage stage) const noexcept;

    // 请求把下一帧的离屏 HDR 渲染结果落盘成 PNG。下一次 Render() 会在
    // bloom mip-chain 完成、tonemap 之前追加一次 GPU readback 到 host-
    // visible buffer，再在 CPU 端做 ACES Narkowicz tonemap、用 stb_image_write
    // 编 PNG 写到 outPath；写完后请求自动清空（不会重复触发）。
    //
    // 路径覆盖：HDR scene color（含 shadow / 主光衰减），但**不含 bloom**——
    // bloom 在 GPU 端 tonemap pass 内与 HDR 合成，本路径未参与该合成。
    // 这是 debug-only 的"够用"型截图，与屏幕看到的最终画面在 bloom halo
    // 上有差异，但场景结构 / 阴影 / 光照方向完全一致。
    //
    // outPath 父目录必须已存在（不自动 mkdir）；本调用幂等：本帧已经收
    // 到一次请求时第二次调用会覆盖前一个 outPath，仅最后一次生效。
    //
    // 仅 debug 用：本路径引入一次 GPU stall（额外 transition + copy +
    // wait fence）+ 一段 CPU 编码时间（~30 ms / 1280×720 量级），不要
    // 在生产 / release 路径上每帧调。
    void RequestCapture(const std::filesystem::path& outPath);

    // 设置当前帧时间（seconds，单调递增）。Pipeline 把它写进 light UBO
    // 的 uFrameInfo.x，shader 端用于 time-pulse / dissolve / 流光等
    // 跨帧持续效果。调用方应在 Render() 之前每帧调一次（典型路径：
    // RenderLayer::OnUpdate 拿 FrameContext.time.totalSeconds 喂进来）。
    // 未调用时 Pipeline 维持上一次值（构造时为 0）；不会触发 reinit。
    void SetFrameTime(float seconds) noexcept;

    // 安装 shadow 配置（by-value 拷贝）。chain 还没有装载或场景里没
    // DirectionalLight 时仍可调用——配置只在真正跑 shadow pass（Render
    // 看见 castsShadow == true 的 DirectionalLight）时生效；mapResolution
    // 切换会触发 shadow target 重建。未调用时使用 ShadowConfig 的默认
    // 值（1024 / 3×3 PCF / 0.005 depthBias / 0.01 normalBias）。
    void SetShadowConfig(const ShadowConfig& config) noexcept;

    // 设置 dummy IBL ambient（没挂 EnvironmentComponent / cubemap 未烘焙时
    // PBR 物体仍能拿到的全局 ambient 量级，写入 dummy irradiance cube 的所有
    // 1×1 face 像素）。engine 默认 (0, 0, 0)（中性化：相当于无 ambient
    // fallback，PBR 物体仅靠 direct light 着色，暗面纯黑），与 OrangeEngine
    // "Game-specific concepts forbidden in engine" + OrangeRender API 中性化
    // 原则同节奏。
    //
    // 典型调用方（编辑器侧）：lazy 创建 Pipeline 之后、InitializeOffscreen
    // 之前调本接口提到非 0 量级，避免"cube 暗面全黑/像透明"UX 陷阱：
    //   * (0.5, 0.5, 0.5)：OrangeEditor 默认值，暗面 ~50% baseColor
    //   * (0.25, 0.25, 0.25)：保守值（Unity URP / Cocos default 量级）
    //
    // 调用时机与行为：
    //   * Initialize 之前调：仅设字段；Initialize 时按本字段填 dummy 数据
    //   * Initialize 之后调：设字段 + 内部 cmd.Begin/End/Submit/WaitIdle
    //     一次性重填 dummyIrradianceCube（小开销，~64B copy）；**必须**在
    //     帧外调（首帧之前 / 两次 Render 之间），帧内调可能撞 validation
    //
    // 不影响 BakeIblFromWorld / SetIblTextures 接通的真 IBL —— 那些路径完全
    // 取代 dummy，本字段仅在三纹理为 dummy 时生效。
    //
    // r / g / b 取 [0, 8] 量级（half float 正常数范围），超出范围按 IEEE
    // half 自然截断；NaN / Inf 未定义。alpha 隐含 1.0。
    void SetDummyIblAmbient(float r, float g, float b) noexcept;

    // 设置主 pass 入口 clear color（HDR linear space，alpha 隐含 1.0）。
    // engine 默认 (0.05, 0.07, 0.10) —— shipping 中性深蓝灰，与 sample 历史
    // 视觉一致。
    //
    // 典型调用方（编辑器侧）：(0.12, 0.12, 0.13) Cocos 风中性灰，让 viewport
    // 与 main panel 深炭灰拉开亮度差便于辨识渲染区。
    //
    // 与 sky pass 配合：
    //   * sky 开 + cubemap 烘焙好 → sky shader 全屏覆盖，clear 仅用于
    //     driver spec requirement，不影响最终视觉
    //   * sky 关 / cubemap 未烘 → clear 直接作为背景色显示
    //
    // 任意时刻调用都安全（不动 GPU 资源，仅改下一帧 attachment desc）；
    // 未 Initialize 时也接受（字段持下来，Initialize 后生效）。
    void SetSceneClearColor(float r, float g, float b) noexcept;

    // 切换 IBL 三纹理（替换 Initialize 时注入的全局 dummy）。典型用法：
    // EnvironmentComponent 资产管线把 HDR 环境烘焙成 irradiance /
    // prefiltered specular cube + 一次性烘 BRDF LUT 之后，调用本接口接通
    // PBR shader 的 IBL 段。`pbr.frag.glsl` 的 binding 2/3/4 不变，整条
    // dummy → 真实切换不重编 shader、不重建 pipeline。
    //
    // 参数语义：
    //   * `irradianceCube` —— RGBA16F cube，per-EnvironmentComponent
    //   * `prefilteredCube` —— RGBA16F cube 多 mip，per-EnvironmentComponent
    //   * `brdfLut2D` —— RG16F 2D，全局共享一次性
    //   * 任一参数为 nullptr → 该 binding 回退到对应 dummy（启动期注入
    //     的 1×1 黑），等效该通道 IBL = 0；三参数全 nullptr 等同于把整
    //     套 IBL 切回 dummy 状态（适用于场景卸 EnvironmentComponent）
    //
    // **生命周期 / 同步契约**：
    //   * Pipeline 不取所有权；调用方负责保证 RHITexture 活到下次
    //     SetIblTextures 切换或 Pipeline::Shutdown 之前
    //   * 内部走 RHI `UpdateDescriptorSet`，应在帧外（两次 Render 之间或
    //     首帧之前）调用；帧内调用可能触发 validation warning 或视觉撕裂
    //   * 未 Initialize 时 silent-ignore（与 SetShadowConfig 同节奏，避免
    //     调用方 forgot-init 时 crash）
    void SetIblTextures(Orange::Rhi::RHITexture* irradianceCube,
                        Orange::Rhi::RHITexture* prefilteredCube,
                        Orange::Rhi::RHITexture* brdfLut2D) noexcept;

    // 高阶 IBL 接通入口：扫 World 找首张 EnvironmentComponent，把它的
    // `cubemap`（HDR equirect TextureAsset，RGBA32Float）上传到 GPU、用内置
    // IblBaker 跑完三件套（irradiance cube + prefiltered specular cube +
    // BRDF LUT 2D），再调 `SetIblTextures` 接通 PBR shader。烘焙产物由
    // Pipeline 内部持有 unique_ptr，等寿与 Pipeline 一致；重复调用本接口会
    // 释放上一轮产物再烘焙新的（典型用例：scene 切换 / 运行时切换 IBL 环境）。
    //
    // **典型用法**（sample 14_pbr_ibl）：
    // ```
    // pipeline.Initialize(window, assets);
    // // ...build world, attach EnvironmentComponent...
    // pipeline.BakeIblFromWorld(world, assets);   // 烘焙 + 接通
    // host->PushLayer(std::make_unique<RenderLayer>(pipeline, world));
    // ```
    //
    // **参数语义**：
    //   * `world` —— 扫描的 EnTT registry 源；找到首个 EnvironmentComponent
    //     即用，多个时取迭代器第一个（与 Pipeline LightUbo 端 EnvironmentComponent
    //     first-found 选取节奏一致）
    //   * `assets` —— 解析 `EnvironmentComponent.cubemap` 用的 AssetRegistry；
    //     必须与 cubemap handle 配对（同一 registry 创建出来的 handle）
    //
    // **退化分支**：
    //   * 未 Initialize → silent-ignore（与 SetIblTextures 同节奏）
    //   * 未挂 EnvironmentComponent / cubemap handle invalid / TextureAsset
    //     Format 非 RGBA32Float / IblBaker 任一阶段失败 → log + 调
    //     `SetIblTextures(nullptr, nullptr, nullptr)` 回退到 dummy IBL，不
    //     阻断 caller。这条策略让 sample / scene 即使 .hdr 资产缺失也能继续渲染
    //
    // **性能 / 时机**：本调用走 IblBaker 三件套（GGX importance sampling
    // 1024 samples + 9 mip prefilter），desktop GPU ~50-200ms 一次性开销；
    // 必须在帧外调用（两次 Render 之间 / 首帧之前），与 SetIblTextures
    // 同款帧外契约。内部含 `RHIDevice::WaitIdle`，**不可**在 frame loop
    // 内高频触发。
    void BakeIblFromWorld(::Orange::Engine::World&                world,
                          ::Orange::Engine::Asset::AssetRegistry& assets);

    // 注册辅助 pass 提供者（v1.3.0 引入）—— 让外部（editor / 游戏端）注入
    // 在主 pass 与后处理之间渲染的 aux pass（如 outline / wireframe /
    // debug overlay / 未来迁出的 grid）。详见 IAuxPassProvider.h 调用约定。
    //
    //   * `pProvider == nullptr`：清除当前注册（默认状态，不调用任何 hook）
    //   * 注册第二个 provider 会覆盖前者（v1.3.0 单 provider 设计；多
    //     provider 链式调用留待真有需求拉动）
    //   * Pipeline 不持 provider 所有权；调用方负责让 provider 活到
    //     Shutdown / SetAuxPassProvider(nullptr) 之间
    //
    // 未 Initialize 时仍接受调用（字段持下来，Initialize 后生效）。
    void SetAuxPassProvider(IAuxPassProvider* pProvider) noexcept;

    // 接通引擎托管的 ImGui debug-UI overlay（消费者在游戏侧 live-debug /
    // 调参的统一入口；与 play-in-editor 正交——这是游戏自己进程里的即时
    // 模式调试 UI）。一次性创建 ImGui context + GLFW/Vulkan backend +
    // descriptor pool，并在内部 renderer 的 swap-chain overlay 注册一次
    // `RenderDrawData`。此后每帧 `Render()` 在内部 `ImGui::NewFrame` 之后、
    // `ImGui::Render` 之前回调 `SetImGuiSubmit` 注册的提交函数，再把生成的
    // draw data 录进 swap-chain image。
    //
    // 约束：
    //   * 仅 **window 模式**（`Initialize(window, ...)`）支持；offscreen 模
    //     式（编辑器路径，自管 ImGui）调用返回 `InvalidArgument`。
    //   * 必须在 `Initialize` 成功之后调；未 Initialize 返回 `NotInitialized`。
    //   * 重复调用返回 `AlreadyInitialized`。
    //   * 进程内同一时刻只应有一个 Pipeline 接通 ImGui（ImGui context 是
    //     进程级单例）。
    //
    // ImGui 资源在 `Shutdown()` 内按正确顺序释放（WaitIdle → 清 overlay →
    // backend shutdown → DestroyContext → descriptor pool）。
    //
    // 消费者侧 `OnImGui()` body 自行 `#include <imgui.h>`（引擎把 imgui
    // include 目录以 INTERFACE 形式 PUBLIC 暴露，与 STATIC 库发布一致）。
    Result<void, ResultCode> EnableImGui();

    bool IsImGuiEnabled() const noexcept;

    // 注册每帧 ImGui 提交回调。Pipeline 在 `Render()` 内 `ImGui::NewFrame`
    // 之后、`ImGui::Render` 之前调一次本回调，消费者在其中提交 ImGui 窗口
    // / widget。典型接法把 `AppHost::DispatchImGui`（遍历 LayerStack 调各
    // `Layer::OnImGui`）接进来：
    //   pipeline.SetImGuiSubmit([h = host.get()]{ h->DispatchImGui(); });
    // 未 EnableImGui 时仍可调用（仅存字段，EnableImGui 后生效）。传空函数
    // 等于"本帧不提交任何 ImGui"。
    void SetImGuiSubmit(std::function<void()> submit);

    // 天空盒（sky-dome）显隐开关（默认开）。开启时若 EnvironmentComponent
    // cubemap 已烘焙（BakeIblFromWorld 走过且成功），Pipeline 在主几何
    // pass 之前画一层全屏 cubemap sample 作背景。关闭 / cubemap 未烘焙
    // 时回退到主 pass 的 clear color（默认深蓝灰）。
    //
    // 典型调用方：编辑器或游戏端 toolbar；本字段独立于 IBL —— IBL 永远
    // 按 BakeIblFromWorld 结果接通 PBR shader，与是否画 sky-dome 解耦。
    void SetSkyEnabled(bool enabled) noexcept;
    bool IsSkyEnabled() const noexcept;

    // 设 / 取渲染调试视图模式（见 DebugViewMode）。默认 Lit（零回归）。编辑器
    // viewport toolbar 据此切换；各 mode 渲染实现逐个后续接入。未 Initialize 时
    // Set 安全 no-op、Get 返回 Lit。
    void          SetDebugViewMode(DebugViewMode mode) noexcept;
    DebugViewMode GetDebugViewMode() const noexcept;

    // 取 Pipeline 内置 DebugDrawScene 引用。Pipeline 自己管 Initialize /
    // Render 内 SetViewProj+Flush / Shutdown 三段生命周期；消费者拿到指针
    // 后**仅**调 Add* / SetEnabled 等公共面（参 `DebugDrawScene.h`）。
    //
    // 未 Initialize / Shutdown 后返回 nullptr。`InitializeOffscreen` 模式
    // 与 window 模式都支持；HDR 目标格式（RGBA16Float）自动匹配 DebugDraw
    // backend pipeline。
    //
    // 路径：每帧 Render() 在主 pass + 粒子 + grid 之后、passthrough/tonemap
    // 之前调 DebugDraw flush，几何叠加在 HDR scene color 上方，不写深度。
    // 与编辑器 ScenePanel toolbar "Debug Draw" checkbox 配套：toggle 走
    // `dbg->SetEnabled(bool)` zero-cost off 路径。
    DebugDrawScene*       GetDebugDrawScene() noexcept;
    const DebugDrawScene* GetDebugDrawScene() const noexcept;

    // 当前帧已经按 const Material* 缓存的 RHI Pipeline 数量。Pipeline 在
    // Render() 时对每个 drawable 按其 MaterialInstance 绑定的 Material
    // 路由到一条 RHI Pipeline；同一 Material 多次出现只会编译一次。本
    // 方法主要供 ctest 与诊断用——0.x 阶段允许这种轻量 introspection，
    // 未来引入更正式的 PipelineStats 时会被它替代。
    //
    // 未 Initialize / 尚未渲染过任何 drawable 时返回 0。
    std::size_t TemplatePipelineCount() const noexcept;

    // 当前帧 HDR off-screen target 的尺寸。`width / height` 通过 out
    // 参数返回；未 Initialize / 窗口最小化时两者都置 0。诊断 + ctest 用，
    // 与 TemplatePipelineCount 同语义（轻量 introspection，未来 PipelineStats
    // 落地时被替代）。
    void GetHdrTargetSize(std::uint32_t& width, std::uint32_t& height) const noexcept;

    // 当前 bloom mip-chain 资源数量。chain 不含 BloomPass / 未 Render 过
    // 时返回 0；含 BloomPass 且 EnsureBloomResources 成功后返回 6（与
    // Pipeline 内部 kBloomMipCount 一致）。诊断 + ctest 用。
    std::size_t BloomMipCount() const noexcept;

    // 第 mipIndex 张 bloom mip 的尺寸（mipIndex < BloomMipCount() 时
    // 有效）；越界返回 (0, 0)。诊断 + ctest 用。
    void GetBloomMipSize(std::size_t mipIndex,
                         std::uint32_t& width,
                         std::uint32_t& height) const noexcept;

    // 离屏 final output（viewportColor，BGRA8）在 (x, y) 处的像素，读回成
    // 归一化 RGBA float（[0,1]）。**仅离屏模式 + 帧外**调用（内部自管 cmd
    // Begin/Submit/WaitIdle，会与 frame 录制冲突）。成功返回 true。诊断 +
    // ctest 像素回归用（GAP-2026-05-25 A2：补上 PBR 像素级回归网，避免再次
    // "ctest 全绿但视觉全黑"）。
    bool DebugReadbackPixel(std::uint32_t x, std::uint32_t y,
                            float outRGBA[4]) const;

    // 把当前 viewport 离屏渲染结果（GetOffscreenColor 那张 viewportColor，即用户
    // 屏幕上看到的画面——**含已激活的后处理** bloom/tonemap 等）整张回读到 CPU。
    // 字节序 BGRA8（每像素 4 字节：B, G, R, A，与 kSwapchainColorFormat 一致）。
    // 成功时 outBgra 被 resize 到 outW*outH*4 并填满像素，返回 true；非离屏模式 /
    // viewport 尚未渲染 / 设备缺失时返回 false（outBgra 不变）。
    //
    // 用途：MCP `capture_viewport`（AI 截图自验）。与 DebugReadbackPixel 同款"帧外
    // 一次性、内部自管 cmd Begin/Submit/WaitIdle"约束——**仅离屏模式 + 帧边界**调用，
    // 与 frame 录制冲突。读的是**已渲染好**的 viewportColor，不重新渲染、不动相机，
    // 故所见即用户所见（区别于 RenderToTexture 的 PBR 直出无后处理路径）。
    bool CaptureViewportToCpu(std::vector<std::uint8_t>& outBgra,
                              std::uint32_t& outW, std::uint32_t& outH) const;

    // 用本 Pipeline 已有的全套资源（IBL / shadow / material / mesh）把任意
    // world 渲染到调用方提供的外部 RT。material 缩略图 / mesh thumbnail /
    // scene snapshot 等编辑器 mini-render 的统一入口，免去各自复刻 mini-pipeline。
    //
    // 仅离屏模式（InitializeOffscreen 已成功）支持；window 模式返回 InvalidArgument。
    // target 要求：格式 == kSwapchainColorFormat（BGRA8Unorm）+ usage 含
    // RenderTarget | Sampled（若调用方要 readback 再加 TransferSrc）；width/height > 0。
    // 渲染范围（S1）：shadow + sky + 主 PBR pass + passthrough，**不含**后处理
    // （SSAO/SSR/Bloom/Tonemap 等一律 skip，与缩略图需求一致）。
    //
    // 帧外一次性调用（内部自管 cmd Begin/Submit/WaitIdle）。返回成功后 target
    // 处于 ShaderReadOnly，调用方可直接采样（ImGui::Image 等）。本调用**不影响**
    // 当前 viewport 的 GetOffscreenColor 缓存（独立 scratch，互不干扰）。
    Result<void, ResultCode> RenderToTexture(World& world, ::Orange::Rhi::RHITexture* target,
                                             std::uint32_t width, std::uint32_t height);

private:
    // 共享 RHI 资源创建逻辑（sampler / passthrough / bloom / tonemap /
    // godrays / 主 pass UBO / shadow caster pipeline / offscreen cmd list）。
    // Initialize / InitializeOffscreen 两条入口都调它；调用前必须保证
    // `mpImpl->renderDevice` + `mpImpl->upload` + `mpImpl->assets` 已就绪。
    // 失败时内部调 `Shutdown()` 整体回滚。
    Result<void, ResultCode> SetupRhiResources();

    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_PIPELINE_H
