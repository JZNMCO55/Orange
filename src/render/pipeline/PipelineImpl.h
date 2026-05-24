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

struct Pipeline::Impl
{
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
    std::unique_ptr<Orange::Rhi::RHIPipeline> bloomUpsamplePipeline;
    std::unique_ptr<Orange::Rhi::RHIPipeline> passthroughCombinePipeline;
    std::unique_ptr<Orange::Rhi::RHIPipeline> tonemapPipeline;

    // ---- Shadow pass + Light UBO 资源 -----------------------
    ShadowConfig shadowConfig{};

    std::unique_ptr<Orange::Rhi::RHITexture> shadowMap;
    std::uint32_t                            shadowMapResolution{0};
    bool                                     shadowMapLayoutShaderReadOnly{false};

    Asset::AssetHandle<Asset::ShaderAsset> shadowCasterVsHandle;
    Asset::AssetHandle<Asset::ShaderAsset> shadowCasterFsHandle;
    std::unique_ptr<Orange::Rhi::RHIPipeline> shadowCasterPipeline;

    // Light UBO：per-frame 写一次（std140 layout，对齐 16 字节）。
    struct LightUboData
    {
        glm::mat4 lightViewProj;
        glm::vec4 lightDirIntensity;  // xyz = direction, w = intensity
        glm::vec4 lightColor;         // xyz = rgb, w = unused
        glm::vec4 shadowParams;       // x = pcfKernelRadius, y = depthBias, z/w pad
        glm::vec4 cameraWorldPos;     // xyz = camera worldPos（rim/spec 类 shader 取 viewDir）, w = unused
        glm::vec4 frameInfo;          // x = time（seconds），y/z/w 预留（deltaTime / frameCount / vsyncFps）
        glm::vec4 iblFactor;          // xyz = EnvironmentComponent.tint * intensity, w pad；PBR shader IBL 段乘子
    };
    static_assert(sizeof(LightUboData) == 64 + 16 * 6,
                  "LightUboData std140 size mismatch (expected 160 bytes)");
    std::unique_ptr<Orange::Rhi::RHIBuffer> lightUbo;

    // PointLights UBO（GAP-2026-05-11 G2）。
    static constexpr std::uint32_t kMaxPointLights = 8;
    struct PointLightStd140
    {
        glm::vec4 posRange{};
        glm::vec4 colorIntensity{};
    };
    struct PointLightsUboData
    {
        glm::uvec4       countPad{0u, 0u, 0u, 0u};
        PointLightStd140 lights[kMaxPointLights]{};
    };
    static_assert(sizeof(PointLightsUboData) == 16 + 32 * kMaxPointLights,
                  "PointLightsUboData std140 size mismatch");
    std::unique_ptr<Orange::Rhi::RHIBuffer> pointLightsUbo;

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

    // ---- Grid pass GPU 资源 --------------------------------------------
    std::unique_ptr<Orange::Rhi::RHIShaderModule>        gridFs;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout> gridLayout;
    std::unique_ptr<Orange::Rhi::RHIPipeline>            gridPipeline;
    std::unique_ptr<Orange::Rhi::RHIDescriptorPool>      gridPool;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>       gridSet;
    Orange::Rhi::RHITexture*                              gridSetBoundDepth{nullptr};
    bool                                                  editorGridEnabled{false};

    // ---- v1.3.0 · AuxPassProvider hook --------------------------------
    // 外部（editor / 游戏端）通过 Pipeline::SetAuxPassProvider 注入，主
    // pass 完成后 Render 内调用 RenderAuxPass。Pipeline 不持所有权 ——
    // 见 IAuxPassProvider.h 调用约定。
    IAuxPassProvider*                                     pAuxPassProvider{nullptr};

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

        PipelineDetail::FillVertexInputLayout(desc);

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

    // 检测当前 chain 是否含 BloomPass / TonemapPass / GodRaysPass。
    const BloomPass*    FindActiveBloomPass()   const noexcept;
    const TonemapPass*  FindActiveTonemapPass() const noexcept;
    const GodRaysPass*  FindActiveGodRaysPass() const noexcept;

    // 在 sceneDepth 重建（OnResize / 首次）后把 godRaysSet 的 binding 0
    // 重新指向当前 sceneDepth view。
    bool EnsureGodRaysSet();
    bool RecordGodRaysPass(const GodRaysPass& gr, const glm::mat4& viewProj);

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

    // 录制编辑器地面 grid pass（主 pass 之后、bloom 之前）。
    bool RecordGridPass(const glm::mat4& invViewProj, const glm::mat4& viewProj);

    // 录制 v0.9 debug draw pass。
    bool RecordDebugDrawPass(const glm::mat4& viewProj);

    // 创建 / 重建 bloom 6 张 mip + 描述符 set。
    bool EnsureBloomResources();
    void ReleaseBloomResources();

    // 创建 / 重建 shadow map。
    bool EnsureShadowMap();

    // 把场景从 light 视角渲到 shadow map（depth-only）。
    bool RecordShadowPass(const DirectionalLight* light, const glm::mat4& lightViewProj);

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

    // 把 light 数据写入 lightUbo。
    void UpdateLightUbo(const DirectionalLight* light,
                        const glm::vec3&        lightWorldDir,
                        const glm::mat4&        lightViewProj,
                        const glm::vec3&        cameraWorldPos,
                        const glm::vec3&        iblTintIntensity);

    // 把 World 内挂 PointLight + Transform 的 entity 收集到 PointLightsUbo。
    void UpdatePointLightsUbo(Orange::Engine::World& world);

    // 计算 light view-proj。
    glm::mat4 ComputeLightViewProj(const glm::vec3& lightWorldDir) const;

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
        gridSetBoundDepth = nullptr;

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
                  | Orange::Rhi::TextureUsage::Sampled;
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
