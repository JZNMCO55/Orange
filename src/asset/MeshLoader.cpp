#include "orange/engine/asset/MeshLoader.h"

#include "orange/engine/core/Serialization.h"

#include <cstdint>
#include <cstring>

namespace Orange::Engine::Asset
{

    Result<std::unique_ptr<MeshAsset>, ResultCode> MeshLoader::Load(std::string_view path)
    {
        auto bytesResult = BinaryReader::LoadFile(path);
        if (bytesResult.IsErr())
        {
            return bytesResult.Error();
        }
        const auto&  bytes = bytesResult.Value();
        BinaryReader reader{bytes.data(), bytes.size()};

        std::uint32_t magic = 0;
        if (!reader.Read(magic))
        {
            return ResultCode::InvalidArgument;
        }
        if (magic != kMagic)
        {
            return ResultCode::SchemaMismatch;
        }

        std::uint32_t version = 0;
        if (!reader.Read(version))
        {
            return ResultCode::InvalidArgument;
        }
        // 接受 v1 / v2 / v3 / v4 / v5；其他 version 拒绝（前向兼容由 Save 时 bump version 处理）。
        if (version != kVersionV1 && version != kVersionV2 && version != kVersionV3 && version != kVersionV4 && version != kVersionV5)
        {
            return ResultCode::SchemaMismatch;
        }

        std::uint32_t vertexCount = 0;
        std::uint32_t indexCount  = 0;
        if (!reader.Read(vertexCount) || !reader.Read(indexCount))
        {
            return ResultCode::InvalidArgument;
        }

        // 上限做一个软保护——单 mesh 上百万顶点已经远超当前场景，
        // 防御性地拒绝异常大的值，避免下面 vector 一口气分配几个 GB。
        constexpr std::uint32_t kMaxCount = 16u * 1024u * 1024u;
        if (vertexCount > kMaxCount || indexCount > kMaxCount)
        {
            return ResultCode::OutOfRange;
        }

        std::vector<VertexPosition3> positions;
        positions.resize(vertexCount);
        if (vertexCount > 0 && !reader.ReadBytes(positions.data(), vertexCount * sizeof(VertexPosition3)))
        {
            return ResultCode::InvalidArgument;
        }

        std::vector<std::uint32_t> indices;
        indices.resize(indexCount);
        if (indexCount > 0 && !reader.ReadBytes(indices.data(), indexCount * sizeof(std::uint32_t)))
        {
            return ResultCode::InvalidArgument;
        }

        // v2 及以上追加段：hasUVs (uint8) + 可选 uvs[vertexCount]。
        // v1 文件读到这里已经到 EOF，MeshAsset.UVs() 留空。
        std::vector<VertexUV2> uvs;
        if (version >= kVersionV2)
        {
            std::uint8_t hasUVs = 0;
            if (!reader.Read(hasUVs))
            {
                // hasUVs 标志缺失 = 文件被截断；拒绝而非降级到无 UV，避免
                // 调试时静默丢失 UV 数据。
                return ResultCode::InvalidArgument;
            }
            if (hasUVs != 0 && vertexCount > 0)
            {
                uvs.resize(vertexCount);
                if (!reader.ReadBytes(uvs.data(), vertexCount * sizeof(VertexUV2)))
                {
                    return ResultCode::InvalidArgument;
                }
            }
        }

        // v3 及以上追加段：hasNormals (uint8) + 可选 normals[vertexCount]。
        // v1 / v2 文件读到这里已经到 EOF，下面 fallback 会调
        // ComputeSmoothNormalsFromTriangles 补算。
        std::vector<VertexNormal3> normals;
        if (version >= kVersionV3)
        {
            std::uint8_t hasNormals = 0;
            if (!reader.Read(hasNormals))
            {
                return ResultCode::InvalidArgument;
            }
            if (hasNormals != 0 && vertexCount > 0)
            {
                normals.resize(vertexCount);
                if (!reader.ReadBytes(normals.data(), vertexCount * sizeof(VertexNormal3)))
                {
                    return ResultCode::InvalidArgument;
                }
            }
        }

        // v4 追加段：hasTangents (uint8) + 可选 tangents[vertexCount]（float[4]）。
        // v1..v3 文件读到这里已经到 EOF，下面 fallback 会在有 UV+normal 时调
        // ComputeTangentsFromTriangles 补算。
        std::vector<VertexTangent4> tangents;
        if (version >= kVersionV4)
        {
            std::uint8_t hasTangents = 0;
            if (!reader.Read(hasTangents))
            {
                return ResultCode::InvalidArgument;
            }
            if (hasTangents != 0 && vertexCount > 0)
            {
                tangents.resize(vertexCount);
                if (!reader.ReadBytes(tangents.data(), vertexCount * sizeof(VertexTangent4)))
                {
                    return ResultCode::InvalidArgument;
                }
            }
        }

        // v5 追加段：subMeshCount (uint32) + 每条 (indexOffset, indexCount,
        // materialSlot)。v1..v4 文件读到这里已经到 EOF，subMeshes 留空 =
        // 整 mesh 单段（渲染端按整 mesh 单 material 路径处理）。
        std::vector<SubMesh> subMeshes;
        if (version >= kVersionV5)
        {
            std::uint32_t subMeshCount = 0;
            if (!reader.Read(subMeshCount))
            {
                return ResultCode::InvalidArgument;
            }
            // 软上限：sub-mesh 段数远小于索引数；用 indexCount 上限的同款防御，
            // 避免坏文件 count 字段诱导一口气分配巨量内存。
            if (subMeshCount > kMaxCount)
            {
                return ResultCode::OutOfRange;
            }
            subMeshes.resize(subMeshCount);
            for (std::uint32_t i = 0; i < subMeshCount; ++i)
            {
                if (!reader.Read(subMeshes[i].indexOffset) || !reader.Read(subMeshes[i].indexCount) || !reader.Read(subMeshes[i].materialSlot))
                {
                    return ResultCode::InvalidArgument;
                }
            }
        }

        std::unique_ptr<MeshAsset> asset;
        if (uvs.empty() && normals.empty())
        {
            asset = std::make_unique<MeshAsset>(std::move(positions), std::move(indices));
        }
        else if (normals.empty())
        {
            asset = std::make_unique<MeshAsset>(std::move(positions),
                                                std::move(uvs),
                                                std::move(indices));
        }
        else if (uvs.empty())
        {
            // 无 UV 但有 normal 的情况：通过 4 参构造塞空 UV，避免再加一个
            // 构造重载。VertexUV2 默认 {0,0}，shader 端拿不到有意义 UV 但
            // 也不报错——本路径只在手工合成 mesh 时会走到。
            std::vector<VertexUV2> emptyUvs;
            emptyUvs.resize(positions.size());
            asset = std::make_unique<MeshAsset>(std::move(positions),
                                                std::move(emptyUvs),
                                                std::move(normals),
                                                std::move(indices));
        }
        else
        {
            asset = std::make_unique<MeshAsset>(std::move(positions),
                                                std::move(uvs),
                                                std::move(normals),
                                                std::move(indices));
        }

        // 缺 normal 时现场补算 smooth normal（v1 / v2 / v3-hasNormals=0 均
        // 走这里）。渲染端从 v3 起统一假定 MeshAsset.Normals() 非空。
        if (asset != nullptr && !asset->HasNormals() && !asset->Positions().empty())
        {
            asset->ComputeSmoothNormalsFromTriangles();
        }

        // 磁盘 tangent（v4-hasTangents=1）优先注入；缺 tangent 但已有 UV+normal
        // 时（v1..v3 全部 / v4-hasTangents=0）落 Lengyel fallback 现场补算。无
        // UV 的 mesh 切线无定义，HasTangents() 保持 false，渲染端走无 tangent 路径。
        if (asset != nullptr)
        {
            if (!tangents.empty())
            {
                asset->SetTangents(std::move(tangents));
            }
            else if (asset->HasUVs() && asset->HasNormals() && !asset->Positions().empty())
            {
                asset->ComputeTangentsFromTriangles();
            }
        }

        // sub-mesh 段（v5）原样注入；v1..v4 文件 subMeshes 为空，保持整 mesh
        // 单段语义。这里不校验 indexOffset/indexCount 是否越界——渲染端按段
        // 绘制时自然受 GPU index buffer 边界保护，loader 层不强加几何约束。
        if (asset != nullptr && !subMeshes.empty())
        {
            asset->SetSubMeshes(std::move(subMeshes));
        }
        return asset;
    }

    Result<void, ResultCode> MeshLoader::Save(std::string_view path, const MeshAsset& mesh)
    {
        const auto& positions = mesh.Positions();
        const auto& uvs       = mesh.UVs();
        const auto& normals   = mesh.Normals();
        const auto& indices   = mesh.Indices();

        // UV / normal 段若存在必须 per-vertex 一一对应——本格式约定，避免
        // 读取端无法确定 attribute index 与 position index 的对应关系。
        const auto& tangents    = mesh.Tangents();
        const bool  hasUVs      = !uvs.empty();
        const bool  hasNormals  = !normals.empty();
        const bool  hasTangents = !tangents.empty();
        if (hasUVs && uvs.size() != positions.size())
        {
            return ResultCode::InvalidArgument;
        }
        if (hasNormals && normals.size() != positions.size())
        {
            return ResultCode::InvalidArgument;
        }
        if (hasTangents && tangents.size() != positions.size())
        {
            return ResultCode::InvalidArgument;
        }

        BinaryWriter writer;
        writer.Write<std::uint32_t>(kMagic);
        writer.Write<std::uint32_t>(kLatestVersion);
        writer.Write<std::uint32_t>(static_cast<std::uint32_t>(positions.size()));
        writer.Write<std::uint32_t>(static_cast<std::uint32_t>(indices.size()));

        if (!positions.empty())
        {
            writer.WriteBytes(positions.data(),
                              positions.size() * sizeof(VertexPosition3));
        }
        if (!indices.empty())
        {
            writer.WriteBytes(indices.data(),
                              indices.size() * sizeof(std::uint32_t));
        }

        writer.Write<std::uint8_t>(hasUVs ? 1u : 0u);
        if (hasUVs)
        {
            writer.WriteBytes(uvs.data(), uvs.size() * sizeof(VertexUV2));
        }

        writer.Write<std::uint8_t>(hasNormals ? 1u : 0u);
        if (hasNormals)
        {
            writer.WriteBytes(normals.data(), normals.size() * sizeof(VertexNormal3));
        }

        writer.Write<std::uint8_t>(hasTangents ? 1u : 0u);
        if (hasTangents)
        {
            writer.WriteBytes(tangents.data(), tangents.size() * sizeof(VertexTangent4));
        }

        // v5 sub-mesh 段：subMeshCount + 每条 (indexOffset, indexCount,
        // materialSlot)。空则写 count=0，等价于整 mesh 单段。SubMesh 是
        // POD（3 个 uint32），但逐字段写以免依赖结构 padding 假设。
        const auto& subMeshes = mesh.SubMeshes();
        writer.Write<std::uint32_t>(static_cast<std::uint32_t>(subMeshes.size()));
        for (const auto& sm : subMeshes)
        {
            writer.Write<std::uint32_t>(sm.indexOffset);
            writer.Write<std::uint32_t>(sm.indexCount);
            writer.Write<std::uint32_t>(sm.materialSlot);
        }

        return writer.SaveToFile(path);
    }

} // namespace Orange::Engine::Asset
