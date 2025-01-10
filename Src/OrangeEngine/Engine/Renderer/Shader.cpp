#include "OpenGL/OpenGLPch.h"
#include "Shader.h"
#include "RendererAPI.h"
#include "OpenGL/OpenGLShader.h"

namespace Orange
{

    Shader::~Shader()
    {
        glDeleteProgram(mRendererID);
    }

    Ref<Shader> Shader::Create(const std::string& filepath)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
        {
            ORANGE_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr; 
        }
        case RendererAPI::API::OpenGL:
        {
            return std::make_shared<OpenGLShader>(filepath);
        }
        default:
            break;
        }

        ORANGE_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    Ref<Shader> Shader::Create(const std::string& name, const std::string& vertexShader, const std::string& fragmentShader)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
        {
            ORANGE_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        }
        case RendererAPI::API::OpenGL:
        {
            return std::make_shared<OpenGLShader>( name, vertexShader, fragmentShader);
        }
        default:
            break;
        }

        ORANGE_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    void ShaderLibrary::Add(const std::string& name, const Ref<Shader>& shader)
    {
        ORANGE_CORE_ASSERT(!Exists(name), "Shader already exists in library!");
        mShaders[name] = shader;
    }

    void ShaderLibrary::Add(const Ref<Shader>& shader)
    {
        const std::string& name = shader->GetName();
        Add(name, shader);
    }

    Ref<Shader> ShaderLibrary::Load(const std::string& filePath)
    {
        auto tpShader = Shader::Create(filePath);
        Add(tpShader);

        return tpShader;
    }
    Ref<Shader> ShaderLibrary::Load(const std::string& name, const std::string& filepath)
    {
        auto tpShader = Shader::Create(filepath);
        Add(name, tpShader);

        return tpShader;
    }
    Ref<Shader> ShaderLibrary::Get(const std::string& name) const
    {
        ORANGE_CORE_ASSERT(Exists(name), "Shader not found in library!");
        return mShaders.at(name);
    }
    bool ShaderLibrary::Exists(const std::string& name) const
    {
        return mShaders.find(name)!= mShaders.end();
    }
}