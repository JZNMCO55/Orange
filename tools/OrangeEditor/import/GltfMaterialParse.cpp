// GltfMaterialParse 实现 —— 详见同名 .h 头注释。本 TU **不** define
// CGLTF_IMPLEMENTATION：cgltf 单 header 的实现在 GltfImporter.cpp / 测试 TU
// 一处 expand，这里只用其类型声明。

#include "GltfMaterialParse.h"

// cgltf 仅取声明（不 expand 实现）。MSVC noisy warning 关掉 —— cgltf 是 C99
// 风格代码，narrowing / unused / deprecated 全套会被 /W4 /WX 当 error。
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable: 4244)  // narrowing
#  pragma warning(disable: 4267)  // size_t → smaller int
#  pragma warning(disable: 4505)  // unreferenced local function
#  pragma warning(disable: 4996)  // deprecated CRT
#  pragma warning(disable: 4100)  // unreferenced formal parameter
#  pragma warning(disable: 4456)  // shadowed local
#endif
#include "cgltf.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

#include <orange/engine/render/MaterialTypes.h>

#include <glm/vec4.hpp>

#include <cstring>
#include <system_error>

namespace Orange::Editor::Import
{

std::string ResolveTextureSource(const cgltf_texture_view& view,
                                 const std::filesystem::path& gltfDir)
{
    if (view.texture == nullptr || view.texture->image == nullptr)
    {
        return {};
    }
    const char* uri = view.texture->image->uri;
    if (uri == nullptr || uri[0] == '\0')
    {
        return {};  // 内嵌 image（.glb buffer view）/ 无 uri
    }
    // data: URI（base64 内嵌）暂不支持。
    if (std::strncmp(uri, "data:", 5) == 0)
    {
        return {};
    }
    // percent-decode（cgltf 提供就地解码；在 uri 副本上做）。
    std::string decoded(uri);
    cgltf_decode_uri(decoded.data());
    decoded.resize(std::strlen(decoded.c_str()));  // 解码可能缩短

    std::error_code ec;
    std::filesystem::path full = gltfDir / decoded;
    if (!std::filesystem::exists(full, ec))
    {
        return {};  // 源贴图缺失 → 跳过（caller 落 default）
    }
    return full.generic_string();
}

int ResolveImageIndex(const cgltf_texture_view& view, const cgltf_data* data)
{
    if (data == nullptr || view.texture == nullptr || view.texture->image == nullptr)
    {
        return -1;
    }
    const cgltf_image* image = view.texture->image;
    // 内嵌判定：GLB buffer_view 非空，或 uri 是 data: 前缀。
    const bool embedded =
        image->buffer_view != nullptr ||
        (image->uri != nullptr && std::strncmp(image->uri, "data:", 5) == 0);
    if (!embedded)
    {
        return -1;
    }
    return static_cast<int>(cgltf_image_index(data, image));
}

GltfMatInfo ExtractGltfMaterial(const cgltf_material* mat,
                                const std::filesystem::path& gltfDir,
                                const cgltf_data* data)
{
    GltfMatInfo info{};
    if (mat == nullptr)
    {
        return info;  // present=false
    }
    info.present = true;

    if (mat->has_pbr_metallic_roughness)
    {
        const auto& pmr = mat->pbr_metallic_roughness;
        info.baseColor[0] = pmr.base_color_factor[0];
        info.baseColor[1] = pmr.base_color_factor[1];
        info.baseColor[2] = pmr.base_color_factor[2];
        info.baseColor[3] = pmr.base_color_factor[3];
        info.metallic     = pmr.metallic_factor;
        info.roughness    = pmr.roughness_factor;
        info.baseColorSrc  = ResolveTextureSource(pmr.base_color_texture, gltfDir);
        info.metalRoughSrc = ResolveTextureSource(pmr.metallic_roughness_texture, gltfDir);
        info.baseColorImageIndex  = ResolveImageIndex(pmr.base_color_texture, data);
        info.metalRoughImageIndex = ResolveImageIndex(pmr.metallic_roughness_texture, data);
    }
    info.normalSrc = ResolveTextureSource(mat->normal_texture, gltfDir);
    info.aoSrc     = ResolveTextureSource(mat->occlusion_texture, gltfDir);
    info.normalImageIndex = ResolveImageIndex(mat->normal_texture, data);
    info.aoImageIndex     = ResolveImageIndex(mat->occlusion_texture, data);

    // occlusionStrength —— 仅当确有 occlusion texture 时才读 cgltf 的
    // occlusion_texture.scale（cgltf 文档：scale 等价于 occlusionTexture.strength）。
    // cgltf 对"没有 occlusionTexture 字段"的 material 是 zero-init，scale==0；
    // 直接读会把 ao 压成 0（全黑 AO）。所以以 texture 指针非空为门：
    //   * 有 occlusion texture → 取 glTF 声明的 strength
    //   * 无 occlusion texture → 保持中性 1.0（构造默认值）
    // 语义对齐 pbr.frag.glsl `ao = clamp(uMRA.z * aoTex.r)`：
    //   有 ao 贴图：uMRA.z=strength → ao = strength·aoTex.r（标准乘法 AO 强度）
    //   无 ao 贴图：uMRA.z=1，default 白贴图 r=1 → ao=1（不衰减，中性）
    if (mat->occlusion_texture.texture != nullptr)
    {
        info.occlusionStrength = mat->occlusion_texture.scale;
    }

    return info;
}

Orange::Editor::Material::MaterialFileData
BuildMaterialFileData(const GltfMatInfo& info, const TextureSlotResolver& resolver)
{
    using ::Orange::Engine::Render::MaterialUniformType;

    Orange::Editor::Material::MaterialFileData mdata;
    mdata.templateName = "pbr";
    if (!info.present)
    {
        return mdata;  // 空壳：仅 templateName
    }

    // 贴图槽 —— binding 与 pbr set 1 对齐（0 baseColor / 1 normal /
    // 2 metalRough / 3 ao）。源路径经 resolver 转成落盘 path；resolver 返回
    // 空（import 失败 / 跳过）则不写该槽。
    auto addSlot = [&](const std::string& src, std::uint32_t binding) {
        if (src.empty()) { return; }
        std::string dest = resolver ? resolver(src) : src;
        if (!dest.empty())
        {
            mdata.textures.push_back({binding, dest});
        }
    };
    addSlot(info.baseColorSrc,  0u);
    addSlot(info.normalSrc,     1u);
    addSlot(info.metalRoughSrc, 2u);
    addSlot(info.aoSrc,         3u);

    // uBaseColor = baseColorFactor（vec4）。
    Orange::Editor::Material::UniformOverrideValue uBase;
    uBase.name  = "uBaseColor";
    uBase.type  = MaterialUniformType::Vec4;
    uBase.value = glm::vec4(info.baseColor[0], info.baseColor[1],
                            info.baseColor[2], info.baseColor[3]);
    mdata.uniforms.push_back(uBase);

    // uMRA = (metallic, roughness, occlusionStrength, 0)。AO 位填 glTF 声明的
    // occlusionStrength（无 occlusion texture 时为中性 1.0），与 shader 的
    // `ao = uMRA.z * aoTex.r` 乘法语义一致——不再写死 1.0（修复：之前 AO 位
    // 恒为 1.0，忽略了模型声明的 occlusionStrength）。
    Orange::Editor::Material::UniformOverrideValue uMra;
    uMra.name  = "uMRA";
    uMra.type  = MaterialUniformType::Vec4;
    uMra.value = glm::vec4(info.metallic, info.roughness, info.occlusionStrength, 0.0f);
    mdata.uniforms.push_back(uMra);

    return mdata;
}

}  // namespace Orange::Editor::Import
