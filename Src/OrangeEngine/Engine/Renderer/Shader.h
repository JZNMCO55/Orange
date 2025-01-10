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

        virtual const std::string& GetName() const = 0;

        static Ref<Shader> Create(const std::string& filepath);
        static Ref<Shader> Create(const std::string& name, const std::string& vertexShader, const std::string& fragmentShader);
    private:
        unsigned int mRendererID;
    };

    class ORANGE_API ShaderLibrary
    {
    public:
        void Add(const std::string& name, const Ref<Shader>& shader);
        void Add(const Ref<Shader>& shader);

        Ref<Shader> Load(const std::string& name);
        Ref<Shader> Load(const std::string& name, const std::string& filepath);

        Ref<Shader> Get(const std::string& name) const;

        bool Exists(const std::string& name) const;

    private:
        std::unordered_map<std::string, Ref<Shader>> mShaders;
    };
}

#endif // SHADER_H