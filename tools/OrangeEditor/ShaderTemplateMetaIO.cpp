// ShaderTemplateMetaIO 实现 —— JsonReader 解析 .template.json editor 块。

#include "ShaderTemplateMetaIO.h"

#include <orange/engine/core/Log.h>
#include <orange/engine/core/Serialization.h>

#include <algorithm>
#include <string_view>
#include <system_error>

namespace Orange::Editor::ShaderMeta
{
namespace
{

using ::Orange::Engine::JsonReader;
using ::Orange::Engine::SchemaVersion;

// 与 src/render/MaterialSystem.cpp 内 ParseUniformType 同款字符串映射。
// 复制独立一份避免引擎公共面暴露内部 helper；type enum 已是公共面，调
// 用方按 enum 派发即可，无需共享 parser。
std::optional<MaterialUniformType> ParseUniformType(std::string_view s)
{
    if (s == "float") { return MaterialUniformType::Float; }
    if (s == "vec2")  { return MaterialUniformType::Vec2;  }
    if (s == "vec3")  { return MaterialUniformType::Vec3;  }
    if (s == "vec4")  { return MaterialUniformType::Vec4;  }
    if (s == "int")   { return MaterialUniformType::Int;   }
    if (s == "mat4")  { return MaterialUniformType::Mat4;  }
    return std::nullopt;
}

UniformWidget ParseWidget(std::string_view s)
{
    if (s == "hidden")     { return UniformWidget::Hidden;     }
    if (s == "color")      { return UniformWidget::Color;      }
    if (s == "slider")     { return UniformWidget::Slider;     }
    if (s == "drag")       { return UniformWidget::Drag;       }
    if (s == "components") { return UniformWidget::Components; }
    // 未知 widget 字符串 → Default + 调用方按 type 选缺省 widget。
    return UniformWidget::Default;
}

// 按 type 推断 default 值数组的有效长度。mat4 = 16，vec4 = 4，vec3 = 3，
// vec2 = 2，float / int = 1。
std::size_t DefaultValueCount(MaterialUniformType type)
{
    switch (type)
    {
        case MaterialUniformType::Mat4:  return 16;
        case MaterialUniformType::Vec4:  return 4;
        case MaterialUniformType::Vec3:  return 3;
        case MaterialUniformType::Vec2:  return 2;
        case MaterialUniformType::Float: return 1;
        case MaterialUniformType::Int:   return 1;
    }
    return 0;
}

// 读 `<base>/range` 为 [min, max] 浮点对。失败返回 nullopt。
std::optional<std::pair<float, float>> ReadRangePair(
    const JsonReader& reader, const std::string& base)
{
    const std::string path = base + "/range";
    if (reader.ArraySize(path) != 2) { return std::nullopt; }
    float buf[2] = {0.0f, 0.0f};
    if (!reader.ReadFloatArray(path, buf, 2)) { return std::nullopt; }
    return std::make_pair(buf[0], buf[1]);
}

// 读 `<base>/step` 为 float。失败返回 nullopt。
std::optional<float> ReadStep(const JsonReader& reader, const std::string& base)
{
    double v = 0.0;
    if (!reader.ReadFloat(base + "/step", v)) { return std::nullopt; }
    return static_cast<float>(v);
}

// 解析 components 数组（widget==Components 模式下的子字段元数据）。
std::vector<ComponentMetadata> ReadComponents(
    const JsonReader& reader, const std::string& editorBase)
{
    std::vector<ComponentMetadata> out;
    const std::string compsBase = editorBase + "/components";
    const std::size_t n = reader.ArraySize(compsBase);
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const std::string base = compsBase + "/" + std::to_string(i);
        ComponentMetadata cm;
        std::string widgetStr;
        reader.ReadString(base + "/label",   cm.label);
        if (reader.ReadString(base + "/widget", widgetStr))
        {
            cm.widget = ParseWidget(widgetStr);
        }
        cm.range   = ReadRangePair(reader, base);
        cm.step    = ReadStep(reader, base);
        reader.ReadString(base + "/tooltip", cm.tooltip);
        out.push_back(std::move(cm));
    }
    return out;
}

// 解析单条 uniform 的完整元数据（含 default + editor 块）。
UniformMetadata ReadUniformMetadata(const JsonReader& reader,
                                    const std::string& uniformBase)
{
    UniformMetadata um;
    reader.ReadString(uniformBase + "/name", um.name);

    std::string typeStr;
    if (reader.ReadString(uniformBase + "/type", typeStr))
    {
        if (auto t = ParseUniformType(typeStr); t.has_value())
        {
            um.type = *t;
        }
    }

    // default 数组（按 type 决定预期长度）。
    const std::size_t expected = DefaultValueCount(um.type);
    const std::string defaultPath = uniformBase + "/default";
    if (expected > 0 && reader.ArraySize(defaultPath) == expected)
    {
        if (reader.ReadFloatArray(defaultPath,
                                  um.defaultValue.data(),
                                  expected))
        {
            um.hasDefault = true;
        }
    }

    // editor 子块。
    const std::string editorBase = uniformBase + "/editor";
    if (reader.Has(editorBase))
    {
        std::string widgetStr;
        if (reader.ReadString(editorBase + "/widget", widgetStr))
        {
            um.widget = ParseWidget(widgetStr);
        }
        reader.ReadString(editorBase + "/displayName", um.displayName);
        reader.ReadString(editorBase + "/tooltip", um.tooltip);
        um.range = ReadRangePair(reader, editorBase);
        um.step  = ReadStep(reader, editorBase);
        if (um.widget == UniformWidget::Components)
        {
            um.components = ReadComponents(reader, editorBase);
        }
    }
    return um;
}

}  // namespace

std::optional<ShaderTemplateMeta> LoadShaderTemplateMeta(
    const std::filesystem::path& jsonPath)
{
    auto readerResult = JsonReader::FromFile(jsonPath.string());
    if (readerResult.IsErr())
    {
        ORANGE_LOG_ERROR("ShaderTemplateMetaIO: 解析失败 (path={}, msg={})",
                         jsonPath.string(),
                         readerResult.Error().message);
        return std::nullopt;
    }
    const JsonReader& reader = readerResult.Value();

    auto verResult = reader.ReadSchemaVersion("schemaVersion");
    if (verResult.IsErr())
    {
        ORANGE_LOG_ERROR("ShaderTemplateMetaIO: schemaVersion 缺失 (path={})",
                         jsonPath.string());
        return std::nullopt;
    }
    const SchemaVersion ver = verResult.Value();
    if (ver.Namespace() != "render/shader_template" || ver.Major() != 1)
    {
        ORANGE_LOG_ERROR("ShaderTemplateMetaIO: schemaVersion 不兼容 "
                         "(path={}, ns={}, ver={}.{})",
                         jsonPath.string(), ver.Namespace(),
                         ver.Major(), ver.Minor());
        return std::nullopt;
    }
    // minor=0 文件兼容：editor / default 字段视作缺失，所有 uniform 走
    // Default widget；与"未配置时全 DragFloat"退化路径一致。

    ShaderTemplateMeta meta;
    if (!reader.ReadString("templateName", meta.templateName)
        || meta.templateName.empty())
    {
        ORANGE_LOG_ERROR("ShaderTemplateMetaIO: templateName 缺失 (path={})",
                         jsonPath.string());
        return std::nullopt;
    }

    const std::size_t uniformCount = reader.ArraySize("uniforms");
    meta.uniforms.reserve(uniformCount);
    for (std::size_t i = 0; i < uniformCount; ++i)
    {
        const std::string base = "uniforms/" + std::to_string(i);
        meta.uniforms.push_back(ReadUniformMetadata(reader, base));
    }
    return meta;
}

std::vector<ShaderTemplateMeta> LoadAllShaderTemplateMetas(
    const std::filesystem::path& dir)
{
    namespace fs = std::filesystem;
    std::vector<ShaderTemplateMeta> out;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
    {
        ORANGE_LOG_WARN("ShaderTemplateMetaIO: 目录不存在 (dir={})",
                        dir.string());
        return out;
    }

    std::vector<fs::path> candidates;
    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file(ec)) { continue; }
        const fs::path& p = entry.path();
        const std::string fn = p.filename().string();
        constexpr std::string_view kSuffix = ".template.json";
        if (fn.size() < kSuffix.size()) { continue; }
        if (fn.compare(fn.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0)
        {
            continue;
        }
        candidates.push_back(p);
    }
    std::sort(candidates.begin(), candidates.end());

    out.reserve(candidates.size());
    for (const auto& p : candidates)
    {
        if (auto m = LoadShaderTemplateMeta(p); m.has_value())
        {
            out.push_back(std::move(*m));
        }
    }
    return out;
}

}  // namespace Orange::Editor::ShaderMeta
