#ifndef ORANGE_ENGINE_ASSET_MESH_ASSET_H
#define ORANGE_ENGINE_ASSET_MESH_ASSET_H

// ---------------------------------------------------------------------------
// MeshAsset —— mesh 资源的 CPU 数据容器。
//
// Phase 2 范围只承载位置 + 可选 UV + 索引——刚好够支持
// 03_textured_quad 这条最小渲染路径。法线 / tangent / 多 set UV /
// skin 等更丰富属性留给后续 phase。
//
// **磁盘 binary 格式（MeshLoader v1）当前仅写入 positions + indices**；
// UV 字段是给"程序式构造的 mesh"留的内存路径——sample 用
// AssetRegistry::Insert 把直接 new 出来的 MeshAsset 塞进 registry
// 时可以一并填 UV。后续把 UV / 法线写入 .orme 文件时升 schema_version
// 与 loader 即可，不破公共 API。
//
// 这一层不做任何 GPU 上传——上传发生在 Render 模块把 MeshAsset 翻
// 成 OrangeRender RHI buffer 的时刻。Asset 层只保证字节正确进了内存。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Orange::Engine::Asset
{

struct VertexPosition3
{
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
};

struct VertexUV2
{
    float u{0.0f};
    float v{0.0f};
};

class ORANGE_ENGINE_API MeshAsset
{
public:
    MeshAsset() = default;

    MeshAsset(std::vector<VertexPosition3> positions, std::vector<std::uint32_t> indices)
        : mPositions(std::move(positions))
        , mIndices(std::move(indices))
    {
    }

    MeshAsset(std::vector<VertexPosition3> positions,
              std::vector<VertexUV2>       uvs,
              std::vector<std::uint32_t>   indices)
        : mPositions(std::move(positions))
        , mUVs(std::move(uvs))
        , mIndices(std::move(indices))
    {
    }

    const std::vector<VertexPosition3>& Positions() const noexcept { return mPositions; }
    const std::vector<VertexUV2>&       UVs() const noexcept { return mUVs; }
    const std::vector<std::uint32_t>&   Indices() const noexcept { return mIndices; }

    bool        HasUVs() const noexcept { return !mUVs.empty(); }
    std::size_t VertexCount() const noexcept { return mPositions.size(); }
    std::size_t IndexCount() const noexcept { return mIndices.size(); }

    bool Empty() const noexcept { return mPositions.empty() && mIndices.empty(); }

private:
    std::vector<VertexPosition3> mPositions;
    std::vector<VertexUV2>       mUVs;
    std::vector<std::uint32_t>   mIndices;
};

}  // namespace Orange::Engine::Asset

#endif  // ORANGE_ENGINE_ASSET_MESH_ASSET_H
