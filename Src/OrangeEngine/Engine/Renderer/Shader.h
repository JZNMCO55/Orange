#ifndef SHADER_H
#define SHADER_H

#include "OrangeExport.h"

namespace Orange
{
    class ORANGE_API Shader
    {
    public:
        Shader(const std::string& verSrc, const std::string& fragSrc);
        virtual ~Shader();

        void Bind() const;
        void Unbind() const;

    private:
        unsigned int mRendererID;
    };
}

#endif // SHADER_H