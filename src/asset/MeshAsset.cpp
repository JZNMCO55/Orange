// MeshAsset 的 normal 计算 helper 实现。
//
// 公共 API 不依赖 glm —— 这里也用裸 float 算，避免在 Asset 模块（位于
// 引擎下层）引入 glm 头依赖；几何计算极简，自己写 cross / normalize 即可。

#include "orange/engine/asset/MeshAsset.h"

#include <cmath>
#include <cstddef>

namespace Orange::Engine::Asset
{
namespace
{

struct Vec3
{
    float x;
    float y;
    float z;
};

inline Vec3 Sub(const VertexPosition3& a, const VertexPosition3& b) noexcept
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3 Cross(const Vec3& a, const Vec3& b) noexcept
{
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

// 不归一化——cross 长度 = 平行四边形面积 = 2× 三角形面积，刚好作为
// smooth normal 的面积权重。flat 用法里 caller 自己归一化。
inline Vec3 TriangleAreaWeightedNormal(const VertexPosition3& a,
                                       const VertexPosition3& b,
                                       const VertexPosition3& c) noexcept
{
    return Cross(Sub(b, a), Sub(c, a));
}

inline VertexNormal3 Normalize(const Vec3& v) noexcept
{
    const float len2 = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len2 <= 0.0f)
    {
        // 退化三角形：兜底 +Y，与 VertexNormal3 default 一致，避免后续
        // shader 端读到 NaN/0 法线。
        return {0.0f, 1.0f, 0.0f};
    }
    const float inv = 1.0f / std::sqrt(len2);
    return {v.x * inv, v.y * inv, v.z * inv};
}

}  // namespace

void MeshAsset::ComputeFlatNormals()
{
    mNormals.assign(mPositions.size(), VertexNormal3{0.0f, 1.0f, 0.0f});
    if (mIndices.size() < 3)
    {
        return;
    }

    const std::size_t triCount = mIndices.size() / 3;
    for (std::size_t t = 0; t < triCount; ++t)
    {
        const std::uint32_t i0 = mIndices[t * 3 + 0];
        const std::uint32_t i1 = mIndices[t * 3 + 1];
        const std::uint32_t i2 = mIndices[t * 3 + 2];
        if (i0 >= mPositions.size() || i1 >= mPositions.size() || i2 >= mPositions.size())
        {
            continue;
        }
        const Vec3 n = TriangleAreaWeightedNormal(
            mPositions[i0], mPositions[i1], mPositions[i2]);
        const VertexNormal3 nn = Normalize(n);
        // 共享顶点情况下后写覆盖先写——严格 flat 需要 split vertex，本
        // helper 仅作为缺 normal 时的兜底。
        mNormals[i0] = nn;
        mNormals[i1] = nn;
        mNormals[i2] = nn;
    }
}

void MeshAsset::ComputeSmoothNormalsFromTriangles()
{
    mNormals.assign(mPositions.size(), VertexNormal3{0.0f, 0.0f, 0.0f});
    if (mIndices.size() < 3)
    {
        // 没有三角形可累加 —— 全部退化到 +Y。
        for (auto& n : mNormals)
        {
            n = {0.0f, 1.0f, 0.0f};
        }
        return;
    }

    const std::size_t triCount = mIndices.size() / 3;
    for (std::size_t t = 0; t < triCount; ++t)
    {
        const std::uint32_t i0 = mIndices[t * 3 + 0];
        const std::uint32_t i1 = mIndices[t * 3 + 1];
        const std::uint32_t i2 = mIndices[t * 3 + 2];
        if (i0 >= mPositions.size() || i1 >= mPositions.size() || i2 >= mPositions.size())
        {
            continue;
        }
        const Vec3 weighted = TriangleAreaWeightedNormal(
            mPositions[i0], mPositions[i1], mPositions[i2]);
        // 累加 cross（含面积权重）——大三角形对其顶点的影响自然大于
        // 小三角形，常见硬表面 mesh 视觉表现稳定。
        mNormals[i0].x += weighted.x;
        mNormals[i0].y += weighted.y;
        mNormals[i0].z += weighted.z;
        mNormals[i1].x += weighted.x;
        mNormals[i1].y += weighted.y;
        mNormals[i1].z += weighted.z;
        mNormals[i2].x += weighted.x;
        mNormals[i2].y += weighted.y;
        mNormals[i2].z += weighted.z;
    }

    for (auto& n : mNormals)
    {
        n = Normalize({n.x, n.y, n.z});
    }
}

}  // namespace Orange::Engine::Asset
