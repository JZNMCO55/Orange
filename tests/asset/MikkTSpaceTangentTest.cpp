// MikkTSpace 切线生成（A2 Inc2）单元测试。
//
// 守的是 tools/OrangeEditor/import/MeshTangentGen.cpp 的集成正确性——
// 即 SMikkTSpaceInterface 回调按 index 喂对了 pos/normal/uv，且 per-face-vertex
// 切线被正确 re-weld 回索引网格。MikkTSpace 算法本身（vendor/mikktspace）是
// Blender / Godot / Unreal 公用的成熟实现，不在本测试覆盖范围；这里只验证
// "我的接线没把数据喂错 / 没把 re-weld 写错"。
//
// 用一个已知答案的平面四边形：XY 平面、法线 +Z、UV 沿 +X(U) / +Y(V)。
// 对这种标准布局，切线方向必然 ≈ ±X（U 方向），handedness w ≈ ±1。

#include "MeshTangentGen.h"

#include <orange/engine/asset/MeshAsset.h>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using Orange::Editor::Import::GenerateMikkTSpaceTangents;
using Orange::Engine::Asset::VertexNormal3;
using Orange::Engine::Asset::VertexPosition3;
using Orange::Engine::Asset::VertexTangent4;
using Orange::Engine::Asset::VertexUV2;

namespace
{

// 单位 XY 四边形：z=0、法线 +Z、UV(u→+X, v→+Y)、两三角 CCW。
void MakeQuad(std::vector<VertexPosition3>& pos,
              std::vector<VertexUV2>&       uv,
              std::vector<VertexNormal3>&   nrm,
              std::vector<std::uint32_t>&   idx)
{
    pos = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    uv  = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    nrm = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    idx = {0, 1, 2, 0, 2, 3};
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[MikkTSpaceTangentTest] running\n");

    // ---- case 1：标准四边形 → 切线沿 ±X、单位长、w = ±1 -----------------
    {
        std::vector<VertexPosition3> pos;
        std::vector<VertexUV2>       uv;
        std::vector<VertexNormal3>   nrm;
        std::vector<std::uint32_t>   idx;
        MakeQuad(pos, uv, nrm, idx);
        std::vector<VertexTangent4> tangents;

        const bool ok = GenerateMikkTSpaceTangents(pos, uv, nrm, idx, tangents);
        assert(ok && "标准 UV+normal 四边形必须成功生成切线");

        // re-weld 后所有顶点切线一致（同一平面、同一 UV 方向）→ 不应分裂。
        assert(tangents.size() == pos.size() && "切线数与（re-weld 后）顶点数一致");
        assert(pos.size() == uv.size() && pos.size() == nrm.size()
               && "re-weld 后 pos/uv/normal 等长");
        assert(idx.size() == 6 && "两三角索引数不变");

        for (const auto& t : tangents)
        {
            const float len = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
            std::fprintf(stderr, "  tangent=(%.3f, %.3f, %.3f) w=%.3f len=%.3f\n",
                         t.x, t.y, t.z, t.w, len);
            assert(std::fabs(len - 1.0f) < 1e-3f && "切线单位长");
            // U 沿 +X → 切线方向被 X 主导。
            assert(std::fabs(t.x) > 0.9f && "切线应沿 ±X（U 方向）");
            assert(std::fabs(t.y) < 0.1f && std::fabs(t.z) < 0.1f
                   && "切线 Y/Z 分量近 0");
            assert(std::fabs(std::fabs(t.w) - 1.0f) < 1e-3f && "handedness w = ±1");
        }
    }

    // ---- case 2：缺 UV → 返回 false 且数组原样不动（落 Lengyel fallback）--
    {
        std::vector<VertexPosition3> pos;
        std::vector<VertexUV2>       uv;
        std::vector<VertexNormal3>   nrm;
        std::vector<std::uint32_t>   idx;
        MakeQuad(pos, uv, nrm, idx);
        uv.clear();  // 模拟无 UV
        const std::size_t posCountBefore = pos.size();
        std::vector<VertexTangent4> tangents;

        const bool ok = GenerateMikkTSpaceTangents(pos, uv, nrm, idx, tangents);
        assert(!ok && "缺 UV 时必须返回 false（切线无定义）");
        assert(pos.size() == posCountBefore && "失败路径不得改动输入数组");
        assert(tangents.empty() && "失败路径不写 outTangents");
    }

    // ---- case 3：退化输入（indices 非 3 倍数）→ false ---------------------
    {
        std::vector<VertexPosition3> pos;
        std::vector<VertexUV2>       uv;
        std::vector<VertexNormal3>   nrm;
        std::vector<std::uint32_t>   idx;
        MakeQuad(pos, uv, nrm, idx);
        idx.push_back(0);  // 7 个索引，非 3 倍数
        std::vector<VertexTangent4> tangents;

        const bool ok = GenerateMikkTSpaceTangents(pos, uv, nrm, idx, tangents);
        assert(!ok && "indices 非 3 倍数必须返回 false");
    }

    std::fprintf(stdout, "[MikkTSpaceTangentTest] done\n");
    return 0;
}
