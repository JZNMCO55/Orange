// Core::Serialization + SchemaVersion 的最小单元测试。
//
// 覆盖：
//   * SchemaVersion::CanRead 的 4 条主路径（happy / namespace 不匹配 /
//     major 不匹配 / minor 比 reader 还新）
//   * JsonWriter → JsonReader 双向往返一致
//   * BinaryWriter → BinaryReader 双向往返一致

#include <orange/engine/core/Result.h>
#include <orange/engine/core/SchemaVersion.h>
#include <orange/engine/core/Serialization.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using Orange::Engine::BinaryReader;
using Orange::Engine::BinaryWriter;
using Orange::Engine::JsonReader;
using Orange::Engine::JsonWriter;
using Orange::Engine::ResultCode;
using Orange::Engine::SchemaVersion;

namespace
{

// ---------------------------------------------------------------------------
// SchemaVersion
// ---------------------------------------------------------------------------

void TestSchemaCanReadHappy()
{
    SchemaVersion reader{"scene/Transform", 1, 3};
    SchemaVersion file{"scene/Transform", 1, 2};  // minor 比 reader 旧——可读
    assert(reader.CanRead(file));
    SchemaVersion sameMinor{"scene/Transform", 1, 3};
    assert(reader.CanRead(sameMinor));
    std::fprintf(stdout, "  [PASS] schema can-read happy\n");
}

void TestSchemaCanReadNamespaceMismatch()
{
    SchemaVersion reader{"scene/Transform", 1, 0};
    SchemaVersion file{"scene/Hierarchy", 1, 0};
    assert(!reader.CanRead(file));
    std::fprintf(stdout, "  [PASS] schema namespace mismatch\n");
}

void TestSchemaCanReadMajorMismatch()
{
    SchemaVersion reader{"scene/Transform", 1, 5};
    SchemaVersion file{"scene/Transform", 2, 0};
    assert(!reader.CanRead(file));
    SchemaVersion older{"scene/Transform", 0, 9};
    assert(!reader.CanRead(older));
    std::fprintf(stdout, "  [PASS] schema major mismatch\n");
}

void TestSchemaCanReadMinorTooNew()
{
    SchemaVersion reader{"scene/Transform", 1, 2};
    SchemaVersion file{"scene/Transform", 1, 3};  // minor 比 reader 新——不可读
    assert(!reader.CanRead(file));
    std::fprintf(stdout, "  [PASS] schema minor too new\n");
}

// ---------------------------------------------------------------------------
// JSON 双向往返
// ---------------------------------------------------------------------------

void TestJsonRoundTrip()
{
    SchemaVersion ver{"test/Aggregate", 2, 7};

    JsonWriter writer;
    writer.WriteSchemaVersion("schema", ver);
    writer.WriteBool("flag", true);
    writer.WriteInt("count", -42);
    writer.WriteFloat("ratio", 0.5);
    writer.WriteString("name", "orange");
    const float vec[3] = {1.0f, 2.0f, 3.0f};
    writer.WriteFloatArray("position", vec, 3);

    const std::string text = writer.Dump();

    auto readerResult = JsonReader::FromString(text);
    assert(readerResult.IsOk());
    const JsonReader& reader = readerResult.Value();

    auto verResult = reader.ReadSchemaVersion("schema");
    assert(verResult.IsOk());
    const SchemaVersion& verRead = verResult.Value();
    assert(verRead == ver);
    assert(verRead.NamespaceMatches(ver));

    bool flag = false;
    assert(reader.ReadBool("flag", flag) && flag == true);

    std::int64_t count = 0;
    assert(reader.ReadInt("count", count) && count == -42);

    double ratio = 0.0;
    assert(reader.ReadFloat("ratio", ratio));
    assert(std::abs(ratio - 0.5) < 1e-9);

    std::string name;
    assert(reader.ReadString("name", name) && name == "orange");

    float vecOut[3] = {0.0f, 0.0f, 0.0f};
    assert(reader.ReadFloatArray("position", vecOut, 3));
    assert(vecOut[0] == 1.0f && vecOut[1] == 2.0f && vecOut[2] == 3.0f);

    // 错误路径：key 缺失。
    bool dummy = false;
    assert(!reader.ReadBool("missing", dummy));

    std::fprintf(stdout, "  [PASS] JSON round-trip\n");
}

void TestJsonListKeys()
{
    // 构造嵌套对象 components/{Transform, Renderable, UnknownThing}（模拟 Scene
    // 序列化结构），验证 ListKeys 枚举对象直接 key——Scene::Load 用它对未注册
    // component 发 warning（数据丢失防护）。
    JsonWriter writer;
    writer.WriteInt("components/Transform/x", 1);
    writer.WriteInt("components/Renderable/mesh", 2);
    writer.WriteBool("components/UnknownThing/flag", true);
    writer.WriteString("topLevel", "v");

    auto readerResult = JsonReader::FromString(writer.Dump());
    assert(readerResult.IsOk());
    const JsonReader& reader = readerResult.Value();

    const std::vector<std::string> keys = reader.ListKeys("components");
    assert(keys.size() == 3);  // 顺序不假设，按集合判定
    const auto has = [&](const char* k) {
        return std::find(keys.begin(), keys.end(), std::string{k}) != keys.end();
    };
    assert(has("Transform") && has("Renderable") && has("UnknownThing"));

    // 不存在的 path → 空。
    assert(reader.ListKeys("nope").empty());
    // 指向非对象叶子（int）→ 空。
    assert(reader.ListKeys("components/Transform/x").empty());
    // 指向数组（WriteFloatArray）→ 空（ListKeys 仅对象）。
    JsonWriter arrW;
    const float v[2] = {1.0f, 2.0f};
    arrW.WriteFloatArray("arr", v, 2);
    auto arrReader = JsonReader::FromString(arrW.Dump());
    assert(arrReader.IsOk());
    assert(arrReader.Value().ListKeys("arr").empty());

    std::fprintf(stdout, "  [PASS] JSON ListKeys\n");
}

void TestJsonParseError()
{
    auto result = JsonReader::FromString("{ this is not json ");
    assert(result.IsErr());
    assert(result.Error().code == ResultCode::InvalidArgument);
    std::fprintf(stdout, "  [PASS] JSON parse error path\n");
}

// ---------------------------------------------------------------------------
// Binary 双向往返
// ---------------------------------------------------------------------------

struct BinaryPayload
{
    std::int32_t  i;
    std::uint64_t u;
    float         f;
    char          tag[4];
};

static_assert(std::is_trivially_copyable_v<BinaryPayload>,
              "BinaryPayload 必须 trivially copyable，BinaryWriter/Reader 才接受。");

void TestBinaryRoundTrip()
{
    BinaryPayload original{};
    original.i = -7;
    original.u = 0x1122334455667788ULL;
    original.f = 3.5f;
    original.tag[0] = 'O';
    original.tag[1] = 'R';
    original.tag[2] = 'N';
    original.tag[3] = 'G';

    BinaryWriter writer;
    writer.Write<std::int32_t>(123);
    writer.Write<float>(2.5f);
    writer.Write<BinaryPayload>(original);

    const auto& bytes = writer.Bytes();
    assert(writer.Size() == sizeof(std::int32_t) + sizeof(float) + sizeof(BinaryPayload));
    assert(bytes.size() == writer.Size());

    BinaryReader reader{bytes.data(), bytes.size()};

    std::int32_t intVal = 0;
    assert(reader.Read<std::int32_t>(intVal) && intVal == 123);

    float floatVal = 0.0f;
    assert(reader.Read<float>(floatVal) && floatVal == 2.5f);

    BinaryPayload roundtrip{};
    assert(reader.Read<BinaryPayload>(roundtrip));
    assert(roundtrip.i == original.i);
    assert(roundtrip.u == original.u);
    assert(roundtrip.f == original.f);
    for (std::size_t i = 0; i < 4; ++i)
    {
        assert(roundtrip.tag[i] == original.tag[i]);
    }

    assert(reader.Eof());
    assert(reader.Remaining() == 0);

    // 越界读：失败但不写出。
    std::int32_t leftover = 0;
    assert(!reader.Read<std::int32_t>(leftover));

    std::fprintf(stdout, "  [PASS] Binary round-trip\n");
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[SerializationTest] running\n");
    TestSchemaCanReadHappy();
    TestSchemaCanReadNamespaceMismatch();
    TestSchemaCanReadMajorMismatch();
    TestSchemaCanReadMinorTooNew();
    TestJsonRoundTrip();
    TestJsonListKeys();
    TestJsonParseError();
    TestBinaryRoundTrip();
    std::fprintf(stdout, "[SerializationTest] all tests passed.\n");
    return 0;
}
