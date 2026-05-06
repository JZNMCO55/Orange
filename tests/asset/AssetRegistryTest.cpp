// AssetRegistry + 三个内置 loader 的端到端单元测试。
//
// 走法：
//   1. 在临时目录里写出三个 fixture 文件（mesh / texture / shader），
//      格式与各 loader 严格对齐；
//   2. 注册三个 loader、依次 Load，验证 handle 有效、Get 返回内容正
//      确；
//   3. 同 path 重复 Load 验证 dedup（返回同一 handle，loader 不重复
//      调用——通过 InstrumentedLoader 计数器观察）；
//   4. Unload 验证 handle 失效、Size 减 1、之后再 Load 重新触发
//      loader；
//   5. 错误路径：未注册 loader 返回 Unsupported；wrong magic 返回
//      SchemaMismatch；不存在的文件返回 IoError。

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/MeshLoader.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/asset/TextureAsset.h>
#include <orange/engine/asset/TextureLoader.h>
#include <orange/engine/core/Result.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using Orange::Engine::Result;
using Orange::Engine::ResultCode;
using namespace Orange::Engine::Asset;

namespace
{

namespace fs = std::filesystem;

fs::path TempDir()
{
    auto root = fs::temp_directory_path() / "orange_engine_asset_test";
    fs::create_directories(root);
    return root;
}

// ---- 写 fixture ---------------------------------------------------------

void WriteAll(const fs::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!bytes.empty())
    {
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
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

fs::path MakeMeshFixture(const fs::path& root)
{
    std::vector<std::uint8_t> bytes;
    AppendPod(bytes, MeshLoader::kMagic);
    AppendPod(bytes, MeshLoader::kSupportedVersion);
    const std::uint32_t vertexCount = 3;
    const std::uint32_t indexCount  = 3;
    AppendPod(bytes, vertexCount);
    AppendPod(bytes, indexCount);

    const VertexPosition3 positions[3] = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };
    AppendBytes(bytes, positions, sizeof(positions));

    const std::uint32_t indices[3] = {0, 1, 2};
    AppendBytes(bytes, indices, sizeof(indices));

    auto path = root / "triangle.orme";
    WriteAll(path, bytes);
    return path;
}

fs::path MakeTextureFixture(const fs::path& root)
{
    std::vector<std::uint8_t> bytes;
    AppendPod(bytes, TextureLoader::kMagic);
    AppendPod(bytes, TextureLoader::kSupportedVersion);
    const std::uint32_t width  = 2;
    const std::uint32_t height = 2;
    const std::uint32_t format = static_cast<std::uint32_t>(TextureFormat::R8G8B8A8_UNorm);
    AppendPod(bytes, width);
    AppendPod(bytes, height);
    AppendPod(bytes, format);

    // 4 个像素 RGBA：红、绿、蓝、白。
    const std::uint8_t pixels[16] = {
        0xFF, 0x00, 0x00, 0xFF,
        0x00, 0xFF, 0x00, 0xFF,
        0x00, 0x00, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF,
    };
    AppendBytes(bytes, pixels, sizeof(pixels));

    auto path = root / "checker.ortx";
    WriteAll(path, bytes);
    return path;
}

fs::path MakeShaderFixture(const fs::path& root, std::string_view filename)
{
    // 合法 SPIR-V 至少要有 magic + version + generator + bound + schema
    // 5 个 word；这里造一个 5-word 的最小合法 stub，保证 Load 通过。
    std::vector<std::uint32_t> words = {
        ShaderLoader::kSpirVMagic,
        0x00010000U,  // version 1.0
        0x00080001U,  // generator id (任意)
        0x00000001U,  // bound = 1
        0x00000000U,  // schema = 0
    };
    std::vector<std::uint8_t> bytes;
    AppendBytes(bytes, words.data(), words.size() * sizeof(std::uint32_t));
    auto path = root / std::string{filename};
    WriteAll(path, bytes);
    return path;
}

// 计数包装：让 dedup 测试能直接观察 loader 实际被调多少次。
template <typename Inner, typename T>
class CountingLoader : public IAssetLoader<T>
{
public:
    int callCount{0};

    Result<std::unique_ptr<T>, ResultCode> Load(std::string_view path) override
    {
        ++callCount;
        return mInner.Load(path);
    }

private:
    Inner mInner;
};

// ---- 测试函数 -----------------------------------------------------------

void TestMeshLoadGetUnload()
{
    auto root = TempDir();
    auto meshPath = MakeMeshFixture(root);

    AssetRegistry reg;
    auto regRc = reg.RegisterLoader<MeshAsset>(std::make_unique<MeshLoader>());
    assert(regRc.IsOk());

    auto loadResult = reg.Load<MeshAsset>(meshPath.string());
    assert(loadResult.IsOk());
    auto handle = loadResult.Value();
    assert(handle.IsValid());

    const MeshAsset* mesh = reg.Get(handle);
    assert(mesh != nullptr);
    assert(mesh->VertexCount() == 3);
    assert(mesh->IndexCount() == 3);
    assert(mesh->Positions()[1].x == 1.0f);
    assert(mesh->Indices()[2] == 2u);

    assert(reg.Size() == 1);
    assert(reg.Unload(handle));
    assert(reg.Size() == 0);
    assert(reg.Get(handle) == nullptr);

    std::fprintf(stdout, "  [PASS] mesh load / get / unload\n");
}

void TestTextureLoad()
{
    auto root = TempDir();
    auto texPath = MakeTextureFixture(root);

    AssetRegistry reg;
    auto regRc = reg.RegisterLoader<TextureAsset>(std::make_unique<TextureLoader>());
    assert(regRc.IsOk());

    auto loadResult = reg.Load<TextureAsset>(texPath.string());
    assert(loadResult.IsOk());
    auto handle = loadResult.Value();

    const TextureAsset* tex = reg.Get(handle);
    assert(tex != nullptr);
    assert(tex->Width() == 2 && tex->Height() == 2);
    assert(tex->Format() == TextureFormat::R8G8B8A8_UNorm);
    assert(tex->Pixels().size() == 16);
    assert(tex->Pixels()[0] == 0xFF && tex->Pixels()[1] == 0x00);  // 第 1 像素 R
    std::fprintf(stdout, "  [PASS] texture load\n");
}

void TestShaderLoadAndStage()
{
    auto root = TempDir();
    auto vertPath = MakeShaderFixture(root, "smoke.vert.spv");
    auto fragPath = MakeShaderFixture(root, "smoke.frag.spv");
    auto raw      = MakeShaderFixture(root, "smoke.spv");  // unknown stage

    // 后缀映射的纯函数路径。
    assert(ShaderLoader::StageFromPath(vertPath.string()) == ShaderStage::Vertex);
    assert(ShaderLoader::StageFromPath(fragPath.string()) == ShaderStage::Fragment);
    assert(ShaderLoader::StageFromPath(raw.string())      == ShaderStage::Unknown);

    AssetRegistry reg;
    assert(reg.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>()).IsOk());

    auto vertResult = reg.Load<ShaderAsset>(vertPath.string());
    assert(vertResult.IsOk());
    const ShaderAsset* vert = reg.Get(vertResult.Value());
    assert(vert != nullptr);
    assert(vert->Stage() == ShaderStage::Vertex);
    assert(vert->WordCount() == 5);
    assert(vert->SpirV()[0] == ShaderLoader::kSpirVMagic);

    std::fprintf(stdout, "  [PASS] shader load / stage detection\n");
}

void TestDedup()
{
    auto root = TempDir();
    auto meshPath = MakeMeshFixture(root);

    auto loader = std::make_unique<CountingLoader<MeshLoader, MeshAsset>>();
    auto* loaderObserved = loader.get();

    AssetRegistry reg;
    assert(reg.RegisterLoader<MeshAsset>(std::move(loader)).IsOk());

    auto first = reg.Load<MeshAsset>(meshPath.string());
    assert(first.IsOk());
    auto second = reg.Load<MeshAsset>(meshPath.string());
    assert(second.IsOk());

    assert(first.Value() == second.Value());
    assert(loaderObserved->callCount == 1);  // dedup 命中：loader 没被再调一次

    // Unload 后再 Load 同 path：loader 应被重新调用一次。
    assert(reg.Unload(first.Value()));
    auto third = reg.Load<MeshAsset>(meshPath.string());
    assert(third.IsOk());
    assert(loaderObserved->callCount == 2);

    std::fprintf(stdout, "  [PASS] dedup + re-load after unload\n");
}

void TestErrorPaths()
{
    auto root = TempDir();

    // 1. 没注册过 loader → Unsupported
    AssetRegistry reg;
    auto noLoader = reg.Load<MeshAsset>("anything.bin");
    assert(noLoader.IsErr());
    assert(noLoader.Error() == ResultCode::Unsupported);

    assert(reg.RegisterLoader<MeshAsset>(std::make_unique<MeshLoader>()).IsOk());

    // 2. 文件不存在 → IoError（来自 BinaryReader::LoadFile）
    auto missing = reg.Load<MeshAsset>((root / "no_such_file.orme").string());
    assert(missing.IsErr());
    assert(missing.Error() == ResultCode::IoError);

    // 3. magic 不对 → SchemaMismatch
    {
        std::vector<std::uint8_t> bytes;
        const std::uint32_t bogus = 0xDEADBEEFU;
        AppendPod(bytes, bogus);
        AppendPod(bytes, MeshLoader::kSupportedVersion);
        AppendPod(bytes, std::uint32_t{0});
        AppendPod(bytes, std::uint32_t{0});
        auto p = root / "bad_magic.orme";
        WriteAll(p, bytes);
        auto bad = reg.Load<MeshAsset>(p.string());
        assert(bad.IsErr());
        assert(bad.Error() == ResultCode::SchemaMismatch);
    }

    // 4. 注册 nullptr loader → InvalidArgument
    AssetRegistry reg2;
    auto badRegister = reg2.RegisterLoader<MeshAsset>(nullptr);
    assert(badRegister.IsErr());
    assert(badRegister.Error() == ResultCode::InvalidArgument);

    std::fprintf(stdout, "  [PASS] error paths\n");
}

void TestMultipleTypesShareRegistry()
{
    auto root = TempDir();
    auto meshPath = MakeMeshFixture(root);
    auto texPath  = MakeTextureFixture(root);

    AssetRegistry reg;
    assert(reg.RegisterLoader<MeshAsset>(std::make_unique<MeshLoader>()).IsOk());
    assert(reg.RegisterLoader<TextureAsset>(std::make_unique<TextureLoader>()).IsOk());

    auto m = reg.Load<MeshAsset>(meshPath.string());
    auto t = reg.Load<TextureAsset>(texPath.string());
    assert(m.IsOk() && t.IsOk());
    assert(reg.Size() == 2);

    // 两种类型 handle 各自独立——即使数值相等也不会拿到对方的资源。
    // (TypedHandle phantom tag 已在编译期挡掉互转，但运行时 GetErased
    //  内部按 type_index 路由，这里做一次跨类型 sanity check。)
    assert(reg.Get(m.Value()) != nullptr);
    assert(reg.Get(t.Value()) != nullptr);

    std::fprintf(stdout, "  [PASS] multi-type registry\n");
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[AssetRegistryTest] running\n");
    TestMeshLoadGetUnload();
    TestTextureLoad();
    TestShaderLoadAndStage();
    TestDedup();
    TestErrorPaths();
    TestMultipleTypesShareRegistry();
    std::fprintf(stdout, "[AssetRegistryTest] all tests passed.\n");
    return 0;
}
