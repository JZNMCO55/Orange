#ifndef OPENGL_UNIFORM_BUFFER_H
#define OPENGL_UNIFORM_BUFFER_H

#include "OrangeExport.h"

class UniformBuffer;
namespace Orange
{
    class ORANGE_API OpenGLUniformBuffer : public UniformBuffer
    {
    public:
        OpenGLUniformBuffer(uint32_t size, uint32_t binding);
        virtual ~OpenGLUniformBuffer();

        virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0) override;

    private:
        uint32_t mRendererID = 0;
    };
}

#endif // OPENGL_UNIFORM_BUFFER_H