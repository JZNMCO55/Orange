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

        virtual void SetInt(const std::string& name, int value) const = 0;
        virtual void SetIntArray(const std::string& name, int* values, uint32_t count) const = 0;
        virtual void SetFloat(const std::string& name, float value) const = 0;
        virtual void SetFloat2(const std::string& name, const glm::vec2& value) const = 0;
        virtual void SetFloat3(const std::string& name, const glm::vec3& value) const = 0;
        virtual void SetFloat4(const std::string& name, const glm::vec4& value) const = 0;
        virtual void SetMat3(const std::string& name, const glm::mat3& value) const = 0;
        virtual void SetMat4(const std::string& name, const glm::mat4& value) const = 0;

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