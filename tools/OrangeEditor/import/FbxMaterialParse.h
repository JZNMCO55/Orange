#ifndef ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_FBX_MATERIAL_PARSE_H
#define ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_FBX_MATERIAL_PARSE_H

// ---------------------------------------------------------------------------
// FbxMaterialParse —— FBX material（Phong / Lambert）→ 引擎 pbr 模板
// MaterialFileData 的共享 seam（FbxImporter 单 mesh 路径 + FbxSceneImporter
// scene 路径共用，避免两处各写一份 material 提取 —— 对标 GltfMaterialParse 之
// 于 GltfImporter / GltfSceneImporter）。
//
// 提取口径（与历史 FbxImporter 内联实现逐字一致，迁出后行为零变化）：
//   * uBaseColor = (diffuseColor.rgb × diffuseFactor, opacity)
//   * uMRA       = (0, roughness, 1, 0)；roughness 由 Phong shininess 推导
//                  sqrt(2/(shininess+2))，clamp[0.04,1]；metallic=0 / ao=1 中性
//   * uEmissive  = (emissiveColor.rgb × emissiveFactor, 0)，仅非零时写
//   * 贴图       : DIFFUSE → binding 0、NORMAL → binding 1、EMISSIVE → binding 4
//
// 本头只引 OpenFBX 声明（ofbx.h）+ MaterialFileIO；ofbx 实现 TU 由 CMake 接进
// target（与 FbxImporter 一致）。
// ---------------------------------------------------------------------------

#include "../MaterialFileIO.h"  // Material::MaterialFileData

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ofbx
{
struct Material;
}

namespace Orange::Editor::Import
{

// 从 FBX material 的某类贴图取源文件磁盘路径（相对 fbxDir 解析）。FBX 贴图记两
// 种文件名：绝对 filename（导出机器路径，常失效）+ 相对 relativeFileName。优先
// 相对路径相对 fbxDir 解析，其次绝对 filename 直接试，都不存在返回空。
std::string ResolveFbxTexture(const ofbx::Material* mat,
                              int textureType,  // ofbx::Texture::TextureType
                              const std::filesystem::path& fbxDir);

// FBX material → pbr 模板 scalar uniform（不含贴图）。贴图源由调用方在 import
// 阶段经真实 ImportTexture 落盘后单独回填 —— 避免把未落盘的源路径误写进 .material。
// resolver 非空时也填贴图（保留与历史 BuildFbxMaterialFileData(resolver) 等价的
// 一步式路径，供单元测试 / 简单场景用）。
Material::MaterialFileData BuildFbxMaterialFileData(
    const ofbx::Material* mat, const std::filesystem::path& fbxDir,
    const std::function<std::string(const std::string&)>& resolver);

// material 的三类贴图源路径（binding → 磁盘源路径）。在 ofbx scene destroy 之前
// 解析（destroy 后 Material* 悬空）。
std::vector<std::pair<std::uint32_t, std::string>> StageFbxTextureSources(
    const ofbx::Material* mat, const std::filesystem::path& fbxDir);

}  // namespace Orange::Editor::Import

#endif  // ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_FBX_MATERIAL_PARSE_H
