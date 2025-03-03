#ifndef FRAME_BUFFER_H
#define FRAME_BUFFER_H

#include "OrangeExport.h"

namespace Orange
{
    enum class FramebufferTextureFormat
    {
        None = 0,

        // Color
        RGBA8,

        // Depth/stencil
        DEPTH24STENCIL8,

        // Defaults
        Depth = DEPTH24STENCIL8
    };

    struct FramebufferTextureSpecification
    {
        FramebufferTextureSpecification() = default;
        FramebufferTextureSpecification(FramebufferTextureFormat format)
            : TextureFormat(format) {}

        FramebufferTextureFormat TextureFormat = FramebufferTextureFormat::None;
        // TODO: filtering/wrap
    };

    struct FramebufferAttachmentSpecification
    {
        FramebufferAttachmentSpecification() = default;
        FramebufferAttachmentSpecification(std::initializer_list<FramebufferTextureSpecification> attachments)
            : Attachments(attachments) {}

        std::vector<FramebufferTextureSpecification> Attachments;
    };

    struct FrameBufferSpecification
    {
        uint32_t width, height;
        uint32_t samples = 1;
        FramebufferAttachmentSpecification attachment;

        bool SwapChainTarget = false;
    };

    class ORANGE_API FrameBuffer
    {
    public:
        virtual void Bind() = 0;
        virtual void Unbind() = 0;

        virtual void Resize(uint32_t width, uint32_t height) = 0;

        virtual const uint32_t GetColorAttachmentRendererID(uint32_t index = 0) const = 0;

        virtual const FrameBufferSpecification& GetSpecification() const = 0;

        static Ref<FrameBuffer> Create(const FrameBufferSpecification& spec);
    };
}

#endif // !FRAME_BUFFER_H