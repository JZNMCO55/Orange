#ifndef FRAME_BUFFER_H
#define FRAME_BUFFER_H

#include "OrangeExport.h"

namespace Orange
{
    struct FrameBufferSpecification
    {
        uint32_t width, height;
        uint32_t samples = 1;

        bool SwapChainTarget = false;
    };

    class ORANGE_API FrameBuffer
    {
    public:
        virtual void Bind() = 0;
        virtual void Unbind() = 0;

        virtual const uint32_t GetColorAttachmentRendererID() const = 0;

        virtual const FrameBufferSpecification& GetSpecification() const = 0;

        static Ref<FrameBuffer> Create(const FrameBufferSpecification& spec);
    };
}

#endif // !FRAME_BUFFER_H