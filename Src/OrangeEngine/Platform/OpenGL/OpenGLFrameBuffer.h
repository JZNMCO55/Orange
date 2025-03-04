#ifndef OPENGL_FRAME_BUFFER_H
#define OPENGL_FRAME_BUFFER_H

#include "OrangeExport.h"
#include "Renderer/FrameBuffer.h"

namespace Orange
{
    class ORANGE_API OpenGLFrameBuffer : public FrameBuffer
    {
    public:
        OpenGLFrameBuffer(const FrameBufferSpecification& spec);
        virtual ~OpenGLFrameBuffer();

        void Invalidate();

        virtual void Bind() override;
        virtual void Unbind() override;

        virtual void Resize(uint32_t width, uint32_t height) override;

        virtual const uint32_t GetColorAttachmentRendererID(uint32_t index = 0) const override;
        virtual int ReadPixel(uint32_t attachmentIndex, int x, int y) const override;

        virtual void ClearAttachment(uint32_t attachmentIndex, int value) override;

        virtual const FrameBufferSpecification& GetSpecification() const override { return mSpecification; }

    private:
        uint32_t mRendererID = 0;

        FrameBufferSpecification mSpecification;

        std::vector<FramebufferTextureSpecification> mColorAttachmentSpecifications;
        FramebufferTextureSpecification mDepthAttachmentSpecification = FramebufferTextureFormat::None;

        std::vector<uint32_t> mColorAttachment;
        uint32_t mDepthAttachment = 0;
    };
}

#endif // OPENGL_FRAME_BUFFER_H