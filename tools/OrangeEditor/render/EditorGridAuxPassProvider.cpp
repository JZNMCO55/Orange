// EditorGridAuxPassProvider —— 编辑器 viewport 地面 grid pass 的
// IAuxPassProvider 实现。完整 contract 与背景在 .h 头注释。
//
// 本文件按 RAII 模板实现：构造仅置空 Impl；Initialize 创建 shader / layout
// / pipeline / pool / set；Shutdown 反序释放；析构兜底调 Shutdown。
// RenderAuxPass 复刻原 PipelineGrid.cpp::RecordGridPass 的渲染逻辑（hdrColor
// alpha-blend / 自采 sceneDepth / 不挂 depth attachment / 大三角形 vertex
// shader + push 128B 矩阵）—— 与原实现行为字字对齐。

#include "EditorGridAuxPassProvider.h"

#include <orange/engine/core/Log.h>

#include <orange/renderer/RenderDevice.h>
#include <orange/rhi/RHI.h>
#include <orange/rhi/RHICommandList.h>
#include <orange/rhi/RHIDescriptor.h>
#include <orange/rhi/RHIPipeline.h>
#include <orange/rhi/RHIShaderModule.h>
#include <orange/rhi/RHITexture.h>
#include <orange/rhi/RHITypes.h>

#include <glm/mat4x4.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#if defined(_WIN32)
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

namespace
{

// 解析当前 .exe 所在目录 —— shader spv 路径相对 .exe 解析（与
// OrangeEngine 内置 shader 同款约定）。
std::filesystem::path GetExecutableDir()
{
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    const DWORD len = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
    {
        return std::filesystem::current_path();
    }
    return std::filesystem::path{std::wstring_view{buffer, len}}.parent_path();
#else
    return std::filesystem::current_path();
#endif
}

std::vector<std::uint32_t> LoadSpirv(const char* relativePath)
{
    const auto fullPath = (GetExecutableDir() / relativePath).string();
    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (!file)
    {
        ORANGE_LOG_ERROR("EditorGridAuxPassProvider: 无法打开 SPIR-V {}", fullPath);
        return {};
    }
    const std::streamsize size = file.tellg();
    if (size <= 0 || (size % 4) != 0)
    {
        ORANGE_LOG_ERROR("EditorGridAuxPassProvider: SPIR-V 大小非法 ({}) for {}",
                         static_cast<long long>(size), fullPath);
        return {};
    }
    std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), size);
    return words;
}

}  // namespace

struct EditorGridAuxPassProvider::Impl
{
    Orange::Renderer::RenderDevice*                       pDevice{nullptr};
    std::unique_ptr<Orange::Rhi::RHIShaderModule>         vs;
    std::unique_ptr<Orange::Rhi::RHIShaderModule>         fs;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSetLayout>  layout;
    std::unique_ptr<Orange::Rhi::RHIPipeline>             pipeline;
    std::unique_ptr<Orange::Rhi::RHIDescriptorPool>       pool;
    std::unique_ptr<Orange::Rhi::RHIDescriptorSet>        set;
    // descriptor set 内 binding 0 当前指向的 sceneDepth；与 Pipeline 内
    // sceneDepth 重建（OnResize）对齐重写。同 PipelineGrid.cpp 原版 lazy
    // realloc / per-frame UpdateDescriptorSet 节奏。
    Orange::Rhi::RHITexture*                              setBoundDepth{nullptr};
};

EditorGridAuxPassProvider::EditorGridAuxPassProvider()
    : mpImpl(std::make_unique<Impl>())
{
}

EditorGridAuxPassProvider::~EditorGridAuxPassProvider()
{
    Shutdown();
}

bool EditorGridAuxPassProvider::IsInitialized() const noexcept
{
    return mpImpl && mpImpl->pipeline != nullptr;
}

bool EditorGridAuxPassProvider::Initialize(Orange::Renderer::RenderDevice& device)
{
    if (!mpImpl) { return false; }
    if (mpImpl->pipeline != nullptr)
    {
        ORANGE_LOG_WARN("EditorGridAuxPassProvider::Initialize: 重复初始化");
        return true;
    }

    mpImpl->pDevice = &device;
    auto& rhi = device.GetRhiDevice();

    auto vsCode = LoadSpirv("shaders/orange_editor/fullscreen.vert.spv");
    auto fsCode = LoadSpirv("shaders/orange_editor/grid.frag.spv");
    if (vsCode.empty() || fsCode.empty())
    {
        ORANGE_LOG_ERROR("EditorGridAuxPassProvider::Initialize: shader spv 加载失败");
        Shutdown();
        return false;
    }

    {
        Orange::Rhi::ShaderModuleDesc sm{};
        sm.mStage      = Orange::Rhi::ShaderStage::Vertex;
        sm.mpCode      = vsCode.data();
        sm.mCodeSize   = vsCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_editor.fullscreen.vert";
        mpImpl->vs = rhi.CreateShaderModule(sm);

        sm.mStage      = Orange::Rhi::ShaderStage::Fragment;
        sm.mpCode      = fsCode.data();
        sm.mCodeSize   = fsCode.size() * sizeof(std::uint32_t);
        sm.mpDebugName = "orange_editor.grid.frag";
        mpImpl->fs = rhi.CreateShaderModule(sm);

        if (!mpImpl->vs || !mpImpl->fs)
        {
            ORANGE_LOG_ERROR("EditorGridAuxPassProvider::Initialize: ShaderModule 创建失败");
            Shutdown();
            return false;
        }
    }

    {
        // descriptor set layout: 1 binding samplerCube/sampler2D sceneDepth
        Orange::Rhi::DescriptorSetLayoutDesc layDesc{};
        layDesc.mBindings.push_back({0,
                                     Orange::Rhi::DescriptorType::CombinedImageSampler,
                                     1,
                                     Orange::Rhi::ShaderStage::Fragment});
        layDesc.mpDebugName = "orange_editor.grid.layout";
        mpImpl->layout = rhi.CreateDescriptorSetLayout(layDesc);
        if (!mpImpl->layout)
        {
            ORANGE_LOG_ERROR("EditorGridAuxPassProvider::Initialize: DescriptorSetLayout 创建失败");
            Shutdown();
            return false;
        }
    }

    {
        // grid pipeline: RGBA16F HDR target with alpha blend (over)，**无
        // depth attachment**；grid shader 自采 sceneDepth + 手动比较 +
        // discard 处理几何遮挡。push 128B (mat4 invVP + mat4 VP)。
        Orange::Rhi::GraphicsPipelineDesc d{};
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Vertex,
                                   mpImpl->vs.get(), "main"});
        d.mShaderStages.push_back({Orange::Rhi::ShaderStage::Fragment,
                                   mpImpl->fs.get(), "main"});
        d.mInputAssembly.mTopology        = Orange::Rhi::PrimitiveTopology::TriangleList;
        d.mRasterizer.mCullMode           = Orange::Rhi::CullMode::None;
        d.mDepthStencil.mDepthTestEnable  = false;
        d.mDepthStencil.mDepthWriteEnable = false;

        Orange::Rhi::ColorBlendAttachmentDesc blend{};
        blend.mBlendEnable         = true;
        blend.mSrcColorBlendFactor = Orange::Rhi::BlendFactor::SrcAlpha;
        blend.mDstColorBlendFactor = Orange::Rhi::BlendFactor::OneMinusSrcAlpha;
        blend.mColorBlendOp        = Orange::Rhi::BlendOp::Add;
        blend.mSrcAlphaBlendFactor = Orange::Rhi::BlendFactor::One;
        blend.mDstAlphaBlendFactor = Orange::Rhi::BlendFactor::OneMinusSrcAlpha;
        blend.mAlphaBlendOp        = Orange::Rhi::BlendOp::Add;
        d.mColorBlend.mAttachments.push_back(blend);

        // 与 Pipeline 内 HDR target 格式必须一致（RGBA16Float）
        d.mRenderTargets.mColorFormats.push_back(Orange::Rhi::TextureFormat::RGBA16Float);
        d.mDescriptorSetLayouts.push_back(mpImpl->layout.get());

        Orange::Rhi::PushConstantRange pcRange{};
        pcRange.mStage  = Orange::Rhi::ShaderStage::Fragment;
        pcRange.mOffset = 0;
        pcRange.mSize   = 128;  // mat4(64) + mat4(64)
        d.mPushConstantRanges.push_back(pcRange);

        d.mpDebugName = "orange_editor.grid";
        mpImpl->pipeline = rhi.CreateGraphicsPipeline(d);
        if (!mpImpl->pipeline)
        {
            ORANGE_LOG_ERROR("EditorGridAuxPassProvider::Initialize: pipeline 创建失败");
            Shutdown();
            return false;
        }
    }

    return true;
}

void EditorGridAuxPassProvider::Shutdown()
{
    if (!mpImpl) { return; }
    // 反序释放（Vulkan handle 依赖：set 引 layout / pool；pipeline 引 layout +
    // shader；layout 独立；shader 独立）。
    mpImpl->set.reset();
    mpImpl->pool.reset();
    mpImpl->pipeline.reset();
    mpImpl->layout.reset();
    mpImpl->fs.reset();
    mpImpl->vs.reset();
    mpImpl->setBoundDepth = nullptr;
    mpImpl->pDevice = nullptr;
}

void EditorGridAuxPassProvider::RenderAuxPass(Orange::Engine::Render::AuxPassContext& ctx)
{
    if (!mEnabled) { return; }
    if (!mpImpl || mpImpl->pipeline == nullptr || mpImpl->layout == nullptr)
    {
        return;
    }
    if (ctx.pCmd == nullptr || ctx.pHdrColor == nullptr
        || ctx.pSceneDepth == nullptr || ctx.pHdrSampler == nullptr)
    {
        return;
    }
    if (mpImpl->pDevice == nullptr)
    {
        return;
    }
    auto& rhi = mpImpl->pDevice->GetRhiDevice();
    auto& cmd = *ctx.pCmd;

    // 1. descriptor pool + set lazy 创建（池 + set 一次性 alloc）。Pipeline
    //    端 sceneDepth 重建（OnResize）会让指针变 —— 走 UpdateDescriptorSet
    //    重写 binding，不重 alloc set（避免 OUT_OF_POOL_MEMORY）。
    if (!mpImpl->pool)
    {
        Orange::Rhi::DescriptorPoolDesc poolDesc{};
        poolDesc.mMaxSets = 1;
        Orange::Rhi::DescriptorPoolSize sz{};
        sz.mType  = Orange::Rhi::DescriptorType::CombinedImageSampler;
        sz.mCount = 1;
        poolDesc.mPoolSizes.push_back(sz);
        poolDesc.mpDebugName = "orange_editor.grid.pool";
        mpImpl->pool = rhi.CreateDescriptorPool(poolDesc);
        if (!mpImpl->pool)
        {
            ORANGE_LOG_ERROR("EditorGridAuxPassProvider: CreateDescriptorPool 失败");
            return;
        }
    }
    if (!mpImpl->set)
    {
        auto s = rhi.AllocateDescriptorSet(*mpImpl->pool, *mpImpl->layout);
        if (!s)
        {
            ORANGE_LOG_ERROR("EditorGridAuxPassProvider: AllocateDescriptorSet 失败");
            return;
        }
        mpImpl->set = std::move(s);
        mpImpl->setBoundDepth = nullptr;  // 强制走下面 UpdateDescriptorSet
    }
    if (mpImpl->setBoundDepth != ctx.pSceneDepth)
    {
        Orange::Rhi::DescriptorWrite w{};
        w.mBinding             = 0;
        w.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
        w.mImageInfo.mpTexture = ctx.pSceneDepth;
        w.mImageInfo.mpSampler = ctx.pHdrSampler;
        rhi.UpdateDescriptorSet(*mpImpl->set, &w, 1);
        mpImpl->setBoundDepth = ctx.pSceneDepth;
    }

    // 2. Pipeline 已经在 hook 前置阶段把 sceneDepth 翻成 ShaderReadOnly（v1.3.0
    //    contract），本 provider 无需 transition depth。

    // 3. hdrColor: ShaderReadOnly → ColorAttachment 准备 alpha-blend 上 grid。
    cmd.TransitionTexture(*ctx.pHdrColor,
                          Orange::Rhi::TextureLayout::ShaderReadOnly,
                          Orange::Rhi::TextureLayout::ColorAttachment);

    Orange::Rhi::ColorAttachment colorAtt{};
    colorAtt.mpView   = ctx.pHdrColor->GetDefaultView();
    colorAtt.mLoadOp  = Orange::Rhi::LoadOp::Load;
    colorAtt.mStoreOp = Orange::Rhi::StoreOp::Store;

    Orange::Rhi::RenderingDesc rd{};
    rd.mRenderArea.mWidth  = ctx.hdrWidth;
    rd.mRenderArea.mHeight = ctx.hdrHeight;
    rd.mColorAttachments.push_back(colorAtt);
    // 不挂 depth attachment —— shader 自采 sceneDepth + 手动比较 + discard。

    cmd.BeginRendering(rd);

    Orange::Rhi::RHIViewport vp{};
    vp.mWidth    = static_cast<float>(ctx.hdrWidth);
    vp.mHeight   = static_cast<float>(ctx.hdrHeight);
    vp.mMinDepth = 0.0f;
    vp.mMaxDepth = 1.0f;
    cmd.SetViewport(vp);
    Orange::Rhi::RHIScissor sc{};
    sc.mWidth  = ctx.hdrWidth;
    sc.mHeight = ctx.hdrHeight;
    cmd.SetScissor(sc);

    cmd.BindGraphicsPipeline(*mpImpl->pipeline);
    cmd.SetDescriptorSet(0, *mpImpl->set);

    struct GridPush
    {
        glm::mat4 invViewProj;
        glm::mat4 viewProj;
    };
    static_assert(sizeof(GridPush) == 128,
                  "GridPush must match grid.frag push_constant block (128 B).");

    GridPush push{};
    push.invViewProj = ctx.invViewProj;
    push.viewProj    = ctx.viewProj;
    cmd.SetPushConstants(Orange::Rhi::ShaderStage::Fragment, 0,
                         sizeof(GridPush), &push);

    cmd.Draw(3, 1, 0, 0);
    cmd.EndRendering();

    // 4. hdrColor 翻回 ShaderReadOnly（hook 离开契约：下游 debug draw / 后
    //    处理按此假设跑）。
    cmd.TransitionTexture(*ctx.pHdrColor,
                          Orange::Rhi::TextureLayout::ColorAttachment,
                          Orange::Rhi::TextureLayout::ShaderReadOnly);
}
