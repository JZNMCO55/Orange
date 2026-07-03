// Pipeline 的接触阴影实现：FindActiveContactShadowPass / EnsureContactShadowResources
// / RecordContactShadowPass。
//
// 单 fullscreen pass：sceneDepth + ubo → 屏幕空间向光源 view-space 射线步进，
// 命中遮挡输出 <1 阴影因子，乘法 blend（dst×src = HDR×shadow）直接进 hdrColor。
// 补 shadow map + PCSS 在接触处的漏光缝隙。与 SSR 同款 view-space march + proj
// 投回（invProj 重建 / proj 投回自洽，规避 y-flip）。仅 directional light 时跑。

#include "PipelineImpl.h"

#include "orange/engine/render/PostProcessPasses.h"

#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp> // glm::inverse

#include <cstring>

namespace Orange::Engine::Render
{

    const ContactShadowPass* Pipeline::Impl::FindActiveContactShadowPass() const noexcept
    {
        if (postComponentActive)
        {
            return postContact.enabled ? &postContact : nullptr;
        }
        if (postProcessChain == nullptr)
        {
            return nullptr;
        }
        const std::size_t count = postProcessChain->PassCount();
        for (std::size_t i = 0; i < count; ++i)
        {
            const IPostProcessPass* p = postProcessChain->PassAt(i);
            if (const ContactShadowPass* cp = dynamic_cast<const ContactShadowPass*>(p))
            {
                return cp->enabled ? cp : nullptr;
            }
        }
        return nullptr;
    }

    bool Pipeline::Impl::EnsureContactShadowResources()
    {
        if (renderDevice == nullptr || sceneDepth == nullptr || hdrSampler == nullptr || contactShadowPipeline == nullptr || contactShadowLayout == nullptr || contactShadowUbo == nullptr)
        {
            return false;
        }
        // normalBuffer：接触阴影用法线做 N·L fade（避免终结线重复暗化）；法线预通道
        // 在主 pass 前已 Ensure + 填好，这里再 Ensure 一次保证 descriptor 有有效贴图。
        if (!EnsureNormalBuffer())
        {
            return false;
        }
        auto& rhi = renderDevice->GetRhiDevice();

        // sampler 计数 = sceneDepth + normalBuffer = 2。
        if (!contactShadowPool)
        {
            Orange::Rhi::DescriptorPoolDesc poolDesc{};
            poolDesc.mMaxSets = 1;
            poolDesc.mPoolSizes.push_back({Orange::Rhi::DescriptorType::CombinedImageSampler, 2});
            poolDesc.mPoolSizes.push_back({Orange::Rhi::DescriptorType::UniformBuffer, 1});
            poolDesc.mpDebugName = "orange_engine.contact_shadow.pool";
            contactShadowPool    = rhi.CreateDescriptorPool(poolDesc);
            if (!contactShadowPool)
            {
                return false;
            }
        }

        // contactShadowSet（0=sceneDepth, 1=ubo, 2=normalBuffer）：只分配一次，
        // depth / normalBuffer 变化时 UpdateDescriptorSet 重写绑定（不 reset+realloc，
        // RHI pool 无 free-bit 反复 realloc 会 resize 累积耗尽 OOM）。
        if (contactShadowSet == nullptr)
        {
            contactShadowSet = rhi.AllocateDescriptorSet(*contactShadowPool, *contactShadowLayout);
            if (!contactShadowSet)
            {
                return false;
            }
            contactShadowSetBoundDepth  = nullptr; // 强制下面 update
            contactShadowSetBoundNormal = nullptr;
        }
        if (contactShadowSetBoundDepth != sceneDepth.get() || contactShadowSetBoundNormal != normalBuffer.get())
        {
            Orange::Rhi::DescriptorWrite w[3]{};
            w[0].mBinding             = 0;
            w[0].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            w[0].mImageInfo.mpTexture = sceneDepth.get();
            w[0].mImageInfo.mpSampler = hdrSampler.get();
            w[1].mBinding             = 1;
            w[1].mType                = Orange::Rhi::DescriptorType::UniformBuffer;
            w[1].mBufferInfo.mpBuffer = contactShadowUbo.get();
            w[1].mBufferInfo.mOffset  = 0;
            w[1].mBufferInfo.mRange   = sizeof(ContactShadowUboData);
            w[2].mBinding             = 2;
            w[2].mType                = Orange::Rhi::DescriptorType::CombinedImageSampler;
            w[2].mImageInfo.mpTexture = normalBuffer.get();
            w[2].mImageInfo.mpSampler = hdrSampler.get();
            rhi.UpdateDescriptorSet(*contactShadowSet, w, 3);
            contactShadowSetBoundDepth  = sceneDepth.get();
            contactShadowSetBoundNormal = normalBuffer.get();
        }
        return true;
    }

    bool Pipeline::Impl::RecordContactShadowPass(const ContactShadowPass& csDesc,
                                                 const glm::mat4&         proj,
                                                 const glm::vec3&         viewLightDir)
    {
        if (offscreenCmd == nullptr || hdrColor == nullptr || sceneDepth == nullptr)
        {
            return false;
        }
        if (!EnsureContactShadowResources())
        {
            return false;
        }

        // ---- 写 CsUbo ----
        {
            ContactShadowUboData data{};
            data.proj         = proj;
            data.invProj      = glm::inverse(proj);
            data.viewLightDir = glm::vec4(viewLightDir, 0.0f);
            data.params       = glm::vec4(csDesc.length, csDesc.maxSteps,
                                          csDesc.thickness, csDesc.strength);
            data.params2      = glm::vec4(csDesc.bias, 0.0f, 0.0f, 0.0f);
            void* mapped      = contactShadowUbo->Map();
            if (mapped == nullptr)
            {
                return false;
            }
            std::memcpy(mapped, &data, sizeof(data));
            contactShadowUbo->Unmap();
        }

        auto& cmd = *offscreenCmd;

        // sceneDepth → ShaderReadOnly（与 SSAO / SSR / god rays 共用 flag）。
        if (!sceneDepthLayoutShaderReadOnly)
        {
            cmd.TransitionTexture(*sceneDepth,
                                  Orange::Rhi::TextureLayout::DepthStencilAttachment,
                                  Orange::Rhi::TextureLayout::ShaderReadOnly);
            sceneDepthLayoutShaderReadOnly = true;
        }

        // 单 pass：乘法 blend 进 HDR（读 depth，写 HDR，无读写同 target 反馈）。
        cmd.TransitionTexture(*hdrColor,
                              Orange::Rhi::TextureLayout::ShaderReadOnly,
                              Orange::Rhi::TextureLayout::ColorAttachment);

        Orange::Rhi::ColorAttachment att{};
        att.mpView   = hdrColor->GetDefaultView();
        att.mLoadOp  = Orange::Rhi::LoadOp::Load;
        att.mStoreOp = Orange::Rhi::StoreOp::Store;

        Orange::Rhi::RenderingDesc rd{};
        rd.mRenderArea.mWidth  = hdrWidth;
        rd.mRenderArea.mHeight = hdrHeight;
        rd.mColorAttachments.push_back(att);
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

        cmd.BindGraphicsPipeline(*contactShadowPipeline);
        cmd.SetDescriptorSet(0, *contactShadowSet);
        cmd.Draw(3, 1, 0, 0);
        cmd.EndRendering();

        cmd.TransitionTexture(*hdrColor,
                              Orange::Rhi::TextureLayout::ColorAttachment,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
        return true;
    }

} // namespace Orange::Engine::Render
