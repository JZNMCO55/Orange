#ifndef ORANGE_ENGINE_ASSET_MESH_LOADER_H
#define ORANGE_ENGINE_ASSET_MESH_LOADER_H

// ---------------------------------------------------------------------------
// MeshLoader —— 引擎自有 mesh 二进制格式的同步加载器。
//
// 文件格式（little-endian、紧凑、无 padding）：
//
// v1（历史）：
//   bytes 0..3   magic = 'O' 'R' 'M' 'E'
//   bytes 4..7   version = 1
//   bytes 8..11  vertexCount  (uint32)
//   bytes 12..15 indexCount   (uint32)
//   bytes 16..   positions[vertexCount] : float[3]
//   随后         indices[indexCount]    : uint32
//
// v2（GAP-2026-05-16）：v1 末尾追加 hasUVs 字节 + 可选 uvs 段：
//   ... 同 v1 头部 + positions + indices
//   1B hasUVs (0 / 1)
//   if hasUVs == 1:
//       uvs[vertexCount] : float[2]
//
// v3（GAP-2026-05-17）：v2 末尾再追加 hasNormals 字节 + 可选 normals 段：
//   ... 同 v2 头部 + positions + indices + (hasUVs + uvs)
//   1B hasNormals (0 / 1)
//   if hasNormals == 1:
//       normals[vertexCount] : float[3]
//
// v4（GAP-2026-05-25）：v3 末尾再追加 hasTangents 字节 + 可选 tangents 段：
//   ... 同 v3 + (hasNormals + normals)
//   1B hasTangents (0 / 1)
//   if hasTangents == 1:
//       tangents[vertexCount] : float[4]  (xyz 方向 + w 手性符号)
//
// Load 同时支持读 v1 / v2 / v3 / v4：
//   * v1 / v2 / v3-hasNormals=0 / v4-hasNormals=0：loader 自动调
//     MeshAsset::ComputeSmoothNormalsFromTriangles 现场补算 normal，
//     渲染端从 v3 起统一假定 MeshAsset.Normals() 非空。
//   * 缺 tangent 但有 UV+normal（v1..v3 全部 / v4-hasTangents=0）：loader
//     自动调 MeshAsset::ComputeTangentsFromTriangles 补算（UV-based
//     Lengyel fallback）；importer 侧 mikktspace 烘出的高质量 tangent 经
//     v4-hasTangents=1 直接读出，优先于 fallback。
//   * v4-hasTangents=1：直接使用磁盘 tangent。
// Save 永远写 v4 格式；输入 MeshAsset.HasUVs() / HasNormals() /
// HasTangents() 决定是否写 UV / normal / tangent 段。
//
// 选择自有格式而不接 OBJ / glTF 是有意为之：避免在
// Asset 模块上线时同时解决"第三方解析器 vendoring"这个独立问题。后
// 续（在 stb / tinygltf 等 vendor 落地后）可以新增对应的
// MeshLoader 子类，与本类并存。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/asset/IAssetLoader.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/core/Result.h>

#include <memory>
#include <string_view>

namespace Orange::Engine::Asset
{

class ORANGE_ENGINE_API MeshLoader final : public IAssetLoader<MeshAsset>
{
public:
    // 'O','R','M','E' 按 little-endian 读出：内存字节序 0x4F,0x52,0x4D,0x45
    // 反推 uint32 = 0x454D524F。
    static constexpr std::uint32_t kMagic = 0x454D524FU;
    // Load 支持的最小 / 最大 version。Save 总是写 kLatestVersion。
    static constexpr std::uint32_t kVersionV1     = 1;
    static constexpr std::uint32_t kVersionV2     = 2;
    static constexpr std::uint32_t kVersionV3     = 3;
    static constexpr std::uint32_t kVersionV4     = 4;
    static constexpr std::uint32_t kLatestVersion = kVersionV4;

    MeshLoader() = default;
    ~MeshLoader() override = default;

    Result<std::unique_ptr<MeshAsset>, ResultCode> Load(std::string_view path) override;

    // 把 MeshAsset 序列化到磁盘 .mesh 文件（v3 格式）。caller 保证目标
    // 目录已存在；本函数不创建目录。HasUVs() / HasNormals() 决定是否
    // 写对应可选段。
    // 失败码：IoError（无法写文件） / InvalidArgument（顶点数据不一致）。
    static Result<void, ResultCode> Save(std::string_view path,
                                         const MeshAsset& mesh);
};

}  // namespace Orange::Engine::Asset

#endif  // ORANGE_ENGINE_ASSET_MESH_LOADER_H
