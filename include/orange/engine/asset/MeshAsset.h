#ifndef ORANGE_ENGINE_ASSET_MESH_ASSET_H
#define ORANGE_ENGINE_ASSET_MESH_ASSET_H

// ---------------------------------------------------------------------------
// MeshAsset —— mesh 资源的 CPU 数据容器。
//
// 当前承载：位置 + 可选 UV + 可选 vertex normal + 可选 tangent + 索引。
// 多 set UV / skin 等更丰富属性仍留待后续扩展。
//
// **磁盘 binary 格式（MeshLoader）演进**：v1 仅 positions+indices；
// v2 追加可选 UV 段；v3 在 v2 之后追加可选 normal 段；v4 再追加可选
// tangent 段（vec4：xyz 方向 + w 手性符号）；v5 在末尾追加 sub-mesh 段
// （每条 indexOffset / indexCount / materialSlot，支持单 mesh 拆多段、
// 各段对应不同 material slot）。Load 兼容全部五个版本，缺失 normal 时
// 由 loader 调用 ComputeSmoothNormalsFromTriangles 补算，缺失 tangent
// 但有 UV+normal 时调 ComputeTangentsFromTriangles 补算（UV-based
// Lengyel fallback；高质量版由 importer 侧 mikktspace 在 import 期烘进
// v4）。Save 始终写 v5。
//
// sub-mesh 语义：mSubMeshes 为空 = 整 mesh 视作单一 sub-mesh、materialSlot
// 0（向后兼容，旧 v1..v4 文件读出后保持空）；非空时每条 SubMesh 描述
// indices[indexOffset, indexOffset+indexCount) 这一段三角形归属哪个
// material slot。顶点 / 索引 buffer 仍是整 mesh 共享一份，sub-mesh 只是
// 索引区间 + slot 路由信息。
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

struct VertexNormal3
{
    float x{0.0f};
    float y{1.0f};
    float z{0.0f};
};

// 切线（vec4）—— xyz 为单位化切线方向，w 为手性符号（±1），shader 端
// 重建副切线 bitangent = cross(normal, tangent.xyz) * tangent.w。这是
// glTF 2.0 TANGENT 属性与 mikktspace setTSpaceBasic 的通用约定，存 w 而
// 非显式 bitangent 省一个通道且天然处理镜像 UV。
struct VertexTangent4
{
    float x{1.0f};
    float y{0.0f};
    float z{0.0f};
    float w{1.0f};
};

// 子网格（sub-mesh）—— 把一个 mesh 的索引缓冲切成若干连续区间，每个
// 区间对应一个 material slot。indices[indexOffset, indexOffset+indexCount)
// 是本段三角形的索引；materialSlot 是渲染端用来在 per-entity 的 material
// slot 列表里挑材质的下标。所有 sub-mesh 共享整 mesh 的同一对 vertex /
// index buffer（GPU 端不拆 buffer，只用 firstIndex / indexCount 分段绘制）。
struct SubMesh
{
    std::uint32_t indexOffset{0};
    std::uint32_t indexCount{0};
    std::uint32_t materialSlot{0};
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

    MeshAsset(std::vector<VertexPosition3> positions,
              std::vector<VertexUV2>       uvs,
              std::vector<VertexNormal3>   normals,
              std::vector<std::uint32_t>   indices)
        : mPositions(std::move(positions))
        , mUVs(std::move(uvs))
        , mNormals(std::move(normals))
        , mIndices(std::move(indices))
    {
    }

    const std::vector<VertexPosition3>& Positions() const noexcept { return mPositions; }
    const std::vector<VertexUV2>&       UVs() const noexcept { return mUVs; }
    const std::vector<VertexNormal3>&   Normals() const noexcept { return mNormals; }
    const std::vector<VertexTangent4>&  Tangents() const noexcept { return mTangents; }
    const std::vector<std::uint32_t>&   Indices() const noexcept { return mIndices; }
    const std::vector<SubMesh>&         SubMeshes() const noexcept { return mSubMeshes; }

    // tangent 通道与 normal / UV 不同，没有进构造函数（避免 5 参重载爆炸）——
    // loader / importer 构造完 mesh 后用本 setter 注入。空 vector 清除 tangent。
    void SetTangents(std::vector<VertexTangent4> tangents) noexcept
    {
        mTangents = std::move(tangents);
    }

    // sub-mesh 列表与 normal / tangent 同款：构造后由 loader / importer 注入。
    // 空 vector 清除分段（退化回整 mesh 单段、slot 0）。
    void SetSubMeshes(std::vector<SubMesh> subMeshes) noexcept
    {
        mSubMeshes = std::move(subMeshes);
    }

    bool        HasUVs() const noexcept { return !mUVs.empty(); }
    bool        HasNormals() const noexcept { return !mNormals.empty(); }
    bool        HasTangents() const noexcept { return !mTangents.empty(); }
    bool        HasSubMeshes() const noexcept { return !mSubMeshes.empty(); }
    std::size_t VertexCount() const noexcept { return mPositions.size(); }
    std::size_t IndexCount() const noexcept { return mIndices.size(); }

    bool Empty() const noexcept { return mPositions.empty() && mIndices.empty(); }

    // 把 normals 替换为"每三角形 face normal、3 个顶点共享"的结果。对
    // 共享顶点的索引网格而言，等价于"最后一个引用本顶点的三角形面法线"
    // ——并非严格 flat shading（要严格 flat 必须 split vertex），主要给
    // tangent / 法线贴图前的 fallback / 调试用。
    void ComputeFlatNormals();

    // 把 normals 替换为"每顶点 smooth normal = 引用本顶点的所有三角形
    // 面法线、按面积加权平均后归一化"。loader 在 v1/v2/v3-no-normal 文
    // 件读取后兜底调用本函数；程序化构造的内置 cube / plane mesh 也走
    // 同一路径，保证渲染端始终拿到 normal。
    void ComputeSmoothNormalsFromTriangles();

    // 用 UV-based Lengyel 方法（Eric Lengyel, "Computing Tangent Space Basis
    // Vectors for an Arbitrary Mesh", 2001）现场补算 per-vertex tangent：按三
    // 角形累加 (du,dv)-加权的边向量、再对 normal 做 Gram-Schmidt 正交化 +
    // 归一化，w 取 dot(cross(N,T), B) 的符号处理镜像 UV。要求 HasUVs() &&
    // HasNormals()——缺任一则清空 tangent（无 UV 时切线无定义）。loader 在
    // v1..v3 / v4-hasTangents=0 文件读取后兜底调用；importer 侧 mikktspace 的
    // 高质量结果优先，仅在 importer 未烘 tangent 时落到本 fallback。
    void ComputeTangentsFromTriangles();

private:
    std::vector<VertexPosition3> mPositions;
    std::vector<VertexUV2>       mUVs;
    std::vector<VertexNormal3>   mNormals;
    std::vector<VertexTangent4>  mTangents;
    std::vector<std::uint32_t>   mIndices;
    // 空 = 整 mesh 单段、slot 0（向后兼容）；非空 = 显式分段。
    std::vector<SubMesh>         mSubMeshes;
};

}  // namespace Orange::Engine::Asset

#endif  // ORANGE_ENGINE_ASSET_MESH_ASSET_H
