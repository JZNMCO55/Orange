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
    const auto& bytes = bytesResult.Value();
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
    // 接受 v1 / v2；其他 version 拒绝（前向兼容由 Save 时 bump version 处理）。
    if (version != kVersionV1 && version != kVersionV2)
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
    if (vertexCount > 0
        && !reader.ReadBytes(positions.data(), vertexCount * sizeof(VertexPosition3)))
    {
        return ResultCode::InvalidArgument;
    }

    std::vector<std::uint32_t> indices;
    indices.resize(indexCount);
    if (indexCount > 0
        && !reader.ReadBytes(indices.data(), indexCount * sizeof(std::uint32_t)))
    {
        return ResultCode::InvalidArgument;
    }

    // v2 追加段：hasUVs (uint8) + 可选 uvs[vertexCount]。
    // v1 文件读到这里已经到 EOF，MeshAsset.UVs() 留空。
    std::vector<VertexUV2> uvs;
    if (version == kVersionV2)
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

    if (uvs.empty())
    {
        return std::make_unique<MeshAsset>(std::move(positions), std::move(indices));
    }
    return std::make_unique<MeshAsset>(std::move(positions),
                                       std::move(uvs),
                                       std::move(indices));
}

Result<void, ResultCode> MeshLoader::Save(std::string_view path, const MeshAsset& mesh)
{
    const auto& positions = mesh.Positions();
    const auto& uvs       = mesh.UVs();
    const auto& indices   = mesh.Indices();

    // UV 段若存在必须 per-vertex 一一对应——本格式约定，避免读取端无法
    // 确定 uv index 与 position index 的关系。
    const bool hasUVs = !uvs.empty();
    if (hasUVs && uvs.size() != positions.size())
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

    return writer.SaveToFile(path);
}

}  // namespace Orange::Engine::Asset
