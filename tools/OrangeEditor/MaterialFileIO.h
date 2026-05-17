#ifndef ORANGE_ENGINE_TOOLS_EDITOR_MATERIAL_FILE_IO_H
#define ORANGE_ENGINE_TOOLS_EDITOR_MATERIAL_FILE_IO_H

// ---------------------------------------------------------------------------
// MaterialFileIO —— .material 文件的 v1.1 schema 读写 helper（编辑器侧）。
//
// Schema v1.1（namespace: render/material_instance）：
//   {
//     "schemaVersion": {"namespace":"render/material_instance","major":1,"minor":1},
//     "templateName":  "toon",
//     "uniforms": [
//       {"name":"ToonColor",    "type":"vec4",  "value":[0.8,0.3,0.2,1.0]},
//       {"name":"OutlineWidth", "type":"float", "value":0.05}
//     ],
//     "textures": [
//       {"binding":0, "path":"assets/textures/foo.png"}
//     ]
//   }
//
// v1.0 → v1.1 兼容：v1.0 文件只有 schemaVersion + templateName 两个字段；
// reader 把 uniforms / textures 视作空（无 override 还原），等价于 GAP
// 原文要求的 "无 uniforms 字段时 default override empty"。
//
// 这个 helper 不在引擎公共面——它只服务 OrangeEditor 工具链（DemoWorld
// bake + InspectorPanel Save）。引擎侧公共面只到 MaterialSystem /
// MaterialInstance，schema 解析永远是消费方的事（与"公共头无裸 nlohmann
// json"纪律一致）。
// ---------------------------------------------------------------------------

#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialTypes.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace Orange::Engine::Asset
{
class AssetRegistry;
}  // namespace Orange::Engine::Asset

namespace Orange::Editor::Material
{

using ::Orange::Engine::Render::MaterialUniformType;

// 单个 uniform override 的运行时数据。type 决定 value 走哪个 std::variant
// 分支；写盘时按 type 决定 JSON value 形态。
struct UniformOverrideValue
{
    std::string         name;
    MaterialUniformType type{MaterialUniformType::Float};

    // 用 variant 包六种 type；与 MaterialInstance::SetUniform 重载一一对应。
    std::variant<float,
                 std::int32_t,
                 glm::vec2,
                 glm::vec3,
                 glm::vec4,
                 glm::mat4> value;
};

// 单个 texture override 槽。path 是 AssetRegistry::Load<TextureAsset> 喂
// 的 path（典型："assets/textures/xxx.png"）。
struct TextureOverrideEntry
{
    std::uint32_t binding{0};
    std::string   path;
};

// .material 文件的 v1.1 表示（也兼容 v1.0：uniforms/textures 为空）。
struct MaterialFileData
{
    std::string                          templateName;
    std::vector<UniformOverrideValue>    uniforms;
    std::vector<TextureOverrideEntry>    textures;
};

// 读取 .material 文件。文件不存在 / JSON 解析失败 / schemaVersion 不属
// 于 render/material_instance namespace → nullopt + stderr 记录。v1.0
// 文件正常返回（uniforms / textures 为空）；v1.x 高 minor 也接受
// （前向兼容）。templateName 必须存在且非空，否则 nullopt。
std::optional<MaterialFileData> ReadMaterialFile(const std::string& path);

// 写 .material 文件。永远以 v1.1 schema 写盘（即使 data.uniforms /
// textures 为空，也写 "uniforms":[] / "textures":[]——读回 v1.0 兼容
// 路径与之等价）。失败 → stderr 记录 + 返回 false。
bool WriteMaterialFile(const std::string& path, const MaterialFileData& data);

// 从一个已有 MaterialInstance 抽出当前的 override 状态（uniforms +
// textures）填到 MaterialFileData——templateName 由调用方填，因为
// MaterialInstance 只持 Material* 不知道 system 里的注册名。
//
// 通过引擎侧 GetUniformOverrideNames / GetUniformOverrideType /
// GetUniformXxx + GetTextureOverrideBindings 读取——这些 API 是 GAP G2
// 引擎侧落地的产物（commit c2）。
//
// 已知约束（texture override 写盘）：当前 AssetRegistry 不提供
// AssetHandle<TextureAsset> → 源路径反查 API。BuildDataFromInstance
// 不能从 instance 里读出"texture binding 当年是从哪个 path 加载的"。
// 因此 textures[] 段在写盘端目前**只填 binding，path 写为空串**，让
// reader 读到 path 为空时跳过——schema 字段就位但 round-trip 在
// texture override 路径上仍残缺。补 AssetRegistry 反查 API 是另一个
// GAP，本 helper 端写盘语义会随之自动完善。
MaterialFileData BuildDataFromInstance(
    const ::Orange::Engine::Render::MaterialInstance& instance,
    const std::string&                                templateName);

// 把 MaterialFileData 内的 uniform / texture override 应用到一个
// MaterialInstance 上。typically 在 LoadMaterialFile → CreateInstance
// 之后调用，把 .material 文件里的调参还原到运行时实例。
//
// pAssetRegistry 用于 texture path → AssetHandle 的加载（调
// AssetRegistry::Load<TextureAsset>）。registry == nullptr 时 texture
// override 跳过（仍记录 stderr 警告）。
void ApplyDataToInstance(
    const MaterialFileData&                       data,
    ::Orange::Engine::Render::MaterialInstance&   instance,
    ::Orange::Engine::Asset::AssetRegistry*       pAssetRegistry);

}  // namespace Orange::Editor::Material

#endif  // ORANGE_ENGINE_TOOLS_EDITOR_MATERIAL_FILE_IO_H
