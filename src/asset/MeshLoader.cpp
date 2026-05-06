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
    if (version != kSupportedVersion)
    {
        return ResultCode::SchemaMismatch;
    }

    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount  = 0;
    if (!reader.Read(vertexCount) || !reader.Read(indexCount))
    {
        return ResultCode::InvalidArgument;
    }

    // 上限做一个软保护——单 mesh 上百万顶点已经远超 Phase 2 场景，
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

    return std::make_unique<MeshAsset>(std::move(positions), std::move(indices));
}

}  // namespace Orange::Engine::Asset
