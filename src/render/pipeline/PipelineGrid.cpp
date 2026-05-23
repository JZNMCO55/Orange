// Pipeline 的 grid 系列实现：RecordGridPass。地面网格 overlay；shader 自
// 采 sceneDepth + 手动比较 + discard，比 gl_FragDepth 路径稳。alpha-blend
// over hdrColor，不挂 depth attachment。

#include "PipelineImpl.h"

#include "orange/engine/core/Profiler.h"

namespace Orange::Engine::Render
{

bool Pipeline::Impl::RecordGridPass(const glm::mat4& invViewProj,
                                    const glm::mat4& viewProj)
{
    ORANGE_PROFILE_SCOPE("Grid");
    if (offscreenCmd == nullptr || hdrColor == nullptr || sceneDepth == nullptr
        || gridPipeline == nullptr || gridLayout == nullptr || hdrSampler == nullptr)
    {
        return false;
    }

    auto& rhi = renderDevice->GetRhiDevice();
    auto& cmd = *offscreenCmd;

    // 1. grid descriptor set lazy create + re-bind sceneDepth：
    //    池 + set 都只 alloc 一次（避免 ImGui viewport 启动期 N 次 resize 让
    //    sceneDepth 反复重建 → reset+realloc 撞 OUT_OF_POOL_MEMORY，Vulkan
    //    默认 pool 不支持 free 单 set）。sceneDepth 重建只走 UpdateDescriptorSet
    //    重写 binding，Pipeline::Render 末尾 WaitIdle 保证前帧 set 不再 in-flight。
    if (!gridPool)
    {
        Orange::Rhi::DescriptorPoolDesc poolDesc{};
        poolDesc.mMaxSets = 1;
        Orange::Rhi::DescriptorPoolSize sz{};
        sz.mType  = Orange::Rhi::DescriptorType::CombinedImageSampler;
        sz.mCount = 1;
        poolDesc.mPoolSizes.push_back(sz);
        poolDesc.mpDebugName = "orange_engine.grid.pool";
        gridPool = rhi.CreateDescriptorPool(poolDesc);
        if (!gridPool)
        {
            ORANGE_LOG_ERROR("Pipeline: grid CreateDescriptorPool 失败");
            return false;
        }
    }
    if (!gridSet)
    {
        auto set = rhi.AllocateDescriptorSet(*gridPool, *gridLayout);
        if (!set)
        {
            ORANGE_LOG_ERROR("Pipeline: grid AllocateDescriptorSet 失败");
            return false;
        }
        gridSet = std::move(set);
        gridSetBoundDepth = nullptr;  // 强制下面走 UpdateDescriptorSet
    }
    if (gridSetBoundDepth != sceneDepth.get())
    {
        Orange::Rhi::DescriptorWrite w{};
        w.mBinding             = 0;
        w.mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
        w.mImageInfo.mpTexture = sceneDepth.get();
        w.mImageInfo.mpSampler = hdrSampler.get();
        rhi.UpdateDescriptorSet(*gridSet, &w, 1);
        gridSetBoundDepth = sceneDepth.get();
    }

    // 2. sceneDepth → ShaderReadOnly 供采样。主 pass 末尾留在 DSA；
    //    god rays（如果启用）已翻 ShaderReadOnly。
    if (!sceneDepthLayoutShaderReadOnly)
    {
        cmd.TransitionTexture(*sceneDepth,
                              Orange::Rhi::TextureLayout::DepthStencilAttachment,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
        sceneDepthLayoutShaderReadOnly = true;
    }

    // 3. hdrColor: ShaderReadOnly → ColorAttachment 准备 alpha-blend 上 grid。
    cmd.TransitionTexture(*hdrColor,
                          Orange::Rhi::TextureLayout::ShaderReadOnly,
                          Orange::Rhi::TextureLayout::ColorAttachment);

    Orange::Rhi::ColorAttachment colorAtt{};
    colorAtt.mpView   = hdrColor->GetDefaultView();
    colorAtt.mLoadOp  = Orange::Rhi::LoadOp::Load;
    colorAtt.mStoreOp = Orange::Rhi::StoreOp::Store;

    Orange::Rhi::RenderingDesc rd{};
    rd.mRenderArea.mWidth  = hdrWidth;
    rd.mRenderArea.mHeight = hdrHeight;
    rd.mColorAttachments.push_back(colorAtt);
    // 不挂 depth attachment —— shader 自己采 sceneDepth + 手动比较 + discard。

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

    cmd.BindGraphicsPipeline(*gridPipeline);
    cmd.SetDescriptorSet(0, *gridSet);

    struct GridPush
    {
        glm::mat4 invViewProj;
        glm::mat4 viewProj;
    };
    static_assert(sizeof(GridPush) == 128,
                  "GridPush must match grid.frag push_constant block (128 B).");

    GridPush push{};
    push.invViewProj = invViewProj;
    push.viewProj    = viewProj;
    cmd.SetPushConstants(Orange::Rhi::ShaderStage::Fragment, 0,
                         sizeof(GridPush), &push);

    cmd.Draw(3, 1, 0, 0);
    cmd.EndRendering();

    // 4. hdrColor 翻回 ShaderReadOnly：下一段（bloom / passthrough / tonemap）
    //    按 ShaderReadOnly 假设跑。sceneDepth 留在 ShaderReadOnly —— 下一
    //    帧主 pass 入口看 sceneDepthLayoutShaderReadOnly == true 会正确
    //    transition 回 DSA。
    cmd.TransitionTexture(*hdrColor,
                          Orange::Rhi::TextureLayout::ColorAttachment,
                          Orange::Rhi::TextureLayout::ShaderReadOnly);
    hdrLayoutShaderReadOnly = true;
    return true;
}

}  // namespace Orange::Engine::Render
