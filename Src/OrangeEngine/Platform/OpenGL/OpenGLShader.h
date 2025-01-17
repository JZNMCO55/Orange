#ifndef OPENGL_SHADER_H
#define OPENGL_SHADER_H

#include "OrangeExport.h"
#include "Renderer/Shader.h"

namespace Orange
{
    class ORANGE_API OpenGLShader : public Shader
    {
    public:
        OpenGLShader(const std::string& filepath);
        OpenGLShader(const std::string& name, const std::string& vertexShader, const std::string& fragmentShader);
        virtual ~OpenGLShader();

        virtual void Bind() const override;
        virtual void Unbind() const override;

        virtual void SetInt(const std::string& name, int value) const override;
        virtual void SetFloat(const std::string& name, float value) const override;
        virtual void SetFloat2(const std::string& name, const glm::vec2& value) const override;
        virtual void SetFloat3(const std::string& name, const glm::vec3& value) const override;
        virtual void SetFloat4(const std::string& name, const glm::vec4& value) const override;
        virtual void SetMat3(const std::string& name, const glm::mat3& matrix) const override;
        virtual void SetMat4(const std::string& name, const glm::mat4& matrix) const override;

        virtual const std::string& GetName() const override { return mName; }
    private:
        void UploadUniformInt(const std::string& name, int value) const;
        void UploadUniformFloat(const std::string& name, float value) const;
        void UploadUniformFloat2(const std::string& name, const glm::vec2& value) const;
        void UploadUniformFloat3(const std::string& name, const glm::vec3& value) const;
        void UploadUniformFloat4(const std::string& name, const glm::vec4& value) const;
        void UploadUniformMat3(const std::string& name, const glm::mat3& matrix) const;
        void UploadUniformMat4(const std::string& name, const glm::mat4& matrix) const;
        
        std::string ReadFile(const std::string& filepath);
        std::unordered_map<unsigned int, std::string> PreProcess(const std::string& source);

        void Compile(const std::unordered_map<unsigned int, std::string>& shaderSources);
    private:
        uint32_t mRendererID;
        std::string mName;
    };
}

#endif // !OPENGL_SHADER_H
