#ifndef ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_GLTF_MATERIAL_PARSE_H
#define ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_GLTF_MATERIAL_PARSE_H

// ---------------------------------------------------------------------------
// GltfMaterialParse —— glTF 2.0 material 通道解析的可独立测试 seam。
//
// 把"cgltf_material → PBR 通道（factor + 贴图源路径）"以及"PBR 通道 →
// .material 文件数据（templateName / uniform override / texture 槽）"两段
// 纯逻辑从 GltfImporter.cpp 的 RunGltfImport 里抽出来：
//
//   * ExtractGltfMaterial：只读 cgltf，无 EditorHost / 无文件落盘，把 base
//     color / metallic / roughness / occlusionStrength + 4 通道贴图源路径
//     抽进 GltfMatInfo
//   * BuildMaterialFileData：把 GltfMatInfo 翻成 MaterialFileData（pbr 模板
//     + uBaseColor / uMRA uniform + binding 0/1/2/3 贴图槽）。贴图源路径 →
//     落盘 path 的转换交给调用方注入的 resolver 回调（真实 importer 传
//     ImportTexture 的结果，headless 测试可传 identity）
//
// 这样 GltfMaterialImportTest 能在不链接整套 EditorHost（AudioEngine /
// ThumbnailService / Vulkan / ImGui）的前提下，锁住"glTF material factor /
// 贴图 → .material 字段"这条链；RunGltfImport 复用同两个函数，测试覆盖的
// 就是真实 import 路径用到的同一份逻辑。
// ---------------------------------------------------------------------------

#include "../MaterialFileIO.h"  // Orange::Editor::Material::MaterialFileData

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

// cgltf 前向声明 —— 本头只用指针 / 引用，不需要完整定义，避免把 cgltf.h
// （及其 CGLTF_IMPLEMENTATION expand）拖进每个 include 本头的 TU。
struct cgltf_material;
struct cgltf_texture_view;
struct cgltf_data;

namespace Orange::Editor::Import
{

// glTF material 通道解析的中间结果。在 cgltf_free 之前从 cgltf_material 抽出，
// free 之后用于 import 贴图 + 写 .material。
//
// 每个贴图槽有两种来源（互斥）：
//   * 外部文件 uri → 对应 *Src 存"源文件磁盘路径"（gltfDir/uri 解析后）。
//   * 内嵌图像（.glb buffer_view / data: URI）→ 对应 *Src 为空，*ImageIndex
//     存该 image 在 cgltf_data::images[] 的下标（>= 0）；由 importer 层在
//     cgltf_free 之前取字节落盘后再回填 *Src，交给现有贴图 co-locate 流程。
// 两者都空 / index < 0 表示该通道无贴图。
struct GltfMatInfo
{
    bool        present{false};
    std::string baseColorSrc;
    std::string normalSrc;
    std::string metalRoughSrc;
    std::string aoSrc;
    int         baseColorImageIndex{-1};
    int         normalImageIndex{-1};
    int         metalRoughImageIndex{-1};
    int         aoImageIndex{-1};
    float       baseColor[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float       metallic{1.0f};
    float       roughness{1.0f};
    // occlusionStrength —— glTF occlusionTexture.strength（cgltf 里是
    // occlusion_texture.scale）。无 occlusion texture 时取中性 1.0；填进
    // uMRA.z，与 pbr.frag.glsl 的 `ao = uMRA.z * aoTex.r` 乘法语义对齐
    // （无 ao 贴图时 default 白贴图 r=1 → ao=1·1=1 中性）。
    float       occlusionStrength{1.0f};
};

// 把一个 cgltf_texture_view 解析成外部源文件路径（相对 gltf 所在目录）。仅解析
// 外部 uri 引用且磁盘存在的 image；.glb 内嵌（buffer_view，uri==null）或 data:
// URI 在这里返回空——内嵌来源改走 ResolveImageIndex + importer 层提取。
std::string ResolveTextureSource(const cgltf_texture_view& view,
                                 const std::filesystem::path& gltfDir);

// 把一个 cgltf_texture_view 解析成内嵌 image 的 images[] 下标（仅当该 image
// 是内嵌来源：buffer_view 非空，或 uri 为 data: 前缀）。非内嵌 / 无 image /
// data 为 null 时返回 -1。data 用于 cgltf_image_index 定位下标。
int ResolveImageIndex(const cgltf_texture_view& view, const cgltf_data* data);

// 从 cgltf_material 抽出 PBR 通道（factor + 外部贴图源路径 + 内嵌 image 下标 +
// occlusionStrength）。mat == nullptr 时返回 present=false 的空 info。gltfDir 用
// 于把外部 image uri 解析成路径；data 用于把内嵌 image 解析成 images[] 下标
// （data 为 null 时跳过内嵌下标解析，外部 uri 路径仍正常——保持旧 caller 兼容）。
// 不触碰文件系统以外的任何引擎状态（无 host 依赖）。
GltfMatInfo ExtractGltfMaterial(const cgltf_material* mat,
                                const std::filesystem::path& gltfDir,
                                const cgltf_data* data = nullptr);

// 贴图槽 resolver 回调：输入"贴图源路径"，输出"落盘后可写进 .material 的
// path"。真实 importer 走 ImportTexture（copy + load + .meta）后回填 destPath；
// 返回空字符串 = 该槽 import 失败 / 跳过（不写进 textures 数组）。
using TextureSlotResolver = std::function<std::string(const std::string& srcTexPath)>;

// 把 GltfMatInfo 翻成 MaterialFileData：
//   * templateName = "pbr"
//   * uBaseColor = baseColorFactor（vec4）
//   * uMRA = (metallic, roughness, occlusionStrength, 0)（vec4）
//   * textures：binding 0 baseColor / 1 normal / 2 metalRough / 3 ao，
//     每个非空源路径经 resolver 转成落盘 path 后入数组（resolver 返回空则跳过）
//
// info.present == false 时返回空壳（仅 templateName="pbr"，无 uniform / texture）。
Orange::Editor::Material::MaterialFileData
BuildMaterialFileData(const GltfMatInfo& info, const TextureSlotResolver& resolver);

}  // namespace Orange::Editor::Import

#endif  // ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_GLTF_MATERIAL_PARSE_H
