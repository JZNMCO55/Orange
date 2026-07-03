// MikkTSpace 切线生成 —— 真实复杂模型验证（A2 Inc2）。
//
// MikkTSpaceTangentTest 用合成平面四边形验证回调接线 + re-weld 的"已知答案"。
// 本测试补一层：在**真实有机曲面**模型上跑同一条 importer 路径
// （cgltf 解析几何 → GenerateMikkTSpaceTangents），断言对每个顶点都产出了
// 单位长切线 + 合法手性 + 全覆盖，并统计切线对法线的正交程度（mikktspace
// basic 输出的切线与顶点法线正交，是切线空间的核心性质）。
//
// fixture（gltf_test/Avocado/Avocado.gltf）是 session 内下载的未跟踪资产，
// 故本测试由 CMake `if(EXISTS)` 门控——fixture 在就编 + 跑，干净 checkout
// 自动跳过（不阻塞 CI）。Avocado：有机弯曲面 + 自带 normal map + 非金属，
// 是法线贴图切线的理想验证对象。

#include "MeshTangentGen.h"

#include <orange/engine/asset/MeshAsset.h>

// cgltf 单 header IMPLEMENTATION 仅本 TU expand；warning 关掉同 GltfImporter.cpp。
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#pragma warning(disable : 4505)
#pragma warning(disable : 4996)
#pragma warning(disable : 4100)
#pragma warning(disable : 4456)
#endif
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#ifndef ORANGE_ENGINE_GLTF_FIXTURE
#error "ORANGE_ENGINE_GLTF_FIXTURE 必须由 CMake 注入 .gltf 路径"
#endif

using Orange::Editor::Import::GenerateMikkTSpaceTangents;
using Orange::Engine::Asset::VertexNormal3;
using Orange::Engine::Asset::VertexPosition3;
using Orange::Engine::Asset::VertexTangent4;
using Orange::Engine::Asset::VertexUV2;

namespace
{

    const cgltf_accessor* FindAttr(const cgltf_primitive& prim, cgltf_attribute_type wanted)
    {
        for (cgltf_size i = 0; i < prim.attributes_count; ++i)
        {
            if (prim.attributes[i].type == wanted && prim.attributes[i].index == 0)
            {
                return prim.attributes[i].data;
            }
        }
        return nullptr;
    }

} // namespace

int main()
{
    std::fprintf(stdout, "[MikkTSpaceRealModelTest] fixture=%s\n", ORANGE_ENGINE_GLTF_FIXTURE);

    cgltf_options options{};
    cgltf_data*   data = nullptr;
    if (cgltf_parse_file(&options, ORANGE_ENGINE_GLTF_FIXTURE, &data) != cgltf_result_success)
    {
        std::fprintf(stderr, "  cgltf_parse_file failed\n");
        return 1;
    }
    if (cgltf_load_buffers(&options, data, ORANGE_ENGINE_GLTF_FIXTURE) != cgltf_result_success)
    {
        std::fprintf(stderr, "  cgltf_load_buffers failed\n");
        cgltf_free(data);
        return 1;
    }

    // 合并所有三角 primitive 的几何（与 GltfImporter 同款，简化版）。
    std::vector<VertexPosition3> positions;
    std::vector<VertexUV2>       uvs;
    std::vector<VertexNormal3>   normals;
    std::vector<std::uint32_t>   indices;

    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi)
    {
        const cgltf_mesh& mesh = data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi)
        {
            const cgltf_primitive& prim = mesh.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles)
            {
                continue;
            }
            const cgltf_accessor* posAcc = FindAttr(prim, cgltf_attribute_type_position);
            const cgltf_accessor* nrmAcc = FindAttr(prim, cgltf_attribute_type_normal);
            const cgltf_accessor* uvAcc  = FindAttr(prim, cgltf_attribute_type_texcoord);
            if (posAcc == nullptr || nrmAcc == nullptr || uvAcc == nullptr)
            {
                continue;
            }

            const std::uint32_t baseIdx  = static_cast<std::uint32_t>(positions.size());
            const cgltf_size    vtxCount = posAcc->count;
            for (cgltf_size v = 0; v < vtxCount; ++v)
            {
                float p[3] = {0, 0, 0}, n[3] = {0, 1, 0}, t[2] = {0, 0};
                cgltf_accessor_read_float(posAcc, v, p, 3);
                cgltf_accessor_read_float(nrmAcc, v, n, 3);
                cgltf_accessor_read_float(uvAcc, v, t, 2);
                positions.push_back({p[0], p[1], p[2]});
                normals.push_back({n[0], n[1], n[2]});
                uvs.push_back({t[0], t[1]});
            }
            if (prim.indices != nullptr)
            {
                for (cgltf_size i = 0; i < prim.indices->count; ++i)
                {
                    indices.push_back(baseIdx + static_cast<std::uint32_t>(
                                                    cgltf_accessor_read_index(prim.indices, i)));
                }
            }
        }
    }
    cgltf_free(data);

    std::fprintf(stderr, "  parsed: %zu verts, %zu indices (%zu tris)\n",
                 positions.size(), indices.size(), indices.size() / 3);
    assert(!positions.empty() && !indices.empty() && "fixture 应含三角几何");

    std::vector<VertexTangent4> tangents;
    const bool                  ok = GenerateMikkTSpaceTangents(positions, uvs, normals, indices, tangents);
    assert(ok && "真实模型（有 UV + normal）必须成功生成切线");

    // re-weld 后全覆盖 + 数组一致。
    assert(tangents.size() == positions.size() && "每个（re-weld 后）顶点都有切线");
    assert(uvs.size() == positions.size() && normals.size() == positions.size() && "re-weld 后 pos/uv/normal 等长");
    assert((indices.size() % 3) == 0 && "三角索引完整");

    // 逐顶点检查：单位长切线 + 手性 ±1；统计切线⊥法线程度。
    float  maxAbsDotTN = 0.0f;
    double sumAbsDotTN = 0.0;
    for (std::size_t i = 0; i < tangents.size(); ++i)
    {
        const VertexTangent4& t   = tangents[i];
        const float           len = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
        assert(std::fabs(len - 1.0f) < 1e-2f && "切线单位长");
        assert(std::fabs(std::fabs(t.w) - 1.0f) < 1e-2f && "handedness w = ±1");

        // 归一化法线后点积——mikktspace basic 切线与顶点法线正交，|dot|≈0。
        const VertexNormal3& n    = normals[i];
        const float          nlen = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (nlen > 1e-6f)
        {
            const float dot = (t.x * n.x + t.y * n.y + t.z * n.z) / nlen;
            const float ad  = std::fabs(dot);
            maxAbsDotTN     = (ad > maxAbsDotTN) ? ad : maxAbsDotTN;
            sumAbsDotTN += ad;
        }
    }
    const double meanAbsDotTN = sumAbsDotTN / static_cast<double>(tangents.size());
    std::fprintf(stderr, "  |dot(T,N)|: max=%.5f mean=%.5f （越接近 0 越正交）\n",
                 maxAbsDotTN, meanAbsDotTN);
    // 正交性是切线空间核心性质——给一个宽松上限守 mikktspace 没退化。
    assert(maxAbsDotTN < 0.05f && "切线应与顶点法线近正交（mikktspace basic 约定）");

    std::fprintf(stdout, "[MikkTSpaceRealModelTest] done\n");
    return 0;
}
