#ifndef ORANGE_ENGINE_ASSET_MESH_ASSET_H
#define ORANGE_ENGINE_ASSET_MESH_ASSET_H

// ---------------------------------------------------------------------------
// MeshAsset —— mesh 资源的 CPU 数据容器。
//
// 当前阶段只承载顶点位置 + 索引：跑通 "ECS → Pipeline → OrangeRender"
// 的最小数据流。法线 / UV / tangent 等等比 Phase 2 的"画一个 mesh"目
// 标更靠后的属性留给后续 task 扩展，避免 Task 02 因为属性面太宽而失
// 焦。
//
// 这一层不做任何 GPU 上传——上传发生在 Render 模块（Phase 2 Task
// 07）把 MeshAsset 翻译成 OrangeRender RHI buffer 的时刻。Asset 层只
// 保证字节正确进了内存。
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

class ORANGE_ENGINE_API MeshAsset
{
public:
    MeshAsset() = default;

    MeshAsset(std::vector<VertexPosition3> positions, std::vector<std::uint32_t> indices)
        : mPositions(std::move(positions))
        , mIndices(std::move(indices))
    {
    }

    const std::vector<VertexPosition3>& Positions() const noexcept { return mPositions; }
    const std::vector<std::uint32_t>&   Indices() const noexcept { return mIndices; }

    std::size_t VertexCount() const noexcept { return mPositions.size(); }
    std::size_t IndexCount() const noexcept { return mIndices.size(); }

    bool Empty() const noexcept { return mPositions.empty() && mIndices.empty(); }

private:
    std::vector<VertexPosition3> mPositions;
    std::vector<std::uint32_t>   mIndices;
};

}  // namespace Orange::Engine::Asset

#endif  // ORANGE_ENGINE_ASSET_MESH_ASSET_H
