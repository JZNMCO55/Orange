// VfxSystem 实现：CPU 池化粒子 sim + 单 draw call 实例化绘制。
//
// 公共面用 `void*` 透传 OrangeRender RHI 类型，本 .cpp 单点 reinterpret
// 回 `Orange::Rhi::*` 与 `Orange::Renderer::RenderDevice`。这条隔离线
// 与 PostProcessChain / Pipeline 同——CLAUDE.md "Header isolation" 约束。
//
// per-frame instance buffer 走 FIF slicing：单个 CpuToGpu buffer 切
// `framesInFlight` 块，每帧写 GPU 不在读的那块。粒子总数超过 slot 容量
// 时 sim 不被截断，但溢出部分本帧不参与绘制（warn）。

#include "orange/engine/render/VfxSystem.h"

#include "orange/engine/asset/AssetRegistry.h"
#include "orange/engine/asset/ShaderAsset.h"
#include "orange/engine/core/Log.h"
#include "orange/engine/render/ParticleEmitterComponent.h"
#include "orange/engine/scene/TransformComponent.h"
#include "orange/engine/scene/World.h"

#include "orange/renderer/RenderDevice.h"
#include "orange/rhi/RHI.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace Orange::Engine::Render
{
    namespace
    {

        // 解析 .exe 同目录下的内置 SPIR-V 路径。与 BuiltinMaterials.cpp 同思路
        // （CWD 不可靠，锚定在 .exe 所在目录）。
        std::filesystem::path GetExecutableDir()
        {
#if defined(_WIN32)
            wchar_t     buffer[MAX_PATH];
            const DWORD len = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
            if (len == 0 || len == MAX_PATH)
            {
                return std::filesystem::current_path();
            }
            return std::filesystem::path(std::wstring(buffer, len)).parent_path();
#else
            return std::filesystem::current_path();
#endif
        }

        std::string ResolveShaderPath(const char* relative)
        {
            return (GetExecutableDir() / relative).string();
        }

        // CPU sim 状态——AoS。kind / colorStart / colorEnd 这些"每粒子常量"也
        // 拷一份进来，避免每帧回查 emitter desc（emitter 可能被销毁）。
        struct ParticleSim
        {
            glm::vec2 position{0.0f, 0.0f};
            glm::vec2 velocity{0.0f, 0.0f};
            glm::vec2 gravity{0.0f, 0.0f};
            float     age{0.0f};
            float     lifetime{1.0f};
            glm::vec4 colorStart{1.0f, 1.0f, 1.0f, 1.0f};
            glm::vec4 colorEnd{1.0f, 1.0f, 1.0f, 0.0f};
            float     sizeStart{0.05f};
            float     sizeEnd{0.05f};
        };

        // GPU 端 per-instance 记录（32 字节）。Layout 必须与
        // additive_billboard.vert 的 vertex input attributes 一致。
        struct ParticleInstance
        {
            glm::vec4 posSize; // xy = world pos, z = size, w = age01
            glm::vec4 color;   // rgb = color, a = intensity
        };
        static_assert(sizeof(ParticleInstance) == 32,
                      "ParticleInstance must be 32 bytes to match shader vertex layout.");

        // Push constant —— 与 additive_billboard.vert 的 push_constant block 一致。
        // 80 字节：mat4 viewProj + vec4 timeAspect。
        struct ParticlePushBlock
        {
            glm::mat4 viewProj{1.0f};
            glm::vec4 timeAspect{0.0f, 0.0f, 1.0f, 0.0f};
        };
        static_assert(sizeof(ParticlePushBlock) == 80, "Push block must be 80 bytes.");

        constexpr std::uint32_t kMaxInstancesPerSlot = 16384;
        constexpr std::uint64_t kSliceBytesPerSlot =
            static_cast<std::uint64_t>(kMaxInstancesPerSlot) * sizeof(ParticleInstance);

        // 单 emitter 的 CPU 池 —— `desc` 拷一份，避免 emitter 销毁后池仍被 sim
        // 一帧时回查 component。
        struct ParticlePool
        {
            ParticleEmitterDesc      desc;
            bool                     emitting{true};
            glm::vec2                originXY{0.0f, 0.0f}; // 来自 entity Transform 的 xy 投影
            float                    spawnAccumulator{0.0f};
            std::vector<ParticleSim> particles;
        };

    } // namespace

    struct VfxSystem::Impl
    {
        bool initialized{false};

        // Opaque 端持有的 OrangeRender 句柄。
        Orange::Renderer::RenderDevice* renderDevice{nullptr};
        std::uint32_t                   framesInFlight{0};

        // GPU 资源。
        std::unique_ptr<Orange::Rhi::RHIShaderModule> vsModule;
        std::unique_ptr<Orange::Rhi::RHIShaderModule> fsModule;
        std::unique_ptr<Orange::Rhi::RHIPipeline>     pipeline;
        std::unique_ptr<Orange::Rhi::RHIBuffer>       instanceBuffer;
        std::uint64_t                                 instanceBufferTotalBytes{0};

        // 发射器池——按 entity 索引。entity 销毁后下次 Tick 自动 erase。
        std::unordered_map<Entity, ParticlePool> pools;

        // sim 端 RNG —— 进程内单源，per-frame 视觉确定性不强求；后续若
        // 需要 deterministic playback 再做 per-emitter seeding。
        std::mt19937 rng{0xC0FFEEu};

        float NextUniform(float lo, float hi)
        {
            if (lo >= hi)
            {
                return lo;
            }
            std::uniform_real_distribution<float> dist(lo, hi);
            return dist(rng);
        }

        // 创建 / 重建 pipeline。RGBA16F target，additive blend，无 depth test。
        bool CreatePipeline()
        {
            if (renderDevice == nullptr || vsModule == nullptr || fsModule == nullptr)
            {
                return false;
            }
            Orange::Rhi::GraphicsPipelineDesc pd{};
            pd.mShaderStages.push_back(
                {Orange::Rhi::ShaderStage::Vertex, vsModule.get(), "main"});
            pd.mShaderStages.push_back(
                {Orange::Rhi::ShaderStage::Fragment, fsModule.get(), "main"});

            // Per-instance binding，无 per-vertex stream（vert 用 gl_VertexIndex 合成 quad）。
            Orange::Rhi::VertexBindingDesc binding{};
            binding.mBinding   = 0;
            binding.mStride    = sizeof(ParticleInstance);
            binding.mInputRate = Orange::Rhi::VertexInputRate::Instance;
            pd.mVertexInput.mBindings.push_back(binding);
            pd.mVertexInput.mAttributes.push_back(
                {0, 0, offsetof(ParticleInstance, posSize), Orange::Rhi::VertexFormat::Float32x4});
            pd.mVertexInput.mAttributes.push_back(
                {1, 0, offsetof(ParticleInstance, color), Orange::Rhi::VertexFormat::Float32x4});

            pd.mInputAssembly.mTopology = Orange::Rhi::PrimitiveTopology::TriangleList;
            pd.mRasterizer.mCullMode    = Orange::Rhi::CullMode::None;
            pd.mRasterizer.mFrontFace   = Orange::Rhi::FrontFace::CounterClockwise;

            // 粒子 pass 不写 depth；不开 depth test（additive 与顺序无关）。
            pd.mDepthStencil.mDepthTestEnable  = false;
            pd.mDepthStencil.mDepthWriteEnable = false;

            Orange::Rhi::ColorBlendAttachmentDesc blend{};
            blend.mBlendEnable         = true;
            blend.mSrcColorBlendFactor = Orange::Rhi::BlendFactor::SrcAlpha;
            blend.mDstColorBlendFactor = Orange::Rhi::BlendFactor::One;
            blend.mColorBlendOp        = Orange::Rhi::BlendOp::Add;
            blend.mSrcAlphaBlendFactor = Orange::Rhi::BlendFactor::One;
            blend.mDstAlphaBlendFactor = Orange::Rhi::BlendFactor::One;
            blend.mAlphaBlendOp        = Orange::Rhi::BlendOp::Add;
            blend.mColorWriteMask      = Orange::Rhi::ColorWriteMask::All;
            pd.mColorBlend.mAttachments.push_back(blend);

            pd.mRenderTargets.mColorFormats.push_back(Orange::Rhi::TextureFormat::RGBA16Float);
            // 本 pass 不绑 depth attachment——保持 RenderingDesc 清单与 pipeline
            // 一致，避免 dynamic-rendering format compatibility 失败。

            Orange::Rhi::PushConstantRange range{};
            range.mStage  = Orange::Rhi::ShaderStage::Vertex;
            range.mOffset = 0;
            range.mSize   = sizeof(ParticlePushBlock);
            pd.mPushConstantRanges.push_back(range);

            pd.mpDebugName = "orange_engine.vfx.additive_billboard";

            pipeline = renderDevice->GetRhiDevice().CreateGraphicsPipeline(pd);
            if (pipeline == nullptr)
            {
                ORANGE_LOG_ERROR("VfxSystem: CreateGraphicsPipeline 失败");
                return false;
            }
            return true;
        }

        // 创建 instance buffer（FIF slicing，CpuToGpu）。
        bool CreateInstanceBuffer()
        {
            if (renderDevice == nullptr || framesInFlight == 0)
            {
                return false;
            }
            const std::uint64_t total = kSliceBytesPerSlot * static_cast<std::uint64_t>(framesInFlight);

            Orange::Rhi::BufferDesc desc{};
            desc.mSize        = total;
            desc.mUsage       = Orange::Rhi::BufferUsage::Vertex | Orange::Rhi::BufferUsage::Transfer;
            desc.mMemoryUsage = Orange::Rhi::MemoryUsage::CpuToGpu;
            instanceBuffer    = renderDevice->GetRhiDevice().CreateBuffer(desc);
            if (instanceBuffer == nullptr)
            {
                ORANGE_LOG_ERROR("VfxSystem: CreateBuffer (instance) 失败 ({} bytes)", total);
                return false;
            }
            instanceBufferTotalBytes = total;
            return true;
        }

        // 把当前 emitter 的 desc / origin / emitting 同步到对应池——首次见
        // 到 entity 时建池。
        ParticlePool& EnsurePool(Entity                           e,
                                 const ParticleEmitterComponent&  comp,
                                 const Scene::TransformComponent* tx)
        {
            auto it = pools.find(e);
            if (it == pools.end())
            {
                ParticlePool p{};
                p.desc     = comp.desc;
                p.emitting = comp.emitting;
                p.particles.reserve(comp.desc.maxParticles);
                it = pools.emplace(e, std::move(p)).first;
            }
            else
            {
                it->second.desc     = comp.desc;
                it->second.emitting = comp.emitting;
            }
            if (tx != nullptr)
            {
                it->second.originXY = glm::vec2(tx->position.x, tx->position.y);
            }
            return it->second;
        }

        // 给一个池新 spawn 一个粒子，按 desc 范围采样初值。返回是否真 spawn。
        bool SpawnOne(ParticlePool& p)
        {
            if (p.particles.size() >= p.desc.maxParticles)
            {
                return false;
            }
            ParticleSim s{};
            const float ox = NextUniform(p.desc.spawnOffsetMin.x, p.desc.spawnOffsetMax.x);
            const float oy = NextUniform(p.desc.spawnOffsetMin.y, p.desc.spawnOffsetMax.y);
            s.position     = p.originXY + glm::vec2(ox, oy);
            s.velocity     = glm::vec2(
                NextUniform(p.desc.initialVelocityMin.x, p.desc.initialVelocityMax.x),
                NextUniform(p.desc.initialVelocityMin.y, p.desc.initialVelocityMax.y));
            s.gravity    = p.desc.gravity;
            s.age        = 0.0f;
            s.lifetime   = std::max(0.001f,
                                    NextUniform(p.desc.lifetimeMin, p.desc.lifetimeMax));
            s.colorStart = p.desc.colorStart;
            s.colorEnd   = p.desc.colorEnd;
            s.sizeStart  = p.desc.sizeStart;
            s.sizeEnd    = p.desc.sizeEnd;
            p.particles.emplace_back(s);
            return true;
        }

        // 单池 sim 一帧：先 spawn（按 emissionRate × dt 累加器）、再推进每
        // 个粒子、最后 swap-and-pop 回收 age >= lifetime 的。
        void StepPool(ParticlePool& p, float dt)
        {
            if (dt <= 0.0f)
            {
                return;
            }

            // Spawn 累加器：emissionRate × dt 次。emitting=false 时只推进
            // 已存在粒子（让它们走完寿命）但不再补新。
            if (p.emitting && p.desc.emissionRate > 0.0f)
            {
                p.spawnAccumulator += p.desc.emissionRate * dt;
                while (p.spawnAccumulator >= 1.0f)
                {
                    p.spawnAccumulator -= 1.0f;
                    if (!SpawnOne(p))
                    {
                        // 池满 → 丢弃剩余 spawn 配额（不抢老的，更可预期）。
                        p.spawnAccumulator = 0.0f;
                        break;
                    }
                }
            }

            // 推进所有活粒子。
            for (auto& s : p.particles)
            {
                s.velocity += s.gravity * dt;
                s.position += s.velocity * dt;
                s.age += dt;
            }

            // swap-and-pop 回收死粒子，避免 vector 中部 erase 的 O(n) 平移。
            std::size_t n = p.particles.size();
            for (std::size_t i = 0; i < n;)
            {
                if (p.particles[i].age >= p.particles[i].lifetime)
                {
                    p.particles[i] = p.particles[n - 1];
                    --n;
                }
                else
                {
                    ++i;
                }
            }
            p.particles.resize(n);
        }

        // 把一个粒子打包成 GPU 端 instance 记录——颜色 / 大小按 age01 在 start
        // / end 端点之间线性插值。
        static ParticleInstance Pack(const ParticleSim& s)
        {
            const float     age01 = std::clamp(s.age / std::max(s.lifetime, 0.001f), 0.0f, 1.0f);
            const glm::vec4 col   = glm::mix(s.colorStart, s.colorEnd, age01);
            const float     sz    = glm::mix(s.sizeStart, s.sizeEnd, age01);

            ParticleInstance inst{};
            inst.posSize = glm::vec4(s.position, sz, age01);
            inst.color   = col;
            return inst;
        }
    };

    VfxSystem::VfxSystem() : mpImpl(std::make_unique<Impl>()) {}
    VfxSystem::~VfxSystem()                               = default;
    VfxSystem::VfxSystem(VfxSystem&&) noexcept            = default;
    VfxSystem& VfxSystem::operator=(VfxSystem&&) noexcept = default;

    Result<void, ResultCode> VfxSystem::Initialize(void*                 pRenderDevice,
                                                   std::uint32_t         framesInFlight,
                                                   Asset::AssetRegistry& assets)
    {
        auto& impl = *mpImpl;
        if (impl.initialized)
        {
            return ResultCode::AlreadyInitialized;
        }
        if (pRenderDevice == nullptr || framesInFlight == 0)
        {
            return ResultCode::InvalidArgument;
        }

        impl.renderDevice   = static_cast<Orange::Renderer::RenderDevice*>(pRenderDevice);
        impl.framesInFlight = framesInFlight;

        // 加载内置 SPIR-V 并建 ShaderModule。
        const auto vsPath = ResolveShaderPath("shaders/orange_engine/additive_billboard.vert.spv");
        const auto fsPath = ResolveShaderPath("shaders/orange_engine/additive_billboard.frag.spv");

        auto vsLoad = assets.Load<Asset::ShaderAsset>(vsPath);
        auto fsLoad = assets.Load<Asset::ShaderAsset>(fsPath);
        if (vsLoad.IsErr() || fsLoad.IsErr())
        {
            ORANGE_LOG_ERROR("VfxSystem: 加载内置 additive_billboard SPIR-V 失败");
            impl.renderDevice   = nullptr;
            impl.framesInFlight = 0;
            return ResultCode::IoError;
        }

        auto& rhi = impl.renderDevice->GetRhiDevice();

        auto buildModule =
            [&](const Asset::AssetHandle<Asset::ShaderAsset>& handle,
                Orange::Rhi::ShaderStage                      stage,
                const char*                                   debugName)
            -> std::unique_ptr<Orange::Rhi::RHIShaderModule>
        {
            const auto* asset = assets.Get(handle);
            if (asset == nullptr || asset->Empty())
            {
                return nullptr;
            }
            Orange::Rhi::ShaderModuleDesc desc{};
            desc.mpCode      = asset->SpirV().data();
            desc.mCodeSize   = asset->ByteSize();
            desc.mStage      = stage;
            desc.mpDebugName = debugName;
            return rhi.CreateShaderModule(desc);
        };

        impl.vsModule = buildModule(vsLoad.Value(),
                                    Orange::Rhi::ShaderStage::Vertex,
                                    "orange_engine.vfx.vs");
        impl.fsModule = buildModule(fsLoad.Value(),
                                    Orange::Rhi::ShaderStage::Fragment,
                                    "orange_engine.vfx.fs");
        if (impl.vsModule == nullptr || impl.fsModule == nullptr)
        {
            ORANGE_LOG_ERROR("VfxSystem: CreateShaderModule 失败");
            Shutdown();
            return ResultCode::InternalError;
        }

        if (!impl.CreatePipeline())
        {
            Shutdown();
            return ResultCode::InternalError;
        }
        if (!impl.CreateInstanceBuffer())
        {
            Shutdown();
            return ResultCode::OutOfMemory;
        }

        impl.initialized = true;
        return Result<void, ResultCode>{};
    }

    void VfxSystem::Shutdown()
    {
        auto& impl = *mpImpl;
        impl.instanceBuffer.reset();
        impl.pipeline.reset();
        impl.fsModule.reset();
        impl.vsModule.reset();
        impl.instanceBufferTotalBytes = 0;
        impl.framesInFlight           = 0;
        impl.renderDevice             = nullptr;
        impl.initialized              = false;
    }

    bool VfxSystem::IsInitialized() const noexcept
    {
        return mpImpl && mpImpl->initialized;
    }

    void VfxSystem::Tick(const World& world, float dt)
    {
        auto& impl = *mpImpl;
        if (dt < 0.0f)
        {
            dt = 0.0f;
        }

        // 1) 同步现存 emitter 到池（可能新建池），并 sim 一帧。
        auto& reg  = world.Registry();
        auto  view = reg.view<ParticleEmitterComponent>();
        for (auto e : view)
        {
            const Entity entity = World::FromEntt(e);
            const auto&  comp   = reg.get<ParticleEmitterComponent>(e);
            const auto*  tx     = world.GetComponent<Scene::TransformComponent>(entity);

            ParticlePool& pool = impl.EnsurePool(entity, comp, tx);
            impl.StepPool(pool, dt);
        }

        // 2) 回收已被销毁 entity 的池（component 还在表里但 entity 已 invalid）。
        for (auto it = impl.pools.begin(); it != impl.pools.end();)
        {
            if (!world.IsValid(it->first) || !world.HasComponent<ParticleEmitterComponent>(it->first))
            {
                it = impl.pools.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void VfxSystem::DrawParticles(void* pCmdList,
                                  void* pHdrColorView,
                                  void* /*pDepthView*/,
                                  const float*  viewProjMatrixData,
                                  std::uint64_t frameIndex,
                                  std::uint32_t hdrWidth,
                                  std::uint32_t hdrHeight)
    {
        auto& impl = *mpImpl;
        if (!impl.initialized || pCmdList == nullptr || pHdrColorView == nullptr || viewProjMatrixData == nullptr)
        {
            return;
        }
        if (impl.pools.empty())
        {
            return;
        }

        // 统计本帧总粒子数；空池跳过整段（含 BeginRendering 的开销）。
        std::size_t total = 0;
        for (const auto& [entity, pool] : impl.pools)
        {
            total += pool.particles.size();
        }
        if (total == 0)
        {
            return;
        }

        // FIF slot 决定写哪段 instance buffer。
        const std::uint32_t fifSlot = static_cast<std::uint32_t>(
            frameIndex % static_cast<std::uint64_t>(impl.framesInFlight));
        const std::uint64_t sliceOffset = static_cast<std::uint64_t>(fifSlot) * kSliceBytesPerSlot;

        // 把所有池打平成 instance 数组写进 GPU 不在读的那段 slice。
        auto* pMapped = impl.instanceBuffer->Map();
        if (pMapped == nullptr)
        {
            ORANGE_LOG_ERROR("VfxSystem: instance buffer Map 失败 (frame={})", frameIndex);
            return;
        }
        auto* pSlot = reinterpret_cast<ParticleInstance*>(
            static_cast<std::uint8_t*>(pMapped) + sliceOffset);

        std::uint32_t written = 0;
        for (const auto& [entity, pool] : impl.pools)
        {
            for (const auto& s : pool.particles)
            {
                if (written >= kMaxInstancesPerSlot)
                {
                    ORANGE_LOG_WARN(
                        "VfxSystem: 本帧粒子总数超过 instance buffer 容量 ({}); 截断绘制。",
                        static_cast<unsigned>(kMaxInstancesPerSlot));
                    break;
                }
                pSlot[written++] = Impl::Pack(s);
            }
            if (written >= kMaxInstancesPerSlot)
            {
                break;
            }
        }
        impl.instanceBuffer->Unmap();

        if (written == 0)
        {
            return;
        }

        // 录制 particle pass —— 把 HDR target 从 ShaderReadOnly 翻回
        // ColorAttachment、BeginRendering(LoadOp::Load 保留主 pass 内容)、
        // 设 viewport / pipeline / vertex / push、Draw、EndRendering、
        // 翻回 ShaderReadOnly 让 bloom downsample 接续。
        auto& cmd        = *static_cast<Orange::Rhi::RHICommandList*>(pCmdList);
        auto* hdrView    = static_cast<Orange::Rhi::RHITextureView*>(pHdrColorView);
        auto& hdrTexture = hdrView->GetTexture();

        // 接续 RecordOffscreenPass 的层级：HDR 此时在 ShaderReadOnly（主
        // pass 末尾已 transition）。本 pass 自己负责把它翻回 ColorAttachment
        // 写一遍粒子，再翻回 ShaderReadOnly 让后续 bloom downsample 能采样。
        cmd.TransitionTexture(hdrTexture,
                              Orange::Rhi::TextureLayout::ShaderReadOnly,
                              Orange::Rhi::TextureLayout::ColorAttachment);

        Orange::Rhi::ColorAttachment att{};
        att.mpView   = hdrView;
        att.mLoadOp  = Orange::Rhi::LoadOp::Load; // 保留主 pass 写入
        att.mStoreOp = Orange::Rhi::StoreOp::Store;

        Orange::Rhi::RenderingDesc rd{};
        rd.mRenderArea.mWidth  = hdrWidth;
        rd.mRenderArea.mHeight = hdrHeight;
        rd.mColorAttachments.push_back(att);
        // 不绑 depth attachment —— pipeline 也声明无 depth target，dynamic-
        // rendering format compatibility 一致。

        cmd.BeginRendering(rd);

        Orange::Rhi::RHIViewport vp{};
        vp.mWidth    = static_cast<float>(hdrWidth);
        vp.mHeight   = static_cast<float>(hdrHeight);
        vp.mMinDepth = 0.0f;
        vp.mMaxDepth = 1.0f;
        cmd.SetViewport(vp);
        Orange::Rhi::RHIScissor sc{};
        sc.mWidth  = hdrWidth;
        sc.mHeight = hdrHeight;
        cmd.SetScissor(sc);

        cmd.BindGraphicsPipeline(*impl.pipeline);
        cmd.BindVertexBuffer(0, *impl.instanceBuffer, sliceOffset);

        ParticlePushBlock push{};
        std::memcpy(&push.viewProj, viewProjMatrixData, sizeof(glm::mat4));
        push.timeAspect = glm::vec4(0.0f, 0.0f,
                                    hdrHeight > 0 ? static_cast<float>(hdrWidth) / static_cast<float>(hdrHeight)
                                                  : 1.0f,
                                    0.0f);
        cmd.SetPushConstants(Orange::Rhi::ShaderStage::Vertex, 0,
                             sizeof(ParticlePushBlock), &push);

        cmd.Draw(/*vertexCount=*/6, /*instanceCount=*/written,
                 /*firstVertex=*/0, /*firstInstance=*/0);

        cmd.EndRendering();

        // 把 HDR 翻回 ShaderReadOnly，恢复 RecordOffscreenPass 末尾的不变量
        // ——下一段 bloom downsample 直接以 sampled 状态消费这张图。
        cmd.TransitionTexture(hdrTexture,
                              Orange::Rhi::TextureLayout::ColorAttachment,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
    }

    std::size_t VfxSystem::TotalLiveParticleCount() const noexcept
    {
        if (!mpImpl)
        {
            return 0;
        }
        std::size_t total = 0;
        for (const auto& [entity, pool] : mpImpl->pools)
        {
            total += pool.particles.size();
        }
        return total;
    }

    std::size_t VfxSystem::LiveParticleCount(Entity emitterEntity) const noexcept
    {
        if (!mpImpl)
        {
            return 0;
        }
        auto it = mpImpl->pools.find(emitterEntity);
        return (it == mpImpl->pools.end()) ? 0 : it->second.particles.size();
    }

} // namespace Orange::Engine::Render
