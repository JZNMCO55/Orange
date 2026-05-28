// Pipeline::Impl 的完整 declaration —— Pipeline 走 PIMPL，公共头
// (`include/orange/engine/render/Pipeline.h`) 只 forward-declare struct
// Impl。后续按 pass 拆 .cpp 时（Setup / Shadow / Bloom / GodRays / Sky /
// Grid / DebugDraw / Capture），每个子 .cpp 都 #include 本 header 拿到
// Impl 完整成员定义，避免在 Pipeline.cpp 单 TU 里堆 5000+ 行。
//
// 本 header 是 internal（在 src/render/pipeline/ 下，不进 include/），
// 不参与 ABI；只在 src/render/**/*.cpp 内部消费。

#ifndef ORANGE_ENGINE_SRC_RENDER_PIPELINE_PIPELINEIMPL_H
#define ORANGE_ENGINE_SRC_RENDER_PIPELINE_PIPELINEIMPL_H

#include "orange/engine/render/Pipeline.h"

#include "PipelineHelpers.h"

#include "orange/engine/asset/AssetHandle.h"
#include "orange/engine/asset/AssetRegistry.h"
#include "orange/engine/asset/ShaderAsset.h"
#include "orange/engine/asset/TextureAsset.h"
#include "orange/engine/core/Log.h"
#include "orange/engine/platform/Window.h"
#include "orange/engine/render/BuiltinMaterials.h"
#include "orange/engine/render/DebugDrawScene.h"
#include "orange/engine/render/EnvironmentComponent.h"
#include "orange/engine/render/IAuxPassProvider.h"  // v1.3.0 · aux pass hook
#include "orange/engine/render/IRenderPass.h"
#include "orange/engine/render/LightComponent.h"
#include "orange/engine/render/Material.h"
#include "orange/engine/render/MaterialInstance.h"
#include "orange/engine/render/MaterialSystem.h"
#include "orange/engine/render/PostProcessChain.h"
#include "orange/engine/render/PostProcessComponent.h"
#include "orange/engine/render/PostProcessPasses.h"
#include "orange/engine/render/RenderScene.h"
#include "orange/engine/render/ShadowConfig.h"
#include "orange/engine/render/VfxSystem.h"
#include "orange/engine/scene/WorldPartition.h"

#include "orange/renderer/RenderDevice.h"
#include "orange/renderer/Renderer.h"
#include "orange/resource/UploadContext.h"
#include "orange/rhi/RHI.h"
#include "orange/rhi/RHIDescriptor.h"
#include "orange/rhi/RHISampler.h"
#include "orange/rhi/RHITexture.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Orange::Engine
{
class World;  // 仅作 ref / ptr 参数类型；不暴露完整定义
}

namespace Orange::Engine::Render
{

// 引擎托管 ImGui overlay 的全部 Vulkan / backend 状态。完整定义只在
// src/render/pipeline/PipelineImGui.cpp（唯一 #include <imgui.h> / vulkan.h
// 的 TU）里出现，避免把 imgui / vulkan 头拽进所有 include PipelineImpl.h
// 的 pass .cpp。Impl 仅持一个 unique_ptr<PipelineImGuiState>（PIMPL 套
// PIMPL），故 Impl 的析构必须 out-of-line（见 ~Impl 声明）。
struct PipelineImGuiState;

struct Pipeline::Impl
{
    // 构造 + 析构都 out-of-line（定义在 PipelineImGui.cpp）—— 让
    // unique_ptr<PipelineImGuiState>（不完整类型）在持有 / 构造 Impl 的 TU
    // （Pipeline.cpp 的 make_unique<Impl>）里不需要看到完整定义：构造函数同
    // 样会实例化成员的析构（异常清理路径），故 ctor 也必须 out-of-line。
    // 其余成员都是完整类型，两者 = default 即可。
    Impl();
    ~Impl();

    RenderScene                            scene;
    Asset::AssetRegistry*                  assets{nullptr};
    Platform::Window*                      window{nullptr};

    // 可选 WorldPartition 引用，由 SetWorldPartition 注入；非空时
    // RenderScene::Collect 会按 layer 可见性过滤 drawable。nullptr 退化
    // 到"不按 layer 过滤"行为。
    const Scene::WorldPartition*           worldPartition{nullptr};

    // 可选编辑器 viewport 相机覆写（非拥有指针），由 SetEditorCameraOverride
    // 注入。非空时 Render() 在 RenderScene::Collect 之后把 main camera
    // 替换为本指针指向的 Camera——ECS 内挂的 Render::Camera **不被修改**，
    // CameraFrustumGizmoPlugin 等读 ECS Camera 的路径拿到的是用户在场景
    // 里摆位的游戏侧相机数据。nullptr 退化到"读 ECS 首个 Camera 组件"。
    // 详见 GAP-2026-05-15-camera-editor-vs-runtime-separation 落地记录。
    const Camera*                          editorCameraOverride{nullptr};

    bool initialized{false};

    // window 模式 / offscreen 模式双向兼容：所有权交给 ownedRenderDevice
    // （仅 window 模式持有），所有访问统一过 raw 指针 renderDevice。
    // offscreen 模式 renderDevice 指向调用方提供的外部 RenderDevice
    // （借用），ownedRenderDevice 保持 nullptr。
    std::unique_ptr<Orange::Renderer::RenderDevice> ownedRenderDevice;
    Orange::Renderer::RenderDevice*                 renderDevice{nullptr};
    // true = InitializeOffscreen 路径，跳过 swap-chain stage B，把场景渲
    // 到内部 viewportColor RT；调用方通过 GetOffscreenColor + Interop
    // 接 ImGui::Image。
    bool                                            offscreenMode{false};
    std::unique_ptr<Orange::Renderer::IRenderer>    renderer;
    std::unique_ptr<Orange::Resource::UploadContext> upload;

    // 离屏模式 final output —— BGRA8Unorm + RenderTarget|Sampled，每帧主
    // pass + passthrough 写入；Render 末尾 transition 到 ShaderReadOnly
    // 让消费者直接采样。window 模式恒空。
    std::unique_ptr<Orange::Rhi::RHITexture> viewportColor;
    // viewportColor 跨帧 layout 跟踪：false = Undefined / 刚重建；true =
    // 上一帧末翻到 ShaderReadOnly。passthrough 写入前再翻回 ColorAttachment。
    bool                                     viewportLayoutShaderReadOnly{false};

    // drawable.materialInstance == nullptr 时的 fallback Material。第一
    // 次需要时 lazy-load——sample 即便不挂 MaterialSystem 也能跑通。
    // 当前 fallback 装配为 PBR 模板（替代历史 textured 棋盘），默认观感
    // 跃迁；textured 模板保留作 dev-checker，sample / DemoWorld 仍可显式
    // BuiltinMaterials::LoadTextured 使用。
    Material builtinDefaultMaterial;
    bool     builtinDefaultLoaded{false};

    // ShaderModule 缓存：键 = AssetHandle<ShaderAsset>::Value()。同 .spv
    // 跨 Material 复用。
    std::unordered_map<std::uint64_t, std::unique_ptr<Orange::Rhi::RHIShaderModule>> shaderModules;

    // Per-template Pipeline 缓存：键 = const Material*（地址稳定来自
    // MaterialSystem 内表 / Pipeline 自身的 builtinTexturedMaterial）。
    // 这些 pipeline 输出格式是 RGBA16F（HDR off-screen），与 swap-chain
    // 的 BGRA8Unorm 不同——离屏段绑 BeginRendering 时要求格式匹配。
    std::unordered_map<const Material*, std::unique_ptr<Orange::Rhi::RHIPipeline>> templatePipelines;

    struct MeshGpu
    {
        std::unique_ptr<Orange::Rhi::RHIBuffer> vertexBuffer;
        std::unique_ptr<Orange::Rhi::RHIBuffer> indexBuffer;
        std::uint32_t                           indexCount{0};
    };
    std::unordered_map<std::uint64_t, MeshGpu> meshCache;

    // ---- 双段 frame 流程相关资源 -----------------------------------------
    // 离屏 HDR scene color。每次 OnResize / 首帧前按 framebuffer extent
    // 重建。
    std::unique_ptr<Orange::Rhi::RHITexture> hdrColor;
    // 主 pass 的 scene depth attachment——D32Float、跟 hdrColor 同生命
    // 周期。没有它主 pass 会按 draw call 顺序覆盖（不做 depth test），重
    // 叠几何只能"后绘者赢"，导致球 / plane 这类 z 重叠场景出现"前后错
    // 乱"假象。
    std::unique_ptr<Orange::Rhi::RHITexture> sceneDepth;
    // sceneDepth 跨段 layout 跟踪——主 pass 输出 DepthStencilAttachment，
    // god rays pass 走 ShaderReadOnly 采样它，下一帧主 pass 再翻回 DSA。
    bool                                     sceneDepthLayoutShaderReadOnly{false};
    std::uint32_t                            hdrWidth{0};
    std::uint32_t                            hdrHeight{0};
    bool                                     hdrLayoutShaderReadOnly{false};

    // Pipeline 持续复用的离屏 cmd list（Begin/End/Submit 反复触发）。
    std::unique_ptr<Orange::Rhi::RHICommandList> offscreenCmd;

    // Passthrough 收尾资源 —— sampler / descriptor set layout / pool /
    // set / shader modules / pipeline。set 的 binding=0 在每次 hdrColor
    // 重建后通过 UpdateDescriptorSet 重新指向新 view。
    std::unique_ptr<Orange::Rhi::RHISampler>             hdrSampler;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> passthroughLayout;
    std::unique_ptr<Orange::Rhi::RHIDescriptorPool>      passthroughPool;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       passthroughSet;
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        fullscreenVs;
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        passthroughFs;
    std::unique_ptr<Orange::Rhi::RHIPipeline>            passthroughPipeline;

    // 期望的 framebuffer 尺寸——OnResize 写入，Render 顶部按它重建 HDR。
    std::uint32_t pendingWidth{0};
    std::uint32_t pendingHeight{0};
    bool          hdrDirty{true};

    std::uint64_t frameIndex{0};

    // 非拥有指针；当前阶段仅持有，下游子任务真正消费。
    PostProcessChain* postProcessChain{nullptr};
    MaterialSystem*   materialSystem{nullptr};
    // VfxSystem 也是非拥有指针——nullptr 时跳过粒子 pass。
    VfxSystem*        vfxSystem{nullptr};

    // 游戏侧自定义 IRenderPass 注册表 —— 每个 PipelineStage 一个 vector。
    // InsertPass 时 push_back 到对应 vec；Render 路径在 stage hook 点
    // 按顺序调 Execute；Pipeline 析构 / ClearInsertedPasses 时整体释放。
    static constexpr std::size_t kPipelineStageCount = 3;  // 与 PipelineStage 枚举对齐
    std::array<std::vector<std::unique_ptr<IRenderPass>>, kPipelineStageCount> insertedPasses;

    // ---- Bloom mip-chain 资源 ------------------------------------------
    // chain 含 BloomPass 时按 HDR target 尺寸建 6 张 RGBA16F；HDR 重建 /
    // chain 切换 / Pipeline 重建时同步重建。
    struct BloomMip
    {
        std::unique_ptr<Orange::Rhi::RHITexture> texture;
        std::uint32_t width{0};
        std::uint32_t height{0};
        bool layoutShaderReadOnly{false};
    };
    std::array<BloomMip, PipelineDetail::kBloomMipCount> bloomMips;
    bool bloomMipsReady{false};

    // bloom layout & pool 与 stage B 的 passthrough 各自独立，避免 set
    // 数与 binding 数互相挤压。
    std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> bloomLayout;       // 1 binding
    std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> combineLayout;     // 2 bindings
    std::unique_ptr<Orange::Rhi::RHIDescriptorPool>      bloomPool;
    // 6 个 downsample set + 5 个 upsample set + 1 个 combine set = 12 sets。
    std::array<std::unique_ptr<Orange::Rhi::RHIDescriptorSet>, PipelineDetail::kBloomMipCount>     bloomDownsampleSets;
    std::array<std::unique_ptr<Orange::Rhi::RHIDescriptorSet>, PipelineDetail::kBloomMipCount - 1> bloomUpsampleSets;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>                                                  bloomCombineSet;

    std::unique_ptr<Orange::Rhi::RHIShaderModule> bloomDownsampleFs;
    std::unique_ptr<Orange::Rhi::RHIShaderModule> bloomUpsampleFs;
    std::unique_ptr<Orange::Rhi::RHIShaderModule> passthroughCombineFs;
    std::unique_ptr<Orange::Rhi::RHIShaderModule> tonemapVs;
    std::unique_ptr<Orange::Rhi::RHIShaderModule> tonemapFs;

    std::unique_ptr<Orange::Rhi::RHIPipeline> bloomDownsamplePipeline;

    // ---- GodRaysPass GPU 资源 ------------------------------------------
    std::unique_ptr<Orange::Rhi::RHIShaderModule> godRaysFs;
    std::unique_ptr<Orange::Rhi::RHIPipeline>     godRaysPipeline;
    std::unique_ptr<Orange::Rhi::RHIDescriptorPool> godRaysPool;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>  godRaysSet;
    Orange::Rhi::RHITexture*                         godRaysSetBoundDepth{nullptr};

    // ---- SSAO GPU 资源（屏幕空间环境光遮蔽）---------------------------
    // ssao pass：sceneDepth + ubo → ssaoColor(R8，原始 AO；噪声用程序化
    // hash，不需 noise 纹理)；ssao_apply pass：ssaoColor 4×4 模糊 → 乘法
    // blend 进 hdrColor。
    static constexpr std::uint32_t kSsaoKernelSize = 32;   // ≤ shader ORANGE_SSAO_MAX_KERNEL(64)
    struct SsaoUboData
    {
        glm::mat4 proj{1.0f};
        glm::mat4 invProj{1.0f};
        glm::vec4 kernel[kSsaoKernelSize]{};  // xyz = 切线空间半球样本
        glm::vec4 params{0.5f, 0.025f, 1.0f, 1.8f};   // radius / bias / strength / power
        glm::vec4 params2{static_cast<float>(kSsaoKernelSize), 0.0f, 0.0f, 0.0f};  // kernelSize / 预留
    };
    // host 端半球 kernel（PipelineSetup 一次性生成；每帧连同 proj/params 写 UBO）。
    std::array<glm::vec4, kSsaoKernelSize> ssaoKernel{};
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        ssaoFs;
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        ssaoApplyFs;
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        gtaoFs;       // GTAO 变体（SsaoPass.useGtao）
    std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> ssaoLayout;   // 0=depth 1=ubo 2=normal
    std::unique_ptr<Orange::Rhi::RHIPipeline>            ssaoPipeline;
    std::unique_ptr<Orange::Rhi::RHIPipeline>            gtaoPipeline; // 复用 ssaoLayout + ssaoSet
    std::unique_ptr<Orange::Rhi::RHIPipeline>            ssaoApplyPipeline;
    std::unique_ptr<Orange::Rhi::RHIBuffer>              ssaoUbo;
    std::unique_ptr<Orange::Rhi::RHITexture>             ssaoColor;        // R8 原始 AO
    std::uint32_t                                        ssaoColorWidth{0};
    std::uint32_t                                        ssaoColorHeight{0};
    bool                                                 ssaoColorLayoutShaderReadOnly{false};
    std::unique_ptr<Orange::Rhi::RHIDescriptorPool>      ssaoPool;          // 容纳 ssaoSet + ssaoApplySet
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       ssaoSet;          // depth + ubo + normal
    Orange::Rhi::RHITexture*                             ssaoSetBoundDepth{nullptr};
    Orange::Rhi::RHITexture*                             ssaoSetBoundNormal{nullptr};
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       ssaoApplySet;     // ssaoColor
    Orange::Rhi::RHITexture*                             ssaoApplySetBoundAo{nullptr};

    // ---- SSR GPU 资源（屏幕空间反射）----------------------------------
    // ssr pass：sceneDepth + hdrColor + ubo → ssrColor(RGBA16F，反射色×权重)；
    // ssr_composite pass：ssrColor 加性 blend 进 hdrColor（独立 ssrColor 避免
    // 采 HDR 同时写 HDR 的反馈）。
    struct SsrUboData
    {
        glm::mat4 proj{1.0f};
        glm::mat4 invProj{1.0f};
        glm::vec4 params{12.0f, 32.0f, 0.6f, 0.6f};  // maxDistance / maxSteps / thickness / strength
    };
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        ssrFs;
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        ssrCompositeFs;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> ssrLayout;   // 0=depth 1=hdr 2=ubo 3=normal
    std::unique_ptr<Orange::Rhi::RHIPipeline>            ssrPipeline;
    std::unique_ptr<Orange::Rhi::RHIPipeline>            ssrCompositePipeline;
    std::unique_ptr<Orange::Rhi::RHIBuffer>              ssrUbo;
    std::unique_ptr<Orange::Rhi::RHITexture>             ssrColor;       // RGBA16F 反射色×权重
    std::uint32_t                                        ssrColorWidth{0};
    std::uint32_t                                        ssrColorHeight{0};
    bool                                                 ssrColorLayoutShaderReadOnly{false};
    std::unique_ptr<Orange::Rhi::RHIDescriptorPool>      ssrPool;        // ssrSet + ssrCompositeSet
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       ssrSet;         // depth + hdr + ubo + normal
    Orange::Rhi::RHITexture*                             ssrSetBoundDepth{nullptr};
    Orange::Rhi::RHITexture*                             ssrSetBoundHdr{nullptr};
    Orange::Rhi::RHITexture*                             ssrSetBoundNormal{nullptr};
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       ssrCompositeSet;  // ssrColor
    Orange::Rhi::RHITexture*                             ssrCompositeSetBound{nullptr};

    // ---- 接触阴影 GPU 资源（屏幕空间向光源射线步进硬阴影）-----------------
    // 单 pass：sceneDepth + ubo → 阴影因子，乘法 blend 直接进 hdrColor（与
    // ssao_apply 同款 multiply blend，无独立 target）。仅 directional light 时跑。
    struct ContactShadowUboData
    {
        glm::mat4 proj{1.0f};
        glm::mat4 invProj{1.0f};
        glm::vec4 viewLightDir{0.0f, 1.0f, 0.0f, 0.0f};  // xyz = 朝光方向(view space)
        glm::vec4 params{0.25f, 16.0f, 0.5f, 1.0f};      // length / maxSteps / thickness / strength
        glm::vec4 params2{0.02f, 0.0f, 0.0f, 0.0f};      // x=bias
    };
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        contactShadowFs;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> contactShadowLayout;  // 0=depth 1=ubo 2=normal
    std::unique_ptr<Orange::Rhi::RHIPipeline>            contactShadowPipeline;
    std::unique_ptr<Orange::Rhi::RHIBuffer>              contactShadowUbo;
    std::unique_ptr<Orange::Rhi::RHIDescriptorPool>      contactShadowPool;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       contactShadowSet;     // depth + ubo + normal
    Orange::Rhi::RHITexture*                             contactShadowSetBoundDepth{nullptr};
    Orange::Rhi::RHITexture*                             contactShadowSetBoundNormal{nullptr};

    // ---- 景深（DoF）GPU 资源 ----------------------------------------------
    // gather pass：hdrColor + depth + ubo → dofColor（CoC 圆盘模糊，RGBA16F）；
    // composite pass：dofColor → hdrColor（replace blend）。独立 dofColor 避免
    // gather 读写同 target 反馈。
    struct DofUboData
    {
        glm::mat4 invProj{1.0f};
        glm::vec4 params{6.0f, 4.0f, 0.012f, 0.0f};  // focusDistance / focusRange / maxCoCRadius / pad
    };
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        dofFs;
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        dofCompositeFs;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> dofLayout;   // 0=hdr 1=ubo 2=depth
    std::unique_ptr<Orange::Rhi::RHIPipeline>            dofPipeline;
    std::unique_ptr<Orange::Rhi::RHIPipeline>            dofCompositePipeline;
    std::unique_ptr<Orange::Rhi::RHIBuffer>              dofUbo;
    std::unique_ptr<Orange::Rhi::RHITexture>             dofColor;        // RGBA16F 模糊结果
    std::uint32_t                                        dofColorWidth{0};
    std::uint32_t                                        dofColorHeight{0};
    bool                                                 dofColorLayoutShaderReadOnly{false};
    std::unique_ptr<Orange::Rhi::RHIDescriptorPool>      dofPool;         // dofSet + dofCompositeSet
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       dofSet;          // hdr + ubo + depth
    Orange::Rhi::RHITexture*                             dofSetBoundHdr{nullptr};
    Orange::Rhi::RHITexture*                             dofSetBoundDepth{nullptr};
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       dofCompositeSet;  // dofColor
    Orange::Rhi::RHITexture*                             dofCompositeSetBound{nullptr};

    // ---- TAA（时序抗锯齿）GPU 资源 ----------------------------------------
    // 每帧 jitter 投影；resolve 把当前 HDR 与重投影的上一帧历史混合 + 邻域
    // clamp → 写 curHistory；再 copy curHistory → hdrColor（复用 dofComposite）。
    // taaHistory[2] ping-pong：parity p = frameIndex%2，写 [p] 读 [1-p]。
    struct TaaUboData
    {
        glm::mat4 invCurViewProj{1.0f};  // depth → world
        glm::mat4 prevViewProj{1.0f};    // world → prev clip
        glm::vec4 params{0.9f, 0.0f, 0.0f, 0.0f};  // feedback / hasHistory / pad
    };
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        taaResolveFs;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> taaLayout;   // 0=cur 1=hist 2=depth 3=ubo
    std::unique_ptr<Orange::Rhi::RHIPipeline>            taaResolvePipeline;
    std::unique_ptr<Orange::Rhi::RHIBuffer>              taaUbo;
    std::array<std::unique_ptr<Orange::Rhi::RHITexture>, 2> taaHistory{};
    std::array<bool, 2>                                  taaHistoryLayoutShaderReadOnly{false, false};
    std::uint32_t                                        taaHistoryWidth{0};
    std::uint32_t                                        taaHistoryHeight{0};
    std::unique_ptr<Orange::Rhi::RHIDescriptorPool>      taaPool;
    std::array<std::unique_ptr<Orange::Rhi::RHIDescriptorSet>, 2> taaResolveSet{};  // [p] 读 history[1-p]
    std::array<std::unique_ptr<Orange::Rhi::RHIDescriptorSet>, 2> taaCopySet{};     // [p] 读 history[p]
    Orange::Rhi::RHITexture*                             taaSetsBoundHdr{nullptr};  // 重建触发
    glm::mat4                                            taaPrevViewProj{1.0f};
    bool                                                 taaHasHistory{false};

    // ---- 色彩分级（color grading）GPU 资源 --------------------------------
    // grade pass：hdrColor + ubo → gradeColor（曝光/白平衡/对比/饱和）；composite
    // 复用 dofCompositePipeline 把 gradeColor replace 回 hdrColor。
    struct GradeUboData
    {
        glm::vec4 params{0.0f, 1.0f, 1.0f, 0.0f};      // exposure / contrast / saturation / pad
        glm::vec4 whiteBalance{1.0f, 1.0f, 1.0f, 0.0f};// rgb 乘子（host 由 temperature/tint 算）
    };
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        colorGradeFs;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> colorGradeLayout;   // 0=hdr 1=ubo
    std::unique_ptr<Orange::Rhi::RHIPipeline>            colorGradePipeline;
    std::unique_ptr<Orange::Rhi::RHIBuffer>              colorGradeUbo;
    std::unique_ptr<Orange::Rhi::RHITexture>             gradeColor;        // RGBA16F 分级结果
    std::uint32_t                                        gradeColorWidth{0};
    std::uint32_t                                        gradeColorHeight{0};
    bool                                                 gradeColorLayoutShaderReadOnly{false};
    std::unique_ptr<Orange::Rhi::RHIDescriptorPool>      colorGradePool;     // gradeSet + gradeCompositeSet
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       colorGradeSet;      // hdr + ubo
    Orange::Rhi::RHITexture*                             colorGradeSetBoundHdr{nullptr};
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       gradeCompositeSet;  // gradeColor
    Orange::Rhi::RHITexture*                             gradeCompositeSetBound{nullptr};

    // ---- 相机运动模糊（motion blur）GPU 资源 ------------------------------
    // gather pass：hdrColor + depth + ubo → motionBlurColor（沿屏幕速度方向 tap
    // 模糊，RGBA16F）；composite pass：motionBlurColor → hdrColor（复用 dofComposite
    // 的 replace blend）。独立 motionBlurColor 避免 gather 读写同 target 反馈。
    // 速度由当前 / 上一帧（均未 jitter）viewProj 重投影算出（与 TAA 同数学）。
    struct MotionBlurUboData
    {
        glm::mat4 invCurViewProj{1.0f};  // depth → world
        glm::mat4 prevViewProj{1.0f};    // world → prev clip
        glm::vec4 params{0.5f, 0.05f, 8.0f, 0.0f};  // intensity / maxRadius / sampleCount / hasHistory
    };
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        motionBlurFs;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> motionBlurLayout;   // 0=hdr 1=ubo 2=depth
    std::unique_ptr<Orange::Rhi::RHIPipeline>            motionBlurPipeline;
    std::unique_ptr<Orange::Rhi::RHIBuffer>              motionBlurUbo;
    std::unique_ptr<Orange::Rhi::RHITexture>             motionBlurColor;    // RGBA16F 模糊结果
    std::uint32_t                                        motionBlurColorWidth{0};
    std::uint32_t                                        motionBlurColorHeight{0};
    bool                                                 motionBlurColorLayoutShaderReadOnly{false};
    std::unique_ptr<Orange::Rhi::RHIDescriptorPool>      motionBlurPool;     // gatherSet + compositeSet
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       motionBlurSet;      // hdr + ubo + depth
    Orange::Rhi::RHITexture*                             motionBlurSetBoundHdr{nullptr};
    Orange::Rhi::RHITexture*                             motionBlurSetBoundDepth{nullptr};
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       motionBlurCompositeSet;  // motionBlurColor
    Orange::Rhi::RHITexture*                             motionBlurCompositeSetBound{nullptr};
    // 上一帧（未 jitter）viewProj + 历史有效标记（RecordMotionBlurPass 末尾更新）。
    glm::mat4                                            motionBlurPrevViewProj{1.0f};
    bool                                                 motionBlurHasHistory{false};

    // ---- 镜头效果（lens：色散 + 暗角）GPU 资源 ----------------------------
    // gather pass：hdrColor + ubo → lensColor（CA 沿径向偏移采样邻域，RGBA16F）；
    // composite pass：lensColor → hdrColor（复用 dofComposite 的 replace blend）。
    // 独立 lensColor 避免 CA 读 HDR 邻域同时写 HDR 同 target 的反馈。
    struct LensUboData
    {
        // x=chromaticAberration, y=vignetteIntensity, z=vignetteSmoothness, w pad
        glm::vec4 params{0.0f, 0.0f, 0.5f, 0.0f};
    };
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        lensFs;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> lensLayout;   // 0=hdr 1=ubo
    std::unique_ptr<Orange::Rhi::RHIPipeline>            lensPipeline;
    std::unique_ptr<Orange::Rhi::RHIBuffer>              lensUbo;
    std::unique_ptr<Orange::Rhi::RHITexture>             lensColor;     // RGBA16F 结果
    std::uint32_t                                        lensColorWidth{0};
    std::uint32_t                                        lensColorHeight{0};
    bool                                                 lensColorLayoutShaderReadOnly{false};
    std::unique_ptr<Orange::Rhi::RHIDescriptorPool>      lensPool;      // gatherSet + compositeSet
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       lensSet;       // hdr + ubo
    Orange::Rhi::RHITexture*                             lensSetBoundHdr{nullptr};
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       lensCompositeSet;  // lensColor
    Orange::Rhi::RHITexture*                             lensCompositeSetBound{nullptr};

    // ---- 锐化（sharpen，CAS 式）GPU 资源 ---------------------------------
    // gather pass：hdrColor 邻域 + ubo → sharpColor（RGBA16F）；composite pass：
    // sharpColor → hdrColor（复用 dofComposite 的 replace blend）。无 depth 依赖。
    struct SharpenUboData
    {
        glm::vec4 params{0.4f, 0.0f, 0.0f, 0.0f};  // sharpness / 预留
    };
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        sharpenFs;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> sharpenLayout;   // 0=hdr 1=ubo
    std::unique_ptr<Orange::Rhi::RHIPipeline>            sharpenPipeline;
    std::unique_ptr<Orange::Rhi::RHIBuffer>              sharpenUbo;
    std::unique_ptr<Orange::Rhi::RHITexture>             sharpenColor;    // RGBA16F 结果
    std::uint32_t                                        sharpenColorWidth{0};
    std::uint32_t                                        sharpenColorHeight{0};
    bool                                                 sharpenColorLayoutShaderReadOnly{false};
    std::unique_ptr<Orange::Rhi::RHIDescriptorPool>      sharpenPool;     // gatherSet + compositeSet
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       sharpenSet;      // hdr + ubo
    Orange::Rhi::RHITexture*                             sharpenSetBoundHdr{nullptr};
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       sharpenCompositeSet;  // sharpColor
    Orange::Rhi::RHITexture*                             sharpenCompositeSetBound{nullptr};

    // ---- 法线预通道 GPU 资源（view-space G-buffer 法线）-----------------
    // SSAO / SSR 此前用深度差分(dFdx/dFdy)从 sceneDepth 重建 view-space 法线——
    // 那在几何边缘 / 薄物体 / 接缝处出锯齿与错误遮蔽（一个三角面内导数恒定，
    // 跨面突变）。本预通道复用 shadow caster 的几何遍历模板，把每个 drawable 的
    // view-space 法线渲到 normalBuffer（RGBA8，编码 n*0.5+0.5），SSAO / SSR 改为
    // 直接采样真实几何法线。
    //
    // depth：复用 sceneDepth 作 scratch（预通道需 z-test 只保留最近面法线；写完
    // 留在 DepthStencilAttachment，紧跟的主 pass 以 Undefined→DSA + LoadOp::Clear
    // 自然丢弃这份深度，零额外 depth buffer）。因此预通道**必须**紧贴主 pass 之前
    // 录制。只在 SSAO 或 SSR 激活时跑，不付额外几何遍历成本。
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        normalPrepassVs;
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        normalPrepassFs;
    std::unique_ptr<Orange::Rhi::RHIPipeline>            normalPrepassPipeline;
    std::unique_ptr<Orange::Rhi::RHITexture>             normalBuffer;     // RGBA8 view-space 法线
    std::uint32_t                                        normalBufferWidth{0};
    std::uint32_t                                        normalBufferHeight{0};
    bool                                                 normalBufferLayoutShaderReadOnly{false};

    std::unique_ptr<Orange::Rhi::RHIPipeline> bloomUpsamplePipeline;
    std::unique_ptr<Orange::Rhi::RHIPipeline> passthroughCombinePipeline;
    std::unique_ptr<Orange::Rhi::RHIPipeline> tonemapPipeline;

    // ---- Shadow pass + Light UBO 资源 -----------------------
    ShadowConfig shadowConfig{};

    // CSM（GAP-2026-05-27-cascaded-shadow-maps）级联上限。shadowConfig.cascadeCount
    // ≤ 本上限；shadowMap 升为 Tex2DArray + per-layer Tex2D depth view（仿
    // spotShadowArray 模式），cascadeCount=1 默认行为退化为单 cascade = 历史
    // 单张 shadow map 行为（零回归安全网）。
    static constexpr std::uint32_t kMaxCascades = 4;

    std::unique_ptr<Orange::Rhi::RHITexture> shadowMap;   // Tex2DArray（layer = cascade）
    std::uint32_t                            shadowMapResolution{0};
    bool                                     shadowMapLayoutShaderReadOnly{false};
    std::array<std::unique_ptr<Orange::Rhi::RHITextureView>, kMaxCascades>
        shadowMapLayerViews;

    // 本帧 cascade 矩阵 + NDC z split 距离 + per-cascade PCSS scale。
    // ComputeCascadeViewProjs 计算，RecordShadowPass 按 layer 渲染消费
    // matrices，UpdateLightUbo 写进 UBO。
    //   * cascadeCount=1 时 [0..3] 全部 = 单 directional ortho 矩阵，
    //     splits 全 1.0 → shader cascade selection 恒返回 0（等价历史单 map）；
    //   * cascadeCount=3/4 时 [0..N-1] 真实视锥分段拟合，splits 是各 cascade
    //     远端 NDC z（pbr.frag 用 gl_FragCoord.z 比较选 cascade）。
    // cascadePcssScales[i] = orthoExtent_0 / orthoExtent_i —— pbr.frag 把
    // pcssLightSize 乘该 scale 以保 world-space 半影宽度跨 cascade 一致。
    std::array<glm::mat4, kMaxCascades> cascadeViewProjs{};
    glm::vec4                           cascadeNdcSplits{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4                           cascadePcssScales{1.0f, 1.0f, 1.0f, 1.0f};

    Asset::AssetHandle<Asset::ShaderAsset> shadowCasterVsHandle;
    Asset::AssetHandle<Asset::ShaderAsset> shadowCasterFsHandle;
    std::unique_ptr<Orange::Rhi::RHIPipeline> shadowCasterPipeline;

    // Light UBO：per-frame 写一次（std140 layout，对齐 16 字节）。
    // CSM additive（GAP-2026-05-27）：cascadeViewProj[0..3] + cascadeNdcSplits
    // 追加到原 160B 末尾；老 shader 只读到 iblFactor 仍 layout-compatible（std140
    // 末尾未引用字段不影响布局）。pbr.frag 是唯一消费新字段的 builtin。
    struct LightUboData
    {
        glm::mat4 lightViewProj;       // = cascadeViewProj[0] backward-compat alias
        glm::vec4 lightDirIntensity;   // xyz = direction, w = intensity
        glm::vec4 lightColor;          // xyz = rgb, w = unused
        glm::vec4 shadowParams;        // x = pcfKernelRadius, y = depthBias, z = pcssLightSize, w pad
        glm::vec4 cameraWorldPos;      // xyz = camera worldPos, w = unused
        glm::vec4 frameInfo;           // x = time（seconds），y/z/w 预留
        glm::vec4 iblFactor;           // xyz = EnvironmentComponent.tint * intensity, w pad
        // ─── CSM additive ─────────────────────────────────────────────────
        glm::mat4 cascadeViewProj[kMaxCascades];   // 256 B；[0..N-1] 实际 cascade
        glm::vec4 cascadeNdcSplits;                // 16 B；x..w = cascade i 远端 NDC z
        glm::vec4 cascadePcssScales;               // 16 B；x..w = orthoExtent_0 / orthoExtent_i（PCSS 半影 world-space 一致）
        glm::vec4 debugFlags;                      // 16 B；x = cascade tint overlay 开关 (1=on/0=off)，y/z/w pad
    };
    static_assert(sizeof(LightUboData) == 64 + 16 * 6 + 64 * kMaxCascades + 16 * 3,
                  "LightUboData std140 size mismatch (expected 464 bytes after CSM additive + debugFlags)");
    std::unique_ptr<Orange::Rhi::RHIBuffer> lightUbo;

    // PointLights UBO（GAP-2026-05-11 G2；GAP-2026-05-26 G3 加 shadowParams）。
    static constexpr std::uint32_t kMaxPointLights = 8;
    struct PointLightStd140
    {
        glm::vec4 posRange{};        // xyz = world pos, w = range
        glm::vec4 colorIntensity{};  // xyz = linear rgb, w = intensity
        glm::vec4 shadowParams{};    // x = cube shadow index（G3；<0 = 无），y/z/w pad
    };
    struct PointLightsUboData
    {
        glm::uvec4       countPad{0u, 0u, 0u, 0u};
        PointLightStd140 lights[kMaxPointLights]{};
    };
    static_assert(sizeof(PointLightsUboData) == 16 + 48 * kMaxPointLights,
                  "PointLightsUboData std140 size mismatch");
    std::unique_ptr<Orange::Rhi::RHIBuffer> pointLightsUbo;

    // SpotLights UBO（GAP-2026-05-26 G1）。锥光 = point 物理基衰减 × 锥角
    // 软边。每盏 4 个 std140 vec4（13 个 float 装不进 3 个 vec4，且第 4 个
    // vec4 给 G2 透视阴影 index 预留位）。
    static constexpr std::uint32_t kMaxSpotLights = 8;
    struct SpotLightStd140
    {
        glm::vec4 posRange{};        // xyz = world pos, w = range
        glm::vec4 dirCosOuter{};     // xyz = spot dir（normalized）, w = cos(outerConeAngle)
        glm::vec4 colorIntensity{};  // xyz = linear rgb, w = intensity
        glm::vec4 cosInnerShadow{};  // x = cos(innerConeAngle), y = shadow index（G2；<0 = 无）, z/w pad
    };
    struct SpotLightsUboData
    {
        glm::uvec4      countPad{0u, 0u, 0u, 0u};
        SpotLightStd140 lights[kMaxSpotLights]{};
    };
    static_assert(sizeof(SpotLightsUboData) == 16 + 64 * kMaxSpotLights,
                  "SpotLightsUboData std140 size mismatch");
    std::unique_ptr<Orange::Rhi::RHIBuffer> spotLightsUbo;

    // ---- Spot shadow（GAP-2026-05-26 G2）：多 shadow caster 透视阴影 -------
    // castsShadow 的 SpotLight 子集（cap kMaxSpotShadowCasters）各占一张
    // D32Float Tex2DArray 的一层；per-layer Tex2D depth view 作 depth
    // attachment 渲 depth-only（复用 shadow_caster pipeline），pbr.frag
    // 按 shadow index 采 sampler2DArray + PCF。directional shadow 仍走独立
    // shadowMap（binding 0），与 spot 阴影各自独立 → 多 caster 不串扰。
    static constexpr std::uint32_t kMaxSpotShadowCasters = 4;
    std::unique_ptr<Orange::Rhi::RHITexture> spotShadowArray;
    std::uint32_t                            spotShadowArrayResolution{0};
    bool                                     spotShadowArrayLayoutShaderReadOnly{false};
    std::array<std::unique_ptr<Orange::Rhi::RHITextureView>, kMaxSpotShadowCasters>
        spotShadowLayerViews;

    // 当前帧 shadow-casting spot 的 light view-proj + 数量：UpdateSpotLightsUbo
    // 计算，RecordSpotShadowPass 消费（渲各 layer）+ 写进 spotShadowUbo 供
    // pbr.frag 采样。索引与 SpotLightStd140.cosInnerShadow.y 对齐。
    std::array<glm::mat4, kMaxSpotShadowCasters> spotShadowMatrices{};
    std::uint32_t                                spotShadowCount{0};

    struct SpotShadowUboData
    {
        glm::uvec4 countPad{0u, 0u, 0u, 0u};
        glm::mat4  lightViewProj[kMaxSpotShadowCasters]{};
    };
    static_assert(sizeof(SpotShadowUboData) == 16 + 64 * kMaxSpotShadowCasters,
                  "SpotShadowUboData std140 size mismatch");
    std::unique_ptr<Orange::Rhi::RHIBuffer> spotShadowUbo;

    // ---- Point shadow（GAP-2026-05-26 G3）：全向 cubemap 阴影 -------------
    // castsShadow 的 PointLight 子集（cap kMaxPointShadowCasters）各占一个
    // 独立的 D32Float TexCube（6 layer）。每 face 用 90° perspective depth-only
    // 渲（复用 shadow_caster pipeline）。pbr.frag 用 samplerCube 按方向采，
    // 比较"从 dominant 轴距离重建的 NDC depth"与采样值。near 全局常量、
    // far = light.range（posRange.w），故无需额外矩阵 UBO —— 仅靠 cube depth
    // + 距离重建。
    //
    // 用 **N 个独立 samplerCube** 而非单个 samplerCubeArray：后者需 Vulkan
    // `imageCubeArray` device feature，OrangeRender 当前未启用（见
    // incoming_feature FEATURE-2026-05-26-enable-image-cube-array）。普通
    // samplerCube 是核心能力（IBL 已在用），无需跨仓改动。
    static constexpr std::uint32_t kMaxPointShadowCasters = 2;
    static constexpr float         kPointShadowNear       = 0.05f;  // 须与 shader 常量一致
    static constexpr std::uint32_t kPointShadowBinding0   = 9;      // 第一个 cube 的 binding
    std::array<std::unique_ptr<Orange::Rhi::RHITexture>, kMaxPointShadowCasters> pointShadowCubes;
    std::uint32_t                            pointShadowCubeResolution{0};
    bool                                     pointShadowCubeLayoutShaderReadOnly{false};
    // per-(cube, face) Tex2D depth view，flat index = cube*6 + face。
    std::array<std::unique_ptr<Orange::Rhi::RHITextureView>, 6u * kMaxPointShadowCasters>
        pointShadowFaceViews;

    // 当前帧 shadow-casting point 的 light world pos + far(=range) + 数量：
    // UpdatePointLightsUbo 算（+ 写 shadowParams.x = cube index），
    // RecordPointShadowPass 消费（构 6 面矩阵渲 depth）。
    std::array<glm::vec3, kMaxPointShadowCasters> pointShadowLightPos{};
    std::array<float, kMaxPointShadowCasters>     pointShadowFar{};
    std::uint32_t                                 pointShadowCount{0};

    // 当前帧时间（seconds，单调递增）。
    float frameTime{0.0f};

    // RequestCapture 路径：pendingCapturePath 在 Render() Stage A 末尾被消费。
    std::optional<std::filesystem::path>     pendingCapturePath;
    std::unique_ptr<Orange::Rhi::RHIBuffer>  captureBuffer;
    std::uint64_t                            captureBufferCapacity{0};

    // Main pass descriptor set —— 所有 per-template pipeline 共用 set 0。
    std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> mainDescLayout;
    std::unique_ptr<Orange::Rhi::RHIDescriptorPool>      mainDescPool;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       mainDescSet;
    bool                                                  mainDescBound{false};

    // ---- set 1 · per-instance material 贴图（GAP-2026-05-25 A2 / G1）-------
    // 仅 PBR 模板（textureSlots 非空）的 pipeline 声明本 set。binding 0=baseColor /
    // 1=normal / 2=metalRough / 3=ao，全 CombinedImageSampler、Fragment stage。
    // 未绑的槽喂 default 贴图（白 / flat-normal）→ 采样 ×scalar = scalar、法线不
    // 扰动 → 没绑贴图时输出与纯 scalar PBR 完全一致（零回归）。
    static constexpr std::uint32_t kMaterialTexBindings = 4;
    static constexpr std::uint32_t kMaxMaterialSets     = 128;
    std::unique_ptr<Orange::Rhi::RHISampler>             materialSampler;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> materialTexLayout;
    std::unique_ptr<Orange::Rhi::RHIDescriptorPool>      materialTexPool;
    std::unique_ptr<Orange::Rhi::RHITexture>             defaultWhiteTex;   // baseColor/MR/AO 缺省
    std::unique_ptr<Orange::Rhi::RHITexture>             defaultNormalTex;  // flat-normal (0.5,0.5,1)
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       defaultMaterialSet;  // 全 default（null instance）
    // 贴图 GPU 缓存：键 = AssetHandle<TextureAsset>::Value()。同贴图跨材质复用。
    std::unordered_map<std::uint64_t, std::unique_ptr<Orange::Rhi::RHITexture>> materialTexCache;
    // per-MaterialInstance descriptor set 缓存 + 签名（4 个 binding 的 handle
    // value）；签名变化（编辑器换贴图）时就地 UpdateDescriptorSet 重建。
    struct MaterialDescEntry
    {
        std::unique_ptr<Orange::Rhi::RHIDescriptorSet> set;
        std::array<std::uint64_t, kMaterialTexBindings> sig{};
    };
    std::unordered_map<const MaterialInstance*, MaterialDescEntry> materialDescCache;

    // pbr.frag/vert 无条件采样 set 1（4 贴图）+ 读 tangent(loc3)。任何 PBR 材质
    // 都必须声明/绑定 set 1 + tangent，否则 shader 采样未绑 descriptor → 管线
    // 非法 → 全黑（GAP-2026-05-25 回归:pbr.template.json 漏填 textureSlots 时
    // 本判定误返 false → 编辑器场景全黑）。因此 gate **不只**看 textureSlots,
    // 还按 PBR push 签名（uMVP+uModel+uBaseColor+uMRA = 160B,仅 pbr 模板有）
    // 兜底——slotless pbr 也声明/绑 set 1（喂 default 贴图,退化为纯 scalar PBR）。
    static bool MaterialUsesTextureSet(const Material& mat) noexcept
    {
        return !mat.textureSlots.empty()
            || PipelineDetail::ComputePushConstantSize(mat) >= 160u;
    }

    // 上传一张 RGBA8 material 贴图到 GPU + 缓存。非 RGBA8 / 缺失 / 失败返
    // nullptr（caller 落 default 贴图）。frame-time 调用（走 UploadContext）。
    Orange::Rhi::RHITexture* EnsureGpuTexture(
        const Asset::AssetHandle<Asset::TextureAsset>& handle)
    {
        if (!handle.IsValid() || assets == nullptr || renderDevice == nullptr || !upload)
        {
            return nullptr;
        }
        if (auto it = materialTexCache.find(handle.Value()); it != materialTexCache.end())
        {
            return it->second.get();
        }
        const auto* asset = assets->Get(handle);
        if (asset == nullptr || asset->Empty() || asset->Width() == 0 || asset->Height() == 0)
        {
            return nullptr;
        }
        if (asset->Format() != Asset::TextureFormat::R8G8B8A8_UNorm)
        {
            ORANGE_LOG_WARN("Pipeline: material 贴图 (handle={}) 非 RGBA8，落 default",
                            static_cast<unsigned long long>(handle.Value()));
            return nullptr;
        }
        auto& rhi = renderDevice->GetRhiDevice();
        Orange::Rhi::TextureDesc t{};
        t.mWidth       = asset->Width();
        t.mHeight      = asset->Height();
        t.mFormat      = Orange::Rhi::TextureFormat::RGBA8Unorm;
        t.mDimension   = Orange::Rhi::TextureDimension::Tex2D;
        t.mArrayLayers = 1u;
        t.mUsage       = Orange::Rhi::TextureUsage::Sampled
                       | Orange::Rhi::TextureUsage::TransferDst;
        auto tex = rhi.CreateTexture(t);
        if (!tex)
        {
            return nullptr;
        }
        const auto& px = asset->Pixels();
        if (Orange::Failed(upload->UploadTexture(*tex, px.data(), px.size())))
        {
            ORANGE_LOG_ERROR("Pipeline: material 贴图 UploadTexture 失败 (handle={})",
                             static_cast<unsigned long long>(handle.Value()));
            return nullptr;
        }
        auto* raw = tex.get();
        materialTexCache.emplace(handle.Value(), std::move(tex));
        return raw;
    }

    // 建 / 复用某 MaterialInstance 的 set 1 descriptor。inst==nullptr 或资源
    // 未就绪 → defaultMaterialSet。签名命中缓存 → 直接返回（不触发 GPU 写，
    // 录制期调用安全）。在 EnsureMaterialDescriptors 预通道里 frame 录制前调
    // 一次确保 build + 上传完成。
    Orange::Rhi::RHIDescriptorSet* EnsureMaterialDescriptorSet(const MaterialInstance* inst)
    {
        if (inst == nullptr || !materialTexLayout || !materialTexPool || renderDevice == nullptr)
        {
            return defaultMaterialSet.get();
        }
        std::array<std::uint64_t, kMaterialTexBindings> sig{};
        for (std::uint32_t b = 0; b < kMaterialTexBindings; ++b)
        {
            sig[b] = inst->GetTextureBinding(b).Value();
        }
        auto it = materialDescCache.find(inst);
        if (it != materialDescCache.end() && it->second.set && it->second.sig == sig)
        {
            return it->second.set.get();
        }

        auto& rhi = renderDevice->GetRhiDevice();
        MaterialDescEntry* entry = nullptr;
        if (it == materialDescCache.end())
        {
            if (materialDescCache.size() >= kMaxMaterialSets)
            {
                return defaultMaterialSet.get();  // 池满兜底
            }
            auto set = rhi.AllocateDescriptorSet(*materialTexPool, *materialTexLayout);
            if (!set)
            {
                return defaultMaterialSet.get();
            }
            entry = &materialDescCache.emplace(inst, MaterialDescEntry{std::move(set), {}}).first->second;
        }
        else
        {
            entry = &it->second;
        }

        Orange::Rhi::RHITexture* defaults[kMaterialTexBindings] = {
            defaultWhiteTex.get(), defaultNormalTex.get(),
            defaultWhiteTex.get(), defaultWhiteTex.get()};
        Orange::Rhi::DescriptorWrite writes[kMaterialTexBindings]{};
        for (std::uint32_t b = 0; b < kMaterialTexBindings; ++b)
        {
            Orange::Rhi::RHITexture* tex = EnsureGpuTexture(inst->GetTextureBinding(b));
            if (tex == nullptr)
            {
                tex = defaults[b];
            }
            writes[b].mBinding             = b;
            writes[b].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            writes[b].mImageInfo.mpTexture = tex;
            writes[b].mImageInfo.mpSampler = materialSampler.get();
        }
        rhi.UpdateDescriptorSet(*entry->set, writes, kMaterialTexBindings);
        entry->sig = sig;
        return entry->set.get();
    }

    // frame 录制前预通道：对所有 PBR drawable 确保 set 1 已 build + 贴图已上传。
    // 录制期 draw loop 再调 EnsureMaterialDescriptorSet 只命中缓存（不触 GPU）。
    void EnsureMaterialDescriptors()
    {
        for (const auto& d : scene.Drawables())
        {
            const Material* mat = (d.materialInstance != nullptr)
                                      ? d.materialInstance->GetMaterial()
                                      : EnsureBuiltinDefaultMaterial();
            if (mat == nullptr || !MaterialUsesTextureSet(*mat))
            {
                continue;
            }
            EnsureMaterialDescriptorSet(d.materialInstance);
        }
    }

    // Dummy IBL 资源：全部 RGBA16Float、1×1 / 1×1×6，启动期 zero-clear。
    std::unique_ptr<Orange::Rhi::RHITexture> dummyIrradianceCube;
    std::unique_ptr<Orange::Rhi::RHITexture> dummyPrefilteredCube;
    std::unique_ptr<Orange::Rhi::RHITexture> dummyBrdfLut;

    // 真实 IBL 烘焙产物（BakeIblFromWorld 出口）。
    std::unique_ptr<Orange::Rhi::RHITexture> bakedIrradianceCube;
    std::unique_ptr<Orange::Rhi::RHITexture> bakedPrefilteredCube;
    std::unique_ptr<Orange::Rhi::RHITexture> bakedBrdfLut;

    // Pipeline 内部记录的 "上次烘焙时 EnvironmentComponent.cubemap 的句柄"。
    ::Orange::Engine::Asset::AssetHandle<::Orange::Engine::Asset::TextureAsset>
        lastBakedCubemap{};

    // 原始 environment cube（BakeEquirectToCube 输出，未经 GGX 卷积）。
    std::unique_ptr<Orange::Rhi::RHITexture> bakedEnvCube;

    // ---- Sky-dome pass GPU 资源 ----------------------------------------
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        skyFs;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> skyLayout;
    std::unique_ptr<Orange::Rhi::RHIPipeline>            skyPipeline;
    std::unique_ptr<Orange::Rhi::RHIDescriptorPool>      skyPool;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       skySet;
    Orange::Rhi::RHITexture*                              skySetBoundCube{nullptr};
    bool                                                  skyEnabled{true};

    // ---- Procedural sky pass GPU 资源 ----------------------------------
    std::unique_ptr<Orange::Rhi::RHIShaderModule> proceduralSkyFs;
    std::unique_ptr<Orange::Rhi::RHIPipeline>     proceduralSkyPipeline;

    // ---- v1.3.0 · AuxPassProvider hook --------------------------------
    // 外部（editor / 游戏端）通过 Pipeline::SetAuxPassProvider 注入，主
    // pass 完成后 Render 内调用 RenderAuxPass。Pipeline 不持所有权 ——
    // 见 IAuxPassProvider.h 调用约定。
    IAuxPassProvider*                                     pAuxPassProvider{nullptr};

    // ---- 引擎托管 ImGui overlay（GAP-2026-05-27-consumer-imgui-tuning-hook）----
    // 游戏侧无 PIE，需要在自己进程里叠即时模式 debug-UI 调参/调试。Pipeline
    // 托管 ImGui context + GLFW/Vulkan backend + descriptor pool；消费者经
    // Layer::OnImGui 提交 widget（经 imguiSubmit 回调驱动）。仅 window 模式
    // 接通；编辑器走自管路径不碰这套。imguiState 完整定义在 PipelineImGui.cpp。
    std::unique_ptr<PipelineImGuiState> imguiState;
    std::function<void()>               imguiSubmit;
    bool                                imguiEnabled{false};

    // 以下三个 helper 的实现都在 PipelineImGui.cpp（唯一接触 imgui / vulkan
    // 头的 TU），声明放这里供 Pipeline.cpp / Pipeline 公共方法转发调用。
    // 签名零 imgui / vulkan 类型，故可在本 header 安全声明。
    Result<void, ResultCode> EnableImGuiImpl();   // EnableImGui 的真正落地
    void                     ShutdownImGuiImpl();  // Shutdown 顶部调（释放 imgui 资源）
    void                     ImGuiBeginFrameAndSubmit();  // Render 顶部调（NewFrame + submit + Render）

    // ---- v1.3.0 · 公共面中性化字段 ------------------------------------
    // engine 默认值彻底中性化（无 ambient fallback + shipping 深蓝灰 clear）；
    // 编辑器侧通过 Pipeline::SetDummyIblAmbient / SetSceneClearColor 公共
    // 接口提到 UX 友好值（Cocos 风灰 + 0.5 ambient）。原 cmake gate
    // `ORANGE_ENGINE_WITH_EDITOR_AUX_PASSES` 对 dummy IBL ambient + 主 pass
    // clear color 的特殊路径已移除（gate 仅剩 grid pass 资源；grid 真迁出
    // 后 gate 整体清退）。
    glm::vec3                                             sceneClearColor{0.05f, 0.07f, 0.10f};
    glm::vec3                                             dummyIblAmbient{0.0f, 0.0f, 0.0f};

    // DebugDrawScene —— v0.9 viewport 调试几何 wrap。
    std::unique_ptr<DebugDrawScene>                       debugDrawScene;

    // -----------------------------------------------------------------

    const Material* EnsureBuiltinDefaultMaterial()
    {
        if (builtinDefaultLoaded)
        {
            return &builtinDefaultMaterial;
        }
        if (assets == nullptr)
        {
            return nullptr;
        }
        builtinDefaultMaterial = BuiltinMaterials::LoadPbr(*assets);
        builtinDefaultLoaded   = true;
        return &builtinDefaultMaterial;
    }

    Orange::Rhi::RHIShaderModule* GetOrCreateShaderModule(
        const Asset::AssetHandle<Asset::ShaderAsset>& handle,
        Orange::Rhi::ShaderStage                       stage,
        const char*                                    debugName)
    {
        if (!handle.IsValid() || assets == nullptr || renderDevice == nullptr)
        {
            return nullptr;
        }
        if (auto it = shaderModules.find(handle.Value()); it != shaderModules.end())
        {
            return it->second.get();
        }
        const auto* asset = assets->Get(handle);
        if (asset == nullptr || asset->Empty())
        {
            ORANGE_LOG_ERROR("Pipeline: ShaderAsset 无效或为空 (handle={})",
                             static_cast<unsigned long long>(handle.Value()));
            return nullptr;
        }
        Orange::Rhi::ShaderModuleDesc desc{};
        desc.mpCode      = asset->SpirV().data();
        desc.mCodeSize   = asset->ByteSize();
        desc.mStage      = stage;
        desc.mpDebugName = debugName;
        auto module = renderDevice->GetRhiDevice().CreateShaderModule(desc);
        if (!module)
        {
            ORANGE_LOG_ERROR("Pipeline: CreateShaderModule 失败 (handle={})",
                             static_cast<unsigned long long>(handle.Value()));
            return nullptr;
        }
        auto* raw = module.get();
        shaderModules.emplace(handle.Value(), std::move(module));
        return raw;
    }

    Orange::Rhi::RHIPipeline* GetOrCompilePipeline(const Material& mat)
    {
        if (auto it = templatePipelines.find(&mat); it != templatePipelines.end())
        {
            return it->second.get();
        }
        if (renderDevice == nullptr)
        {
            return nullptr;
        }
        if (!mat.vertexShader.IsValid() || !mat.fragmentShader.IsValid())
        {
            ORANGE_LOG_ERROR("Pipeline: Material '{}' 的 shader handle 无效，跳过编 pipeline",
                             mat.name);
            return nullptr;
        }

        const std::string vsLabel = "orange_engine.material." + mat.name + ".vert";
        const std::string fsLabel = "orange_engine.material." + mat.name + ".frag";
        auto* vsModule = GetOrCreateShaderModule(mat.vertexShader,
                                                 Orange::Rhi::ShaderStage::Vertex,
                                                 vsLabel.c_str());
        auto* fsModule = GetOrCreateShaderModule(mat.fragmentShader,
                                                 Orange::Rhi::ShaderStage::Fragment,
                                                 fsLabel.c_str());
        if (vsModule == nullptr || fsModule == nullptr)
        {
            return nullptr;
        }

        Orange::Rhi::GraphicsPipelineDesc desc{};
        desc.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,   vsModule, "main"});
        desc.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment, fsModule, "main"});

        // PBR 模板声明 location 3 (tangent)；其余模板不声明（避免 unconsumed
        // attribute validation 警告，stride 仍 48B）。
        PipelineDetail::FillVertexInputLayout(desc, MaterialUsesTextureSet(mat));

        desc.mInputAssembly.mTopology = Orange::Rhi::PrimitiveTopology::TriangleList;
        desc.mRasterizer.mCullMode    = Orange::Rhi::CullMode::Back;
        desc.mRasterizer.mFrontFace   = Orange::Rhi::FrontFace::CounterClockwise;
        desc.mDepthStencil.mDepthTestEnable  = true;
        desc.mDepthStencil.mDepthWriteEnable = true;
        desc.mDepthStencil.mDepthCompareOp   = Orange::Rhi::CompareOp::LessOrEqual;
        desc.mColorBlend.mAttachments.push_back({});
        desc.mRenderTargets.mColorFormats.push_back(PipelineDetail::kHdrColorFormat);
        desc.mRenderTargets.mDepthStencilFormat = Orange::Rhi::TextureFormat::D32Float;

        if (mainDescLayout)
        {
            desc.mDescriptorSetLayouts.push_back(mainDescLayout.get());
        }
        // PBR 模板（含贴图槽）额外声明 set 1 = per-instance material 贴图。
        // 其他模板（textured / toon / ...）只有 set 0，本 layout 不挂。
        if (MaterialUsesTextureSet(mat) && materialTexLayout)
        {
            desc.mDescriptorSetLayouts.push_back(materialTexLayout.get());
        }

        PipelineDetail::FillPushConstantRanges(desc, PipelineDetail::ComputePushConstantSize(mat));

        desc.mpDebugName = nullptr;

        auto pipeline = renderDevice->GetRhiDevice().CreateGraphicsPipeline(desc);
        if (!pipeline)
        {
            ORANGE_LOG_ERROR("Pipeline: CreateGraphicsPipeline 失败 (material='{}')", mat.name);
            return nullptr;
        }
        auto* raw = pipeline.get();
        templatePipelines.emplace(&mat, std::move(pipeline));
        return raw;
    }

    // Initialize 时一次性按 window 查询当前 framebuffer 尺寸；之后由
    // OnResize 推动尺寸更新。
    void SeedExtentFromWindow()
    {
        if (window == nullptr)
        {
            return;
        }
        std::uint32_t w = 0;
        std::uint32_t h = 0;
        window->GetFramebufferSize(w, h);
        pendingWidth  = w;
        pendingHeight = h;
        hdrDirty      = true;
    }

    // 离屏段开始之前先把所有 mesh 上传到 GPU——UploadContext 的内部
    // transient cmd 必须在 offscreenCmd.Begin 之前完成。
    void EnsureMeshGpuCache();

    // 自管 cmd list 跑离屏 HDR 主 pass。
    bool RecordOffscreenPass(const glm::mat4& viewProj, bool loadColor = false);

    // 在已经 Begin 的 offscreenCmd 上追加 6 round downsample + 5 round upsample。
    bool RecordBloomChain(const BloomPass& bloomDesc);

    // 检测当前 chain 是否含 BloomPass / TonemapPass / GodRaysPass / SsaoPass。
    const BloomPass*    FindActiveBloomPass()   const noexcept;
    const TonemapPass*  FindActiveTonemapPass() const noexcept;
    const GodRaysPass*  FindActiveGodRaysPass() const noexcept;
    const SsaoPass*     FindActiveSsaoPass()    const noexcept;

    // 在 sceneDepth 重建（OnResize / 首次）后把 godRaysSet 的 binding 0
    // 重新指向当前 sceneDepth view。
    bool EnsureGodRaysSet();
    bool RecordGodRaysPass(const GodRaysPass& gr, const glm::mat4& viewProj);

    // SSAO：noise + kernel 一次性生成 + ssaoColor target / descriptor set
    // 按 hdr 尺寸重建（OnResize / 首次 / sceneDepth 重建后重绑）。
    bool EnsureSsaoResources();
    // 录制 SSAO：compute AO → ssaoColor，再 4×4 模糊 + 乘法 blend 进 hdrColor。
    // proj 取自当前帧 main camera（重建 view-space + 投回屏幕）。
    bool RecordSsaoPass(const SsaoPass& ssaoDesc, const glm::mat4& proj);

    // SSR：ssrColor target / descriptor set 按 hdr 尺寸重建（sceneDepth /
    // hdrColor 重建后重绑）。
    const SsrPass* FindActiveSsrPass() const noexcept;
    bool EnsureSsrResources();
    // 录制 SSR：射线步进采反射 → ssrColor，再加性 blend 进 hdrColor。
    bool RecordSsrPass(const SsrPass& ssrDesc, const glm::mat4& proj);

    // 接触阴影：屏幕空间向光源 march，乘法 blend 进 hdrColor。viewLightDir =
    // 朝光方向（view space）。无 directional light 时调用方跳过。
    const ContactShadowPass* FindActiveContactShadowPass() const noexcept;
    bool EnsureContactShadowResources();
    bool RecordContactShadowPass(const ContactShadowPass& csDesc,
                                 const glm::mat4& proj, const glm::vec3& viewLightDir);

    // 景深：CoC 圆盘 gather → dofColor → composite 回 hdrColor。proj 用于
    // invProj 重建 view 深度。
    const DofPass* FindActiveDofPass() const noexcept;
    bool EnsureDofResources();
    bool RecordDofPass(const DofPass& dofDesc, const glm::mat4& proj);

    // TAA：jitter + 历史 resolve。ApplyTaaJitter 在 TaaPass 激活时给 proj 叠加
    // per-frame sub-pixel 偏移（否则原样返回）；RecordTaaResolve 在所有 post 之后、
    // tonemap/passthrough 之前调用，curJitteredViewProj = 本帧主 pass 用的
    // jittered viewProj。
    const TaaPass* FindActiveTaaPass() const noexcept;
    glm::mat4 ApplyTaaJitter(const glm::mat4& proj) const;
    bool EnsureTaaResources();
    bool RecordTaaResolve(const TaaPass& taaDesc, const glm::mat4& curJitteredViewProj);

    // 色彩分级：曝光/白平衡/对比/饱和 → gradeColor → composite 回 hdrColor。
    const ColorGradePass* FindActiveColorGradePass() const noexcept;
    bool EnsureColorGradeResources();
    bool RecordColorGradePass(const ColorGradePass& gradeDesc);

    // 相机运动模糊：重投影算屏幕速度 → 沿速度 gather → motionBlurColor →
    // composite 回 hdrColor。curViewProj = 本帧未 jitter 的 viewProj（速度不含
    // TAA 亚像素抖动）；末尾把它存为下帧 prevViewProj。
    const MotionBlurPass* FindActiveMotionBlurPass() const noexcept;
    bool EnsureMotionBlurResources();
    bool RecordMotionBlurPass(const MotionBlurPass& mbDesc, const glm::mat4& curViewProj);

    // 镜头效果：色散 + 暗角 → lensColor → composite 回 hdrColor。无 depth 依赖。
    const LensPass* FindActiveLensPass() const noexcept;
    bool EnsureLensResources();
    bool RecordLensPass(const LensPass& lensDesc);

    // 锐化（CAS 式）：邻域自适应锐化 → sharpColor → composite 回 hdrColor。
    const SharpenPass* FindActiveSharpenPass() const noexcept;
    bool EnsureSharpenResources();
    bool RecordSharpenPass(const SharpenPass& sharpenDesc);

    // ---- PostProcessComponent 消费（V1：find-first 全局）-------------------
    // 每帧渲染前从 world 找 PostProcessComponent（全局单例语义，find-first）→
    // 填充下面的 post* 成员 pass 结构 + 把 PCSS/阴影分辨率灌进 shadowConfig；
    // postComponentActive=true。无组件时 false，FindActive* 退回 chain（保
    // sample/test 等 chain 用法兼容）。bloom/tonemap 不在组件覆盖范围（仍走 chain）。
    void SyncPostProcessFromWorld(Orange::Engine::World& world);
    bool           postComponentActive{false};
    SsaoPass       postSsao{};
    SsrPass        postSsr{};
    ContactShadowPass postContact{};
    DofPass        postDof{};
    TaaPass        postTaa{};
    ColorGradePass postGrade{};
    MotionBlurPass postMotionBlur{};
    LensPass       postLens{};
    SharpenPass    postSharpen{};

    // 法线预通道：normalBuffer 按 hdr 尺寸建 / 重建（供 SSAO / SSR 采真实法线）。
    bool EnsureNormalBuffer();
    // 录制法线预通道：遍历 drawables 把 view-space 法线渲进 normalBuffer。
    // 必须紧贴主 pass 之前调用（复用 sceneDepth 作 scratch depth，见字段注释）。
    bool RecordNormalPrepass(const glm::mat4& viewProj, const glm::mat4& view);

    // bakedEnvCube 重建后分配 / 重写 skySet。
    bool EnsureSkyDescSet();

    // 录制 sky-dome pass（主 pass 之前）。
    bool RecordSkyPass(const glm::mat4& invViewProj, const glm::vec3& cameraPos,
                       const glm::vec3& tint, float intensity);

    // 录制 procedural sky pass（cubemap 不可用时的替代分支）。
    bool RecordProceduralSkyPass(const glm::mat4& invViewProj,
                                 const glm::vec3& cameraPos,
                                 const glm::vec3& sunDir,
                                 const glm::vec3& sunColor,
                                 float            sunIntensity);

    // 按当前 dummyIblAmbient 字段值填 dummy IBL irradiance cube 的所有 face
    // 像素（1×1 per face，half float RGBA）。两条入口：
    //   * Initialize 首次创建 dummy IBL 资源时按字段初值填一次（pBootCmd 复
    //     用 offscreenCmd，Initialize 内部已 Begin，cmd 处于录制中）
    //   * SetDummyIblAmbient 运行时切换值时单独跑一次（pBootCmd = nullptr，
    //     内部自管 cmd.Begin/End/Submit/WaitIdle，必须在帧外调）
    // 失败返 false（typically dummyIrradianceCube 未就绪 / staging buffer
    // 创建失败 / cmd lifecycle 出错），caller 决定是否回退。
    bool FillDummyIblIrradiance(Orange::Rhi::RHICommandList* pBootCmd);

    // 录制 v0.9 debug draw pass。
    bool RecordDebugDrawPass(const glm::mat4& viewProj);

    // 创建 / 重建 bloom 6 张 mip + 描述符 set。
    bool EnsureBloomResources();
    void ReleaseBloomResources();

    // 创建 / 重建 shadow map（Tex2DArray，layer = cascade）+ per-layer view。
    bool EnsureShadowMap();

    // 把场景从 directional light 视角渲到 shadowMap 各 cascade layer
    //（depth-only）。每帧由 ComputeCascadeViewProjs 预先填好 cascadeViewProjs[]，
    // 本函数按 layer loop（cascadeCount 之外的 layer 仍 Clear 到 1.0 避免残留）。
    bool RecordShadowPass(const DirectionalLight* light);

    // 离屏模式专用：HDR → viewportColor 的 passthrough。
    bool RecordPassthroughToViewport();

    // 离屏模式专用：跳过 swap-chain 收尾的 Render 实现。
    void RenderOffscreen(Orange::Engine::World& world);

    // RequestCapture 路径辅助：
    //   * EnsureCaptureBuffer：grow captureBuffer
    //   * RecordCaptureCopy：CopyTextureToBuffer + transition
    //   * FinalizeCapture：WaitIdle 后 Map + ACES tonemap + stb_image_write
    bool EnsureCaptureBuffer();
    bool RecordCaptureCopy(Orange::Rhi::RHICommandList& cmd);
    void FinalizeCapture();

    // 把 light 数据写入 lightUbo。CSM cascade 数据由本函数从成员
    // cascadeViewProjs[] + cascadeNdcSplits 读取（ComputeCascadeViewProjs
    // 已在调用前填好）。lightViewProj 字段写 cascadeViewProjs[0] 作 backward-
    // compat alias，老 shader 仍可读旧字段名。
    void UpdateLightUbo(const DirectionalLight* light,
                        const glm::vec3&        lightWorldDir,
                        const glm::vec3&        cameraWorldPos,
                        const glm::vec3&        iblTintIntensity);

    // 把 World 内挂 PointLight + Transform 的 entity 收集到 PointLightsUbo。
    void UpdatePointLightsUbo(Orange::Engine::World& world);

    // 把 World 内挂 SpotLight + Transform 的 entity 收集到 SpotLightsUbo
    //（pos 由 Transform.position、dir 由 Transform.rotation 派生）。同时为
    // castsShadow 的子集（cap kMaxSpotShadowCasters）算 light view-proj +
    // 分配 shadow index + 写 spotShadowUbo。
    void UpdateSpotLightsUbo(Orange::Engine::World& world);

    // 创建 / 重建 spot shadow Tex2DArray + per-layer depth view。
    bool EnsureSpotShadowArray();

    // 把场景从各 shadow-casting spot 视角渲到 spotShadowArray 对应层
    //（depth-only，复用 shadow_caster pipeline）。
    bool RecordSpotShadowPass();

    // 计算单个 spot 的 perspective light view-proj：view = lookAt(pos,
    // pos+dir)，proj = perspective(2*outerConeAngle, 1, near, range)。
    glm::mat4 ComputeSpotLightViewProj(const glm::vec3& pos,
                                       const glm::vec3& dir,
                                       float            outerConeAngle,
                                       float            range) const;

    // 创建 / 重建 point shadow cube array（6 × kMaxPointShadowCasters layer）
    // + per-face depth view。
    bool EnsurePointShadowCube();

    // 把场景从各 shadow-casting point 视角渲到 pointShadowCube 6 个 face
    //（90° perspective depth-only，复用 shadow_caster pipeline）。
    bool RecordPointShadowPass();

    // 计算单 cascade 的 light view-proj（历史 ±10 ortho box；ComputeCascadeViewProjs
    // 在 cascadeCount=1 时直接复用本函数填所有 slot）。
    glm::mat4 ComputeLightViewProj(const glm::vec3& lightWorldDir) const;

    // CSM：按相机视锥分段拟合每 cascade 的 light view-proj，结果写入成员
    // cascadeViewProjs[] + cascadeNdcSplits + cascadePcssScales。
    //   * cascadeCount=1：退化为 ComputeLightViewProj 旧 ±10 ortho box，
    //     splits 全 1.0、scales 全 1.0（cascade 选择恒返回 0）；
    //   * cascadeCount>1（C2 起 default=3）：practical PSSM 划分（λ=0.5 mix
    //     log+uniform）→ 每段视锥 8 角点变换到 world → light view 空间 AABB
    //     → 手写 Vulkan ortho（z[0,1]+y-flip，不用 glm::ortho 防裁半 frustum）
    //     → texel snap 防 shimmer。cascadeNdcSplits[i] = cascade i 远端在 main
    //     相机投影下的 NDC z（pbr.frag 用 gl_FragCoord.z 比较）。
    //     cascadePcssScales[i] = orthoExtent_0 / orthoExtent_i（PCSS 跨 cascade
    //     一致 world-space 半影宽度）。
    void ComputeCascadeViewProjs(const glm::vec3& lightWorldDir,
                                 const glm::mat4& cameraView,
                                 const glm::mat4& cameraProj);

    // 创建 / 重建 HDR off-screen target。
    bool EnsureHdrTarget()
    {
        if (!hdrDirty && hdrColor &&
            hdrWidth == pendingWidth && hdrHeight == pendingHeight)
        {
            return true;
        }
        if (pendingWidth == 0 || pendingHeight == 0)
        {
            return false;
        }
        if (renderDevice == nullptr || !passthroughSet || !hdrSampler)
        {
            return false;
        }

        renderDevice->WaitIdle();

        Orange::Rhi::TextureDesc t{};
        t.mWidth     = pendingWidth;
        t.mHeight    = pendingHeight;
        t.mFormat    = PipelineDetail::kHdrColorFormat;
        t.mUsage     = Orange::Rhi::TextureUsage::RenderTarget
                     | Orange::Rhi::TextureUsage::Sampled
                     | Orange::Rhi::TextureUsage::TransferSrc;
        auto newTex = renderDevice->GetRhiDevice().CreateTexture(t);
        if (!newTex)
        {
            ORANGE_LOG_ERROR("Pipeline: CreateTexture (HDR off-screen) 失败 ({}x{})",
                             pendingWidth, pendingHeight);
            return false;
        }
        hdrColor = std::move(newTex);
        hdrWidth = pendingWidth;
        hdrHeight = pendingHeight;
        hdrLayoutShaderReadOnly = false;
        hdrDirty = false;

        Orange::Rhi::TextureDesc dt{};
        dt.mWidth  = pendingWidth;
        dt.mHeight = pendingHeight;
        dt.mFormat = Orange::Rhi::TextureFormat::D32Float;
        dt.mUsage  = Orange::Rhi::TextureUsage::DepthStencil
                   | Orange::Rhi::TextureUsage::Sampled;
        auto newDepth = renderDevice->GetRhiDevice().CreateTexture(dt);
        if (!newDepth)
        {
            ORANGE_LOG_ERROR("Pipeline: CreateTexture (scene depth) 失败 ({}x{} D32Float)",
                             pendingWidth, pendingHeight);
            return false;
        }
        sceneDepth = std::move(newDepth);
        sceneDepthLayoutShaderReadOnly = false;

        Orange::Rhi::DescriptorWrite w{};
        w.mBinding             = 0;
        w.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
        w.mImageInfo.mpTexture = hdrColor.get();
        w.mImageInfo.mpSampler = hdrSampler.get();
        renderDevice->GetRhiDevice().UpdateDescriptorSet(*passthroughSet, &w, 1);

        if (bloomMipsReady)
        {
            ReleaseBloomResources();
        }
        return true;
    }

    // 创建 / 重建 viewportColor（离屏 final output）。
    bool EnsureViewportTarget()
    {
        if (!offscreenMode)
        {
            return false;
        }
        if (viewportColor &&
            viewportWidth == pendingWidth && viewportHeight == pendingHeight)
        {
            return true;
        }
        if (pendingWidth == 0 || pendingHeight == 0)
        {
            return false;
        }
        if (renderDevice == nullptr)
        {
            return false;
        }

        renderDevice->WaitIdle();

        Orange::Rhi::TextureDesc t{};
        t.mWidth  = pendingWidth;
        t.mHeight = pendingHeight;
        t.mFormat = PipelineDetail::kSwapchainColorFormat;
        t.mUsage  = Orange::Rhi::TextureUsage::RenderTarget
                  | Orange::Rhi::TextureUsage::Sampled
                  | Orange::Rhi::TextureUsage::TransferSrc;  // 允许像素 readback（DebugReadbackPixel）
        auto newTex = renderDevice->GetRhiDevice().CreateTexture(t);
        if (!newTex)
        {
            ORANGE_LOG_ERROR("Pipeline: CreateTexture (viewport color) 失败 ({}x{})",
                             pendingWidth, pendingHeight);
            return false;
        }
        viewportColor = std::move(newTex);
        viewportWidth  = pendingWidth;
        viewportHeight = pendingHeight;
        viewportLayoutShaderReadOnly = false;
        return true;
    }

    // viewportColor 当前 build 出来的实际尺寸。
    std::uint32_t viewportWidth{0};
    std::uint32_t viewportHeight{0};
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_SRC_RENDER_PIPELINE_PIPELINEIMPL_H
