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
        OpenGLShader(const std::string& vertexShader, const std::string& fragmentShader);
        virtual ~OpenGLShader();

        virtual void Bind() const override;
        virtual void Unbind() const override;

        void UploadUniformInt(const std::string& name, int value) const;
        void UploadUniformFloat(const std::string& name, float value) const;
        void UploadUniformFloat2(const std::string& name, const glm::vec2& value) const;
        void UploadUniformFloat3(const std::string& name, const glm::vec3& value) const;
        void UploadUniformFloat4(const std::string& name, const glm::vec4& value) const;
        void UploadUniformMat3(const std::string& name, const glm::mat3& matrix) const;
        void UploadUniformMat4(const std::string& name, const glm::mat4& matrix) const;

    private:
        std::string ReadFile(const std::string& filepath);
        std::unordered_map<unsigned int, std::string> PreProcess(const std::string& source);

        void Compile(const std::unordered_map<unsigned int, std::string>& shaderSources);
    private:
        uint32_t mRendererID;
    };
}

#endif // !OPENGL_SHADER_H
