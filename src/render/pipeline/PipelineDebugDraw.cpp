// Pipeline 的 DebugDraw overlay 系列实现：RecordDebugDrawPass。Pipeline 把
// 调试几何（line / aabb / sphere / quad / gizmo overlay 等）累在
// DebugDrawScene 内，本 pass 调 backend Flush 把它们绘到 hdrColor 之上，
// always-on-top（不挂 depth attachment）。

#include "PipelineImpl.h"

#include "orange/engine/core/Profiler.h"

namespace Orange::Engine::Render
{

    bool Pipeline::Impl::RecordDebugDrawPass(const glm::mat4& viewProj)
    {
        ORANGE_PROFILE_SCOPE("DebugDraw");
        if (!debugDrawScene || !debugDrawScene->IsInitialized())
        {
            return false;
        }
        if (!debugDrawScene->IsEnabled() || debugDrawScene->IsEmptyBackend_())
        {
            // 当前帧无几何 / 整条 wrap 已 disable —— silent skip，不付 BeginRendering
            // 开销。SetViewProjBackend_ 即使不调，Flush silent 内 OR side 自然 skip。
            return true;
        }
        if (offscreenCmd == nullptr || hdrColor == nullptr)
        {
            return false;
        }

        auto& cmd = *offscreenCmd;

        // hdrColor: ShaderReadOnly → ColorAttachment 准备叠加 debug 几何。
        if (hdrLayoutShaderReadOnly)
        {
            cmd.TransitionTexture(*hdrColor,
                                  Orange::Rhi::TextureLayout::ShaderReadOnly,
                                  Orange::Rhi::TextureLayout::ColorAttachment);
            hdrLayoutShaderReadOnly = false;
        }

        Orange::Rhi::ColorAttachment colorAtt{};
        colorAtt.mpView   = hdrColor->GetDefaultView();
        colorAtt.mLoadOp  = Orange::Rhi::LoadOp::Load;
        colorAtt.mStoreOp = Orange::Rhi::StoreOp::Store;

        Orange::Rhi::RenderingDesc rd{};
        rd.mRenderArea.mWidth  = hdrWidth;
        rd.mRenderArea.mHeight = hdrHeight;
        rd.mColorAttachments.push_back(colorAtt);
        // 不挂 depth attachment —— DebugDraw pipeline 自身 depth-test 关，
        // 视觉上 always-on-top，与 Lumix / Godot debug viewport 同款节奏。

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

        debugDrawScene->SetViewProjBackend_(viewProj);
        debugDrawScene->FlushBackend_(cmd);

        cmd.EndRendering();

        // hdrColor 翻回 ShaderReadOnly：与 RecordGridPass 收尾同款契约，
        // 下一段 pass（bloom / passthrough / tonemap）按 ShaderReadOnly 假设跑。
        cmd.TransitionTexture(*hdrColor,
                              Orange::Rhi::TextureLayout::ColorAttachment,
                              Orange::Rhi::TextureLayout::ShaderReadOnly);
        hdrLayoutShaderReadOnly = true;
        return true;
    }

} // namespace Orange::Engine::Render
