#ifndef SHADER_H
#define SHADER_H

#include "OrangeExport.h"

namespace Orange
{
    class ORANGE_API Shader
    {
    public:
        virtual ~Shader();

        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;

        static Shader* Create(const std::string& filepath);
        static Shader* Create(const std::string& vertexShader, const std::string& fragmentShader);
    private:
        unsigned int mRendererID;
    };
}

#endif // SHADER_H