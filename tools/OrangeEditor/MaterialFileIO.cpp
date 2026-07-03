// MaterialFileIO 实现 —— .material 文件 v1.1 schema 读写。详见同名 .h
// 头注释。

#include "MaterialFileIO.h"

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/TextureAsset.h>
#include <orange/engine/core/Log.h>
#include <orange/engine/core/Serialization.h>

#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <cstring>
#include <variant>

namespace Orange::Editor::Material
{
    namespace
    {

        using ::Orange::Engine::JsonReader;
        using ::Orange::Engine::JsonWriter;
        using ::Orange::Engine::ResultCode;
        using ::Orange::Engine::SchemaVersion;
        using ::Orange::Engine::Render::MaterialInstance;
        using ::Orange::Engine::Render::MaterialUniformType;

        // schema v1.1 namespace ID 与 v1.0 同 namespace；major == 1；minor 0 / 1
        // 都接受（reader 前向兼容 minor）。
        constexpr const char* kSchemaNamespace = "render/material_instance";
        constexpr int         kSchemaMajor     = 1;
        constexpr int         kSchemaMinor     = 1;

        // type 字段的 stringly-typed 表示。与 MaterialUniformType 一一对应。
        const char* UniformTypeToString(MaterialUniformType type) noexcept
        {
            switch (type)
            {
                case MaterialUniformType::Float:
                    return "float";
                case MaterialUniformType::Int:
                    return "int";
                case MaterialUniformType::Vec2:
                    return "vec2";
                case MaterialUniformType::Vec3:
                    return "vec3";
                case MaterialUniformType::Vec4:
                    return "vec4";
                case MaterialUniformType::Mat4:
                    return "mat4";
            }
            return "float";
        }

        std::optional<MaterialUniformType> ParseUniformType(std::string_view s) noexcept
        {
            if (s == "float")
                return MaterialUniformType::Float;
            if (s == "int")
                return MaterialUniformType::Int;
            if (s == "vec2")
                return MaterialUniformType::Vec2;
            if (s == "vec3")
                return MaterialUniformType::Vec3;
            if (s == "vec4")
                return MaterialUniformType::Vec4;
            if (s == "mat4")
                return MaterialUniformType::Mat4;
            return std::nullopt;
        }

    } // namespace

    std::optional<MaterialFileData> ReadMaterialFile(const std::string& path)
    {
        auto rr = JsonReader::FromFile(path);
        if (rr.IsErr())
        {
            ORANGE_LOG_ERROR("[MaterialFileIO] 读 '{}' 失败 (code={})",
                             path,
                             static_cast<unsigned>(rr.Error().code));
            return std::nullopt;
        }
        const JsonReader& reader = rr.Value();

        auto sv = reader.ReadSchemaVersion("schemaVersion");
        if (sv.IsErr())
        {
            ORANGE_LOG_ERROR("[MaterialFileIO] '{}' 缺 schemaVersion 或格式错", path);
            return std::nullopt;
        }
        if (sv.Value().Namespace() != kSchemaNamespace || sv.Value().Major() != kSchemaMajor)
        {
            ORANGE_LOG_ERROR("[MaterialFileIO] '{}' schema namespace/major 不匹配 "
                             "(got {} v{}.x，期望 {} v{}.x)",
                             path,
                             sv.Value().Namespace(),
                             static_cast<unsigned>(sv.Value().Major()),
                             kSchemaNamespace,
                             kSchemaMajor);
            return std::nullopt;
        }

        MaterialFileData data;
        if (!reader.ReadString("templateName", data.templateName) || data.templateName.empty())
        {
            ORANGE_LOG_ERROR("[MaterialFileIO] '{}' templateName 缺失或空", path);
            return std::nullopt;
        }

        // uniforms 段（v1.0 缺这个字段，ArraySize 返回 0 → 直接跳过）
        const std::size_t uniformCount = reader.ArraySize("uniforms");
        data.uniforms.reserve(uniformCount);
        for (std::size_t i = 0; i < uniformCount; ++i)
        {
            const std::string    base = "uniforms/" + std::to_string(i) + "/";
            UniformOverrideValue u;
            if (!reader.ReadString(base + "name", u.name) || u.name.empty())
            {
                ORANGE_LOG_WARN("[MaterialFileIO] '{}' uniforms[{}].name 缺失，跳过",
                                path, i);
                continue;
            }
            std::string typeStr;
            if (!reader.ReadString(base + "type", typeStr))
            {
                ORANGE_LOG_WARN("[MaterialFileIO] '{}' uniforms[{}].type 缺失，跳过",
                                path, i);
                continue;
            }
            auto typeOpt = ParseUniformType(typeStr);
            if (!typeOpt.has_value())
            {
                ORANGE_LOG_WARN("[MaterialFileIO] '{}' uniforms[{}].type='{}' 未知，跳过",
                                path, i, typeStr);
                continue;
            }
            u.type = *typeOpt;

            const std::string valuePath = base + "value";
            switch (u.type)
            {
                case MaterialUniformType::Float:
                {
                    double tmp = 0.0;
                    if (!reader.ReadFloat(valuePath, tmp))
                    {
                        continue;
                    }
                    u.value = static_cast<float>(tmp);
                    break;
                }
                case MaterialUniformType::Int:
                {
                    std::int64_t tmp = 0;
                    if (!reader.ReadInt(valuePath, tmp))
                    {
                        continue;
                    }
                    u.value = static_cast<std::int32_t>(tmp);
                    break;
                }
                case MaterialUniformType::Vec2:
                {
                    float buf[2]{};
                    if (!reader.ReadFloatArray(valuePath, buf, 2))
                    {
                        continue;
                    }
                    u.value = glm::vec2(buf[0], buf[1]);
                    break;
                }
                case MaterialUniformType::Vec3:
                {
                    float buf[3]{};
                    if (!reader.ReadFloatArray(valuePath, buf, 3))
                    {
                        continue;
                    }
                    u.value = glm::vec3(buf[0], buf[1], buf[2]);
                    break;
                }
                case MaterialUniformType::Vec4:
                {
                    float buf[4]{};
                    if (!reader.ReadFloatArray(valuePath, buf, 4))
                    {
                        continue;
                    }
                    u.value = glm::vec4(buf[0], buf[1], buf[2], buf[3]);
                    break;
                }
                case MaterialUniformType::Mat4:
                {
                    float buf[16]{};
                    if (!reader.ReadFloatArray(valuePath, buf, 16))
                    {
                        continue;
                    }
                    glm::mat4 m{};
                    std::memcpy(glm::value_ptr(m), buf, sizeof(buf));
                    u.value = m;
                    break;
                }
            }
            data.uniforms.push_back(std::move(u));
        }

        // textures 段同理
        const std::size_t textureCount = reader.ArraySize("textures");
        data.textures.reserve(textureCount);
        for (std::size_t i = 0; i < textureCount; ++i)
        {
            const std::string    base = "textures/" + std::to_string(i) + "/";
            TextureOverrideEntry t;
            std::int64_t         bindingRaw = 0;
            if (!reader.ReadInt(base + "binding", bindingRaw))
            {
                ORANGE_LOG_WARN("[MaterialFileIO] '{}' textures[{}].binding 缺失，跳过",
                                path, i);
                continue;
            }
            t.binding = static_cast<std::uint32_t>(bindingRaw);
            reader.ReadString(base + "path", t.path); // path 可空（占位）
            data.textures.push_back(std::move(t));
        }

        return data;
    }

    bool WriteMaterialFile(const std::string& path, const MaterialFileData& data)
    {
        static const SchemaVersion kSchema{kSchemaNamespace, kSchemaMajor, kSchemaMinor};

        JsonWriter writer;
        writer.WriteSchemaVersion("schemaVersion", kSchema);
        writer.WriteString("templateName", data.templateName);

        writer.BeginArray("uniforms", data.uniforms.size());
        for (std::size_t i = 0; i < data.uniforms.size(); ++i)
        {
            const auto&       u    = data.uniforms[i];
            const std::string base = "uniforms/" + std::to_string(i) + "/";
            writer.WriteString(base + "name", u.name);
            writer.WriteString(base + "type", UniformTypeToString(u.type));

            // value 字段按 type 走对应分支
            switch (u.type)
            {
                case MaterialUniformType::Float:
                    writer.WriteFloat(base + "value",
                                      static_cast<double>(std::get<float>(u.value)));
                    break;
                case MaterialUniformType::Int:
                    writer.WriteInt(base + "value",
                                    static_cast<std::int64_t>(std::get<std::int32_t>(u.value)));
                    break;
                case MaterialUniformType::Vec2:
                {
                    const auto& v = std::get<glm::vec2>(u.value);
                    writer.WriteFloatArray(base + "value", glm::value_ptr(v), 2);
                    break;
                }
                case MaterialUniformType::Vec3:
                {
                    const auto& v = std::get<glm::vec3>(u.value);
                    writer.WriteFloatArray(base + "value", glm::value_ptr(v), 3);
                    break;
                }
                case MaterialUniformType::Vec4:
                {
                    const auto& v = std::get<glm::vec4>(u.value);
                    writer.WriteFloatArray(base + "value", glm::value_ptr(v), 4);
                    break;
                }
                case MaterialUniformType::Mat4:
                {
                    const auto& m = std::get<glm::mat4>(u.value);
                    writer.WriteFloatArray(base + "value", glm::value_ptr(m), 16);
                    break;
                }
            }
        }

        writer.BeginArray("textures", data.textures.size());
        for (std::size_t i = 0; i < data.textures.size(); ++i)
        {
            const auto&       t    = data.textures[i];
            const std::string base = "textures/" + std::to_string(i) + "/";
            writer.WriteInt(base + "binding", static_cast<std::int64_t>(t.binding));
            writer.WriteString(base + "path", t.path);
        }

        auto sv = writer.SaveToFile(path, 2);
        if (sv.IsErr())
        {
            ORANGE_LOG_ERROR("[MaterialFileIO] 写 '{}' 失败 (code={})",
                             path,
                             static_cast<unsigned>(sv.Error()));
            return false;
        }
        return true;
    }

    MaterialFileData BuildDataFromInstance(
        const MaterialInstance&                       instance,
        const std::string&                            templateName,
        const ::Orange::Engine::Asset::AssetRegistry* pAssetRegistry)
    {
        MaterialFileData data;
        data.templateName = templateName;

        // 枚举 uniform override + 按 name 查 type + 按 type 拉对应 GetUniformXxx
        std::vector<std::string> names = instance.GetUniformOverrideNames();
        data.uniforms.reserve(names.size());
        for (const auto& name : names)
        {
            auto typeOpt = instance.GetUniformOverrideType(name);
            if (!typeOpt.has_value())
            {
                continue;
            } // 不应发生，防御性跳过
            UniformOverrideValue u;
            u.name = name;
            u.type = *typeOpt;
            switch (u.type)
            {
                case MaterialUniformType::Float:
                    u.value = instance.GetUniformFloat(name).value_or(0.0f);
                    break;
                case MaterialUniformType::Int:
                    u.value = instance.GetUniformInt(name).value_or(0);
                    break;
                case MaterialUniformType::Vec2:
                    u.value = instance.GetUniformVec2(name).value_or(glm::vec2(0.0f));
                    break;
                case MaterialUniformType::Vec3:
                    u.value = instance.GetUniformVec3(name).value_or(glm::vec3(0.0f));
                    break;
                case MaterialUniformType::Vec4:
                    u.value = instance.GetUniformVec4(name).value_or(glm::vec4(0.0f));
                    break;
                case MaterialUniformType::Mat4:
                    u.value = instance.GetUniformMat4(name).value_or(glm::mat4(1.0f));
                    break;
            }
            data.uniforms.push_back(std::move(u));
        }

        // texture override：pAssetRegistry 非空时调 PathOf 把 handle 反查回
        // path 字符串落盘；nullptr 时 path 留空（reader 端识别空 path 跳过
        // 还原——兼容母 GAP c3 阶段过渡行为）。
        std::vector<std::uint32_t> bindings = instance.GetTextureOverrideBindings();
        data.textures.reserve(bindings.size());
        for (std::uint32_t b : bindings)
        {
            TextureOverrideEntry t;
            t.binding = b;
            if (pAssetRegistry != nullptr)
            {
                auto handle = instance.GetTextureBinding(b);
                // PathOf 处理无效 handle / 已卸载 entry 都安全返回空 view，
                // 不需要再判 IsValid。
                std::string_view pv = pAssetRegistry->PathOf(handle);
                t.path.assign(pv.data(), pv.size());
            }
            // else: t.path 留空
            data.textures.push_back(std::move(t));
        }

        return data;
    }

    void ApplyDataToInstance(
        const MaterialFileData&                 data,
        MaterialInstance&                       instance,
        ::Orange::Engine::Asset::AssetRegistry* pAssetRegistry)
    {
        for (const auto& u : data.uniforms)
        {
            switch (u.type)
            {
                case MaterialUniformType::Float:
                    instance.SetUniform(u.name, std::get<float>(u.value));
                    break;
                case MaterialUniformType::Int:
                    instance.SetUniform(u.name, std::get<std::int32_t>(u.value));
                    break;
                case MaterialUniformType::Vec2:
                    instance.SetUniform(u.name, std::get<glm::vec2>(u.value));
                    break;
                case MaterialUniformType::Vec3:
                    instance.SetUniform(u.name, std::get<glm::vec3>(u.value));
                    break;
                case MaterialUniformType::Vec4:
                    instance.SetUniform(u.name, std::get<glm::vec4>(u.value));
                    break;
                case MaterialUniformType::Mat4:
                    instance.SetUniform(u.name, std::get<glm::mat4>(u.value));
                    break;
            }
        }

        for (const auto& t : data.textures)
        {
            if (t.path.empty())
            {
                // 占位段 / 写盘端尚未填 path —— 跳过还原
                continue;
            }
            if (pAssetRegistry == nullptr)
            {
                ORANGE_LOG_WARN("[MaterialFileIO] texture override binding={} path='{}' "
                                "无 AssetRegistry，跳过",
                                t.binding,
                                t.path);
                continue;
            }
            auto lr = pAssetRegistry->Load<::Orange::Engine::Asset::TextureAsset>(t.path);
            if (lr.IsErr())
            {
                ORANGE_LOG_WARN("[MaterialFileIO] texture path='{}' 加载失败 (code={})",
                                t.path,
                                static_cast<unsigned>(lr.Error()));
                continue;
            }
            instance.SetTexture(t.binding, lr.Value());
        }
    }

} // namespace Orange::Editor::Material
