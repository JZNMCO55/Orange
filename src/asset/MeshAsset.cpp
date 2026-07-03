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

        inline float Dot(const Vec3& a, const Vec3& b) noexcept
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        inline Vec3 AddInto(const Vec3& a, const Vec3& b) noexcept
        {
            return {a.x + b.x, a.y + b.y, a.z + b.z};
        }

        inline Vec3 Scale(const Vec3& a, float s) noexcept
        {
            return {a.x * s, a.y * s, a.z * s};
        }

        // 归一化到裸 Vec3（区别于上面返回 VertexNormal3 的 Normalize）；退化时
        // 返回零向量，由 caller 决定 fallback。
        inline Vec3 NormalizeVec3OrZero(const Vec3& v) noexcept
        {
            const float len2 = v.x * v.x + v.y * v.y + v.z * v.z;
            if (len2 <= 1e-20f)
            {
                return {0.0f, 0.0f, 0.0f};
            }
            const float inv = 1.0f / std::sqrt(len2);
            return {v.x * inv, v.y * inv, v.z * inv};
        }

        // 给定单位法线 n，造一条与之正交的任意单位切线（退化 UV / 退化三角形
        // 兜底用）。取 n 与最不平行的基轴叉乘。
        inline Vec3 ArbitraryTangent(const Vec3& n) noexcept
        {
            const Vec3 ref = (std::fabs(n.x) < 0.9f) ? Vec3{1.0f, 0.0f, 0.0f}
                                                     : Vec3{0.0f, 1.0f, 0.0f};
            return NormalizeVec3OrZero(Cross(ref, n));
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

    } // namespace

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

    void MeshAsset::ComputeTangentsFromTriangles()
    {
        // 切线在没有 UV 或没有 normal 时无定义——清空让 HasTangents() 返回
        // false，渲染端按"无 tangent"路径处理（degenerate fallback 在 GPU 端
        // 由 default flat-normal 法线贴图配合恒等 TBN 给出）。
        if (mUVs.size() != mPositions.size() || mNormals.size() != mPositions.size() || mIndices.size() < 3)
        {
            mTangents.clear();
            return;
        }

        const std::size_t vtxCount = mPositions.size();
        std::vector<Vec3> tanAccum(vtxCount, Vec3{0.0f, 0.0f, 0.0f});
        std::vector<Vec3> bitanAccum(vtxCount, Vec3{0.0f, 0.0f, 0.0f});

        const std::size_t triCount = mIndices.size() / 3;
        for (std::size_t t = 0; t < triCount; ++t)
        {
            const std::uint32_t i0 = mIndices[t * 3 + 0];
            const std::uint32_t i1 = mIndices[t * 3 + 1];
            const std::uint32_t i2 = mIndices[t * 3 + 2];
            if (i0 >= vtxCount || i1 >= vtxCount || i2 >= vtxCount)
            {
                continue;
            }

            const Vec3 e1 = Sub(mPositions[i1], mPositions[i0]);
            const Vec3 e2 = Sub(mPositions[i2], mPositions[i0]);

            const float du1 = mUVs[i1].u - mUVs[i0].u;
            const float dv1 = mUVs[i1].v - mUVs[i0].v;
            const float du2 = mUVs[i2].u - mUVs[i0].u;
            const float dv2 = mUVs[i2].v - mUVs[i0].v;

            // UV 行列式：退化（共线 UV / 零面积 UV）时跳过本三角形贡献，
            // 避免 1/0 把整段 accumulation 污染成 NaN。
            const float det = du1 * dv2 - du2 * dv1;
            if (std::fabs(det) <= 1e-12f)
            {
                continue;
            }
            const float r = 1.0f / det;

            const Vec3 tDir = Scale(AddInto(Scale(e1, dv2), Scale(e2, -dv1)), r);
            const Vec3 bDir = Scale(AddInto(Scale(e2, du1), Scale(e1, -du2)), r);

            tanAccum[i0]   = AddInto(tanAccum[i0], tDir);
            tanAccum[i1]   = AddInto(tanAccum[i1], tDir);
            tanAccum[i2]   = AddInto(tanAccum[i2], tDir);
            bitanAccum[i0] = AddInto(bitanAccum[i0], bDir);
            bitanAccum[i1] = AddInto(bitanAccum[i1], bDir);
            bitanAccum[i2] = AddInto(bitanAccum[i2], bDir);
        }

        mTangents.assign(vtxCount, VertexTangent4{1.0f, 0.0f, 0.0f, 1.0f});
        for (std::size_t v = 0; v < vtxCount; ++v)
        {
            const Vec3 n{mNormals[v].x, mNormals[v].y, mNormals[v].z};
            const Vec3 nUnit = NormalizeVec3OrZero(n);
            // normal 自身退化（不该发生，loader 已补算）—— 兜底单位 X 切线。
            const Vec3 nSafe = (nUnit.x == 0.0f && nUnit.y == 0.0f && nUnit.z == 0.0f)
                                   ? Vec3{0.0f, 1.0f, 0.0f}
                                   : nUnit;

            // Gram-Schmidt：T' = normalize(T - N·(N·T))，把切线投影到法线
            // 切平面内。
            const Vec3 tAcc   = tanAccum[v];
            Vec3       tOrtho = AddInto(tAcc, Scale(nSafe, -Dot(nSafe, tAcc)));
            Vec3       tUnit  = NormalizeVec3OrZero(tOrtho);
            if (tUnit.x == 0.0f && tUnit.y == 0.0f && tUnit.z == 0.0f)
            {
                // 顶点未被任何有效 UV 三角形覆盖（孤立 / 全退化）—— 造一条
                // 与法线正交的任意切线，保证 TBN 非奇异。
                tUnit = ArbitraryTangent(nSafe);
            }

            // 手性：w = sign(dot(cross(N,T), B_accum))，负值表示镜像 UV，
            // shader 端 bitangent = cross(N,T)*w 才与几何一致。
            const Vec3  cross  = Cross(nSafe, tUnit);
            const float handed = (Dot(cross, bitanAccum[v]) < 0.0f) ? -1.0f : 1.0f;

            mTangents[v] = VertexTangent4{tUnit.x, tUnit.y, tUnit.z, handed};
        }
    }

} // namespace Orange::Engine::Asset
