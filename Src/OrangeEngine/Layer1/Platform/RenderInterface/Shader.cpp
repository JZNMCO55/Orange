#include "orgpch.h"

#include "Shader.h"
#include <utility>

#include "ShaderPack.h"
#include "RendererAPI.h"

namespace Orange
{
    Ref<Shader> Shader::Create(const std::string &filepath, bool forceCompile, bool disableOptimization)
    {
        return nullptr;
    }

    Ref<Shader> Shader::CreateFromString(const std::string &source)
    {
        Ref<Shader> result = nullptr;

        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
            return nullptr;
        }
        return result;
    }

    ShaderLibrary::ShaderLibrary()
    {
    }

    ShaderLibrary::~ShaderLibrary()
    {
    }

    void ShaderLibrary::Add(const Ref<Shader> &shader)
    {
        auto &name = shader->GetName();
        ORG_CORE_ASSERT(m_Shaders.find(name) == m_Shaders.end());
        m_Shaders[name] = shader;
    }

    void ShaderLibrary::Load(std::string_view path, bool forceCompile, bool disableOptimization)
    {
        Ref<Shader> shader;
        if (!forceCompile && m_ShaderPack)
        {
            if (m_ShaderPack->Contains(path))
                shader = m_ShaderPack->LoadShader(path);
        }
        else
        {
            // Try compile from source
            // Unavailable at runtime
#ifdef TODO
            shader = VulkanShaderCompiler::Compile(path, forceCompile, disableOptimization);
#endif
        }

        auto &name = shader->GetName();
        ORG_CORE_ASSERT(m_Shaders.find(name) == m_Shaders.end());
        m_Shaders[name] = shader;
    }

    void ShaderLibrary::Load(std::string_view name, const std::string &path)
    {
        ORG_CORE_ASSERT(m_Shaders.find(std::string(name)) == m_Shaders.end());
        m_Shaders[std::string(name)] = Shader::Create(path);
    }

    void ShaderLibrary::LoadShaderPack(const std::filesystem::path &path)
    {
        m_ShaderPack = Ref<ShaderPack>::Create(path);
        if (!m_ShaderPack->IsLoaded())
        {
            m_ShaderPack = nullptr;
            ORG_CORE_ERROR("Could not load shader pack: " + path.string());
        }
    }

    const Ref<Shader> &ShaderLibrary::Get(const std::string &name) const
    {
        ORG_CORE_ASSERT(m_Shaders.find(name) != m_Shaders.end());
        return m_Shaders.at(name);
    }

    ShaderUniform::ShaderUniform(std::string name, const ShaderUniformType type, const uint32_t size, const uint32_t offset)
        : m_Name(std::move(name)), m_Type(type), m_Size(size), m_Offset(offset)
    {
    }

    constexpr std::string_view ShaderUniform::UniformTypeToString(const ShaderUniformType type)
    {
        if (type == ShaderUniformType::Bool)
        {
            return std::string("Boolean");
        }
        else if (type == ShaderUniformType::Int)
        {
            return std::string("Int");
        }
        else if (type == ShaderUniformType::Float)
        {
            return std::string("Float");
        }

        return std::string("None");
    }
}