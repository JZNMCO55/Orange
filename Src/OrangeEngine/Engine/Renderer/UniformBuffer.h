#ifndef UNIFORM_BUFFER_H
#define UNIFORM_BUFFER_H

#include "OrangeExport.h"

namespace Orange
{
    class ORANGE_API UniformBuffer
    {
    public:
        virtual ~UniformBuffer() = default;

        virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0) = 0;

        static Ref<UniformBuffer> Create(uint32_t size, uint32_t binding);
    };
}

#endif // UNIFORM_BUFFER_H