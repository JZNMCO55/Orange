#ifndef ORANGE_ENGINE_TOOLS_EDITOR_SHADER_TEMPLATE_META_IO_H
#define ORANGE_ENGINE_TOOLS_EDITOR_SHADER_TEMPLATE_META_IO_H

// ---------------------------------------------------------------------------
// ShaderTemplateMetaIO —— `.template.json` editor 元数据块的编辑器侧 parser。
//
// 与 MaterialFileIO 同款分层：引擎公共面（MaterialSystem /
// ShaderTemplateDesc）仅承载运行时必需字段（name / SPV path / uniforms[
// name+type] / textureSlots）；UI 元数据（widget / displayName / tooltip
// / range / step / components）属编辑器关注点，留在 OrangeEditor 内不污
// 染引擎。
//
// schema v1.1（namespace: render/shader_template）：在 v1.0 基础上 uniforms
// 每条 optional 加 `default` + `editor` 块：
//
//   {
//     "schemaVersion": {"namespace":"render/shader_template","major":1,"minor":1},
//     "templateName": "pbr",
//     "uniforms": [
//       {"name":"uMVP", "type":"mat4", "editor":{"widget":"hidden"}},
//       {"name":"uBaseColor", "type":"vec4", "default":[0.8,0.8,0.8,1.0],
//        "editor":{"widget":"color", "displayName":"Base Color", "tooltip":"..."}},
//       {"name":"uMRA", "type":"vec4", "default":[0.0,0.5,1.0,0.0],
//        "editor":{"widget":"components",
//                  "components":[
//                    {"label":"Metallic",  "widget":"slider", "range":[0,1], "step":0.001},
//                    ...]}}
//     ]
//   }
//
// v1.0 ↔ v1.1 兼容：v1.0 文件无 default / editor 字段 → 视作全部 widget=
// Default 渲染（与未配置时退化路径一致）。
// ---------------------------------------------------------------------------

#include <orange/engine/render/MaterialTypes.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Orange::Editor::ShaderMeta
{

using ::Orange::Engine::Render::MaterialUniformType;

// 单字段 widget 类型。
enum class UniformWidget : std::uint8_t
{
    Default,    // 按 type 选缺省 widget（DragFloat / InputInt / ColorEdit 等）
    Hidden,     // 不渲染（Pipeline 自动 push 的字段如 uMVP / uModel）
    Color,      // vec3 → ColorEdit3；vec4 → ColorEdit4
    Slider,     // float / int + range → SliderFloat / SliderInt
    Drag,       // float 系列 → DragFloat
    Components, // vec4 拆 4 子字段独立 widget（uMRA = Metallic/Roughness/AO/Reserved）
};

// `components` 模式下的单子字段元数据。
struct ComponentMetadata
{
    std::string                              label;
    UniformWidget                            widget{UniformWidget::Default};
    std::optional<std::pair<float, float>>   range;
    std::optional<float>                     step;
    std::string                              tooltip;
};

// 单个 uniform 的完整 UI 元数据。
struct UniformMetadata
{
    std::string                              name;          // GLSL uniform 标识符
    MaterialUniformType                      type{MaterialUniformType::Float};
    UniformWidget                            widget{UniformWidget::Default};
    std::string                              displayName;   // UI 标签（空则用 name）
    std::string                              tooltip;
    std::optional<std::pair<float, float>>   range;
    std::optional<float>                     step;
    std::vector<ComponentMetadata>           components;    // 仅 widget==Components 时有效

    // 默认值（向 MaterialInstance 注入的初始 override）。mat4 最多 16 float；
    // 调用方按 type 决定读前 N 个。schema 内未声明时 hasDefault=false。
    std::array<float, 16>                    defaultValue{};
    bool                                     hasDefault{false};
};

// 整个 .template.json 的元数据视图。
struct ShaderTemplateMeta
{
    std::string                  templateName;
    std::vector<UniformMetadata> uniforms;
};

// 读取单个 .template.json 文件。失败 / 不存在 / schemaVersion 不兼容 →
// nullopt + stderr 记录。v1.0 文件正常解析（editor / default 字段视作缺
// 失，对应 widget=Default + hasDefault=false）。
std::optional<ShaderTemplateMeta> LoadShaderTemplateMeta(
    const std::filesystem::path& jsonPath);

// 扫描目录下所有 `*.template.json`，按字典序返回元数据列表。失败的文件
// 跳过 + log，与 MaterialSystem::RegisterTemplatesFromDirectory 同款。
std::vector<ShaderTemplateMeta> LoadAllShaderTemplateMetas(
    const std::filesystem::path& dir);

}  // namespace Orange::Editor::ShaderMeta

#endif  // ORANGE_ENGINE_TOOLS_EDITOR_SHADER_TEMPLATE_META_IO_H
