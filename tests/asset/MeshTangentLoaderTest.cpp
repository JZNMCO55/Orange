// MeshLoader .mesh v4（tangent 段）+ MeshAsset::ComputeTangentsFromTriangles
// 的单元测试。覆盖（GAP-2026-05-25 A2 / G2）：
//
//   1. 计算 + round-trip：单位 quad（pos+uv+normal）现场算 tangent → 断言
//      单位长 + 与 normal 正交 + w=±1 + 期望方向；Save(v4) → Load → tangent
//      字节级一致（±eps）。
//   2. v4-hasTangents=0 fallback：mesh 不带 tangent Save → Load 端按 UV+normal
//      现场补算，HasTangents() 变 true。
//   3. migrator：手写 v3 文件（无 tangent 段）在 v4 loader 下成功 Load +
//      fallback 补 tangent；手写 v1 文件（无 UV）Load 成功但 HasTangents()=false。
//   4. 无 UV mesh：ComputeTangentsFromTriangles 清空，HasTangents()=false。
//
// 走最小 <cassert> + 独立 main()，exit 0 即通过（与其余引擎测试一致）。

#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/MeshLoader.h>
#include <orange/engine/core/Result.h>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

using Orange::Engine::ResultCode;
using namespace Orange::Engine::Asset;

namespace
{
namespace fs = std::filesystem;

fs::path TempDir()
{
    auto root = fs::temp_directory_path() / "orange_engine_mesh_tangent_test";
    fs::create_directories(root);
    return root;
}

bool Approx(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) <= eps;
}

template <typename T>
void AppendPod(std::vector<std::uint8_t>& bytes, const T& value)
{
    const auto* src = reinterpret_cast<const std::uint8_t*>(&value);
    bytes.insert(bytes.end(), src, src + sizeof(T));
}

void AppendBytes(std::vector<std::uint8_t>& bytes, const void* data, std::size_t count)
{
    const auto* src = static_cast<const std::uint8_t*>(data);
    bytes.insert(bytes.end(), src, src + count);
}

void WriteAll(const fs::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

// 单位 quad（XY 平面，法线 +Z，UV 0..1）—— 已知 tangent 应为 +X。
MeshAsset MakeQuad()
{
    std::vector<VertexPosition3> pos = {
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
    };
    std::vector<VertexUV2> uv = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
    };
    std::vector<VertexNormal3> nrm = {
        {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f},
    };
    std::vector<std::uint32_t> idx = {0, 1, 2, 0, 2, 3};
    return MeshAsset(std::move(pos), std::move(uv), std::move(nrm), std::move(idx));
}

// 校验单个 tangent：单位长 + 与对应法线正交 + w=±1。
void AssertValidTangent(const VertexTangent4& t, const VertexNormal3& n)
{
    const float len = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
    assert(Approx(len, 1.0f) && "tangent 必须单位长");
    const float dotNT = t.x * n.x + t.y * n.y + t.z * n.z;
    assert(Approx(dotNT, 0.0f) && "tangent 必须与 normal 正交");
    assert((Approx(t.w, 1.0f) || Approx(t.w, -1.0f)) && "w 必须 ±1");
}

// ---- 1. 计算 + v4 round-trip --------------------------------------------
void TestComputeAndRoundTrip(const fs::path& root)
{
    MeshAsset quad = MakeQuad();
    assert(!quad.HasTangents());
    quad.ComputeTangentsFromTriangles();
    assert(quad.HasTangents());
    assert(quad.Tangents().size() == quad.VertexCount());

    for (std::size_t v = 0; v < quad.VertexCount(); ++v)
    {
        AssertValidTangent(quad.Tangents()[v], quad.Normals()[v]);
    }
    // 已知 quad → tangent +X，w +1。
    const VertexTangent4& t0 = quad.Tangents()[0];
    assert(Approx(t0.x, 1.0f) && Approx(t0.y, 0.0f) && Approx(t0.z, 0.0f));
    assert(Approx(t0.w, 1.0f));

    const fs::path meshPath = root / "quad_v4.mesh";
    auto saveRes = MeshLoader::Save(meshPath.generic_string(), quad);
    assert(saveRes.IsOk());

    MeshLoader loader;
    auto loadRes = loader.Load(meshPath.generic_string());
    assert(loadRes.IsOk());
    const MeshAsset& loaded = *loadRes.Value();
    assert(loaded.HasTangents());
    assert(loaded.Tangents().size() == quad.Tangents().size());
    for (std::size_t v = 0; v < loaded.Tangents().size(); ++v)
    {
        const auto& a = quad.Tangents()[v];
        const auto& b = loaded.Tangents()[v];
        assert(Approx(a.x, b.x) && Approx(a.y, b.y)
               && Approx(a.z, b.z) && Approx(a.w, b.w)
               && "tangent 必须 round-trip 一致");
    }
    std::printf("[ok] compute + v4 round-trip\n");
}

// ---- 2. v4-hasTangents=0 fallback ---------------------------------------
void TestFallbackOnSavedWithoutTangents(const fs::path& root)
{
    MeshAsset quad = MakeQuad();  // 不调 ComputeTangents → 不带 tangent
    assert(!quad.HasTangents());

    const fs::path meshPath = root / "quad_no_tan.mesh";
    auto saveRes = MeshLoader::Save(meshPath.generic_string(), quad);
    assert(saveRes.IsOk());

    MeshLoader loader;
    auto loadRes = loader.Load(meshPath.generic_string());
    assert(loadRes.IsOk());
    const MeshAsset& loaded = *loadRes.Value();
    // loader 端 fallback 应已补算（有 UV + normal）。
    assert(loaded.HasTangents() && "缺 tangent + 有 UV/normal → loader 应补算");
    for (std::size_t v = 0; v < loaded.Tangents().size(); ++v)
    {
        AssertValidTangent(loaded.Tangents()[v], loaded.Normals()[v]);
    }
    std::printf("[ok] v4 hasTangents=0 -> loader fallback\n");
}

// ---- 3. migrator：手写 v3 / v1 文件在 v4 loader 下 ------------------------
void TestV3FileLoadsUnderV4Loader(const fs::path& root)
{
    // 手写 v3：magic + ver3 + counts + pos + idx + hasUVs + uvs + hasNormals + normals
    std::vector<std::uint8_t> bytes;
    AppendPod(bytes, MeshLoader::kMagic);
    AppendPod(bytes, MeshLoader::kVersionV3);

    const VertexPosition3 pos[4] = {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    const VertexUV2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const VertexNormal3 nrm[4] = {
        {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    const std::uint32_t idx[6] = {0, 1, 2, 0, 2, 3};

    AppendPod(bytes, std::uint32_t{4});  // vertexCount
    AppendPod(bytes, std::uint32_t{6});  // indexCount
    AppendBytes(bytes, pos, sizeof(pos));
    AppendBytes(bytes, idx, sizeof(idx));
    AppendPod(bytes, std::uint8_t{1});   // hasUVs
    AppendBytes(bytes, uv, sizeof(uv));
    AppendPod(bytes, std::uint8_t{1});   // hasNormals
    AppendBytes(bytes, nrm, sizeof(nrm));

    const fs::path meshPath = root / "quad_v3.mesh";
    WriteAll(meshPath, bytes);

    MeshLoader loader;
    auto loadRes = loader.Load(meshPath.generic_string());
    assert(loadRes.IsOk() && "v3 文件必须在 v4 loader 下成功 Load");
    const MeshAsset& loaded = *loadRes.Value();
    assert(loaded.HasNormals());
    assert(loaded.HasTangents() && "v3（无 tangent 段）应触发 fallback 补算");
    std::printf("[ok] v3 file migrates under v4 loader\n");
}

void TestV1FileLoadsNoTangent(const fs::path& root)
{
    // 手写 v1：仅 magic + ver1 + counts + pos + idx（无 UV → 切线无定义）
    std::vector<std::uint8_t> bytes;
    AppendPod(bytes, MeshLoader::kMagic);
    AppendPod(bytes, MeshLoader::kVersionV1);
    const VertexPosition3 pos[3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    const std::uint32_t idx[3] = {0, 1, 2};
    AppendPod(bytes, std::uint32_t{3});
    AppendPod(bytes, std::uint32_t{3});
    AppendBytes(bytes, pos, sizeof(pos));
    AppendBytes(bytes, idx, sizeof(idx));

    const fs::path meshPath = root / "tri_v1.mesh";
    WriteAll(meshPath, bytes);

    MeshLoader loader;
    auto loadRes = loader.Load(meshPath.generic_string());
    assert(loadRes.IsOk() && "v1 文件必须在 v4 loader 下成功 Load");
    const MeshAsset& loaded = *loadRes.Value();
    assert(loaded.HasNormals() && "v1 缺 normal → loader 补算 smooth normal");
    assert(!loaded.HasUVs());
    assert(!loaded.HasTangents() && "无 UV → 切线无定义，HasTangents 应为 false");
    std::printf("[ok] v1 file loads, no UV -> no tangent\n");
}

// ---- 4. 无 UV mesh：ComputeTangents 清空 ---------------------------------
void TestNoUvMeshHasNoTangent()
{
    std::vector<VertexPosition3> pos = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    std::vector<std::uint32_t> idx = {0, 1, 2};
    MeshAsset mesh(std::move(pos), std::move(idx));
    mesh.ComputeSmoothNormalsFromTriangles();  // 有 normal 但无 UV
    mesh.ComputeTangentsFromTriangles();
    assert(!mesh.HasTangents() && "无 UV → ComputeTangents 必须清空");
    std::printf("[ok] no-UV mesh has no tangent\n");
}

}  // namespace

int main()
{
    const fs::path root = TempDir();

    TestComputeAndRoundTrip(root);
    TestFallbackOnSavedWithoutTangents(root);
    TestV3FileLoadsUnderV4Loader(root);
    TestV1FileLoadsNoTangent(root);
    TestNoUvMeshHasNoTangent();

    std::printf("all mesh tangent loader cases passed\n");
    return 0;
}
