#ifndef OPENGL_BUFFER_H
#define OPENGL_BUFFER_H

#include "Renderer/Buffer.h"

namespace Orange
{
    class ORANGE_API OpenGLVertexBuffer : public VertexBuffer
    {
    public:
        OpenGLVertexBuffer(float* vertices, uint32_t size);
        virtual ~OpenGLVertexBuffer();

        virtual void Bind() const override;
        virtual void Unbind() const override;
        virtual const BufferLayout& GetLayout() const override { return mLayout; }
        virtual void SetLayout(const BufferLayout& layout) override { mLayout = layout; }

    private:
        uint32_t mRendererID;
        BufferLayout mLayout;
    };

    class ORANGE_API OpenGLIndexBuffer : public IndexBuffer
    {
    public:
        OpenGLIndexBuffer(uint32_t* indices, uint32_t count);
        virtual ~OpenGLIndexBuffer();

        virtual void Bind() const;
        virtual void Unbind() const;

        virtual uint32_t GetCount() const { return mCount; }
    private:
        uint32_t mRendererID;
        uint32_t mCount;
    };
}

#endif // OPENGL_BUFFER_H