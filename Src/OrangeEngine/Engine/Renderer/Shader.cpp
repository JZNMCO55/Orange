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

    Shader* Shader::Create(const std::string& filepath)
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
            return new OpenGLShader(filepath);
        }
        default:
            break;
        }

        ORANGE_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    Shader* Shader::Create(const std::string& vertexShader, const std::string& fragmentShader)
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
            return new OpenGLShader(vertexShader, fragmentShader);
        }
        default:
            break;
        }

        ORANGE_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
}