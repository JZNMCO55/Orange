#include "FbxMaterialParse.h"

#include <orange/engine/render/MaterialTypes.h>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

// OpenFBX vendor 头 —— 仅取声明（ofbx.cpp / libdeflate.c 作为独立 TU 编译）。
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244) // narrowing
#pragma warning(disable : 4267) // size_t → smaller int
#endif
#include "ofbx.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <cmath>

namespace Orange::Editor::Import
{

    namespace
    {
        // 把 DataView（FBX 字符串）拷成 std::string（裸字节区间）。
        std::string DataViewToString(const ofbx::DataView& dv)
        {
            if (dv.begin == nullptr || dv.end <= dv.begin)
            {
                return {};
            }
            return std::string(reinterpret_cast<const char*>(dv.begin),
                               reinterpret_cast<const char*>(dv.end));
        }
    } // namespace

    std::string ResolveFbxTexture(const ofbx::Material* mat, int textureType,
                                  const std::filesystem::path& fbxDir)
    {
        if (mat == nullptr)
        {
            return {};
        }
        const ofbx::Texture* tex =
            mat->getTexture(static_cast<ofbx::Texture::TextureType>(textureType));
        if (tex == nullptr)
        {
            return {};
        }

        namespace fs = std::filesystem;
        std::error_code ec;

        // 1) 相对路径相对 .fbx 目录解析。
        const std::string rel = DataViewToString(tex->getRelativeFileName());
        if (!rel.empty())
        {
            std::string relNorm = rel;
            for (char& c : relNorm)
            {
                if (c == '\\')
                {
                    c = '/';
                }
            }
            const fs::path full = fbxDir / relNorm;
            if (fs::exists(full, ec) && fs::is_regular_file(full, ec))
            {
                return full.generic_string();
            }
            // 相对路径常带导出机器的多级前缀；退化只用文件名在 fbxDir 找。
            const fs::path byName = fbxDir / fs::path(relNorm).filename();
            if (fs::exists(byName, ec) && fs::is_regular_file(byName, ec))
            {
                return byName.generic_string();
            }
        }

        // 2) 绝对 filename 直接试（导出机器路径，常失效但偶尔同机有效）。
        const std::string abs = DataViewToString(tex->getFileName());
        if (!abs.empty())
        {
            fs::path absPath(abs);
            if (fs::exists(absPath, ec) && fs::is_regular_file(absPath, ec))
            {
                return absPath.generic_string();
            }
            const fs::path byName = fbxDir / absPath.filename();
            if (fs::exists(byName, ec) && fs::is_regular_file(byName, ec))
            {
                return byName.generic_string();
            }
        }
        return {};
    }

    Material::MaterialFileData BuildFbxMaterialFileData(
        const ofbx::Material* mat, const std::filesystem::path& fbxDir,
        const std::function<std::string(const std::string&)>& resolver)
    {
        using ::Orange::Engine::Render::MaterialUniformType;
        Material::MaterialFileData mdata;
        mdata.templateName = "pbr";
        if (mat == nullptr)
        {
            return mdata;
        }

        const ofbx::Color              diffuse       = mat->getDiffuseColor();
        const double                   diffuseFactor = mat->getDiffuseFactor();
        const double                   opacity       = mat->getOpacity();
        Material::UniformOverrideValue uBase;
        uBase.name  = "uBaseColor";
        uBase.type  = MaterialUniformType::Vec4;
        uBase.value = glm::vec4(
            static_cast<float>(diffuse.r * diffuseFactor),
            static_cast<float>(diffuse.g * diffuseFactor),
            static_cast<float>(diffuse.b * diffuseFactor),
            (opacity > 0.0) ? static_cast<float>(opacity) : 1.0f);
        mdata.uniforms.push_back(uBase);

        // roughness：由 Phong shininess（高光指数）推导。shininess 越大越光滑。
        double shininess = mat->getShininess();
        if (shininess <= 0.0)
        {
            shininess = mat->getShininessExponent();
        }
        float roughness = static_cast<float>(std::sqrt(2.0 / (shininess + 2.0)));
        if (roughness < 0.04f)
        {
            roughness = 0.04f;
        }
        if (roughness > 1.0f)
        {
            roughness = 1.0f;
        }
        Material::UniformOverrideValue uMra;
        uMra.name  = "uMRA";
        uMra.type  = MaterialUniformType::Vec4;
        uMra.value = glm::vec4(0.0f, roughness, 1.0f, 0.0f); // metallic=0 / ao=1 中性
        mdata.uniforms.push_back(uMra);

        const ofbx::Color emissive       = mat->getEmissiveColor();
        const double      emissiveFactor = mat->getEmissiveFactor();
        const glm::vec3   emis(static_cast<float>(emissive.r * emissiveFactor),
                               static_cast<float>(emissive.g * emissiveFactor),
                               static_cast<float>(emissive.b * emissiveFactor));
        if (emis != glm::vec3(0.0f))
        {
            Material::UniformOverrideValue uEmis;
            uEmis.name  = "uEmissive";
            uEmis.type  = MaterialUniformType::Vec4;
            uEmis.value = glm::vec4(emis, 0.0f);
            mdata.uniforms.push_back(uEmis);
        }

        // 贴图槽：DIFFUSE → 0 / NORMAL → 1 / EMISSIVE → 4。resolver==null（scalar-only
        // 蓝本抽取阶段）时不填贴图 —— 贴图源由调用方在 import 阶段单独解析 + 落盘后回填。
        if (resolver)
        {
            auto addTex = [&](ofbx::Texture::TextureType type, std::uint32_t binding)
            {
                const std::string src =
                    ResolveFbxTexture(mat, static_cast<int>(type), fbxDir);
                if (src.empty())
                {
                    return;
                }
                const std::string dest = resolver(src);
                if (!dest.empty())
                {
                    mdata.textures.push_back({binding, dest});
                }
            };
            addTex(ofbx::Texture::DIFFUSE, 0u);
            addTex(ofbx::Texture::NORMAL, 1u);
            addTex(ofbx::Texture::EMISSIVE, 4u);
        }

        return mdata;
    }

    std::vector<std::pair<std::uint32_t, std::string>> StageFbxTextureSources(
        const ofbx::Material* mat, const std::filesystem::path& fbxDir)
    {
        std::vector<std::pair<std::uint32_t, std::string>> out;
        if (mat == nullptr)
        {
            return out;
        }
        const auto addSrc = [&](ofbx::Texture::TextureType type,
                                std::uint32_t              binding)
        {
            const std::string p =
                ResolveFbxTexture(mat, static_cast<int>(type), fbxDir);
            if (!p.empty())
            {
                out.emplace_back(binding, p);
            }
        };
        addSrc(ofbx::Texture::DIFFUSE, 0u);
        addSrc(ofbx::Texture::NORMAL, 1u);
        addSrc(ofbx::Texture::EMISSIVE, 4u);
        return out;
    }

} // namespace Orange::Editor::Import
