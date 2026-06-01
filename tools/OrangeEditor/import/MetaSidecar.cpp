#include "MetaSidecar.h"

#include <orange/engine/core/Log.h>
#include <orange/engine/core/SchemaVersion.h>
#include <orange/engine/core/Serialization.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace Orange::Editor::Import
{

namespace
{
// schema namespace 字面量。改字符串等于切 schema namespace，下游 reader
// 严格按名字过滤，故不轻易改动；major / minor 走 SchemaVersion 三元组。
constexpr const char* kSchemaNamespace = "editor/import/texture";
constexpr std::uint16_t kSchemaMajor = 1;
constexpr std::uint16_t kSchemaMinor = 0;

// FNV-1a 64-bit 常量。算法 RFC：http://www.isthe.com/chongo/tech/comp/fnv/
constexpr std::uint64_t kFnvOffset64 = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime64  = 1099511628211ULL;
}  // namespace

std::optional<std::uint64_t> ComputeFileHashFnv1a(std::string_view path)
{
    // ifstream 流式读 64KB chunks，避免大文件 mmap / 全文件加载内存峰值；
    // 资产典型 1-50 MB，chunk size 选 64KB 是 syscall 与栈占用的折中。
    // 走 ifstream 而非 fopen 是为了避开 MSVC /WX 下 C4996 fopen deprecation
    // 噪音，且不污染本 TU 周围加 pragma push/disable。
    std::string cpath(path);
    std::ifstream stream(cpath, std::ios::binary);
    if (!stream.is_open())
    {
        ORANGE_LOG_ERROR("MetaSidecar: open failed for '{}'", cpath);
        return std::nullopt;
    }

    std::uint64_t hash = kFnvOffset64;
    std::array<char, 64 * 1024> buffer{};
    while (stream)
    {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize n = stream.gcount();
        if (n <= 0)
        {
            break;
        }
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(buffer.data());
        for (std::streamsize i = 0; i < n; ++i)
        {
            hash ^= static_cast<std::uint64_t>(bytes[i]);
            hash *= kFnvPrime64;
        }
    }

    if (stream.bad())
    {
        ORANGE_LOG_ERROR("MetaSidecar: read error during hash of '{}'", cpath);
        return std::nullopt;
    }
    return hash;
}

std::string HashToHexString(std::uint64_t hash)
{
    // "0x" + 16 位小写 hex；snprintf %016llx 跨平台稳。
    std::array<char, 32> buf{};
    std::snprintf(buf.data(), buf.size(), "0x%016llx",
                  static_cast<unsigned long long>(hash));
    return std::string{buf.data()};
}

std::optional<std::uint64_t> HexStringToHash(std::string_view hex)
{
    // 接受 "0x" 前缀 + 16 hex digits；不带前缀也接受（兼容手工编辑）。
    std::string_view body = hex;
    if (body.size() >= 2 && body[0] == '0' && (body[1] == 'x' || body[1] == 'X'))
    {
        body = body.substr(2);
    }
    if (body.size() != 16)
    {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (char ch : body)
    {
        std::uint64_t digit = 0;
        if (ch >= '0' && ch <= '9') { digit = static_cast<std::uint64_t>(ch - '0'); }
        else if (ch >= 'a' && ch <= 'f') { digit = static_cast<std::uint64_t>(ch - 'a' + 10); }
        else if (ch >= 'A' && ch <= 'F') { digit = static_cast<std::uint64_t>(ch - 'A' + 10); }
        else { return std::nullopt; }
        value = (value << 4) | digit;
    }
    return value;
}

std::optional<TextureMetaV1> ReadTextureMeta(std::string_view path)
{
    using ::Orange::Engine::JsonReader;
    using ::Orange::Engine::SchemaVersion;

    auto readerResult = JsonReader::FromFile(path);
    if (readerResult.IsErr())
    {
        // 缺 .meta 是正常路径（首次 import 时还没生成）；不在这层 ERROR，
        // 由调用方判断"应当存在但缺失"语义。
        return std::nullopt;
    }
    const JsonReader& reader = readerResult.Value();

    auto schemaResult = reader.ReadSchemaVersion("schemaVersion");
    if (schemaResult.IsErr())
    {
        ORANGE_LOG_ERROR("MetaSidecar: '{}' missing or malformed schemaVersion", path);
        return std::nullopt;
    }
    const SchemaVersion& sv = schemaResult.Value();
    if (sv.Namespace() != kSchemaNamespace)
    {
        ORANGE_LOG_ERROR("MetaSidecar: '{}' schemaVersion namespace='{}' "
                         "(expected '{}')",
                         path, sv.Namespace(), kSchemaNamespace);
        return std::nullopt;
    }
    if (sv.Major() != kSchemaMajor)
    {
        // 主版本不兼容；future migrator 路径处理。当前 v1.x 高 minor
        // 视为向前兼容（自身缺字段走默认值）。
        ORANGE_LOG_ERROR("MetaSidecar: '{}' schemaVersion major={} "
                         "(expected {}); migrator not yet wired",
                         path, sv.Major(), kSchemaMajor);
        return std::nullopt;
    }

    TextureMetaV1 meta{};
    reader.ReadString("sourcePath", meta.sourcePath);

    std::string hashHex;
    if (reader.ReadString("sourceHash", hashHex))
    {
        if (auto h = HexStringToHash(hashHex))
        {
            meta.sourceHash = *h;
        }
        else
        {
            ORANGE_LOG_ERROR("MetaSidecar: '{}' sourceHash='{}' malformed",
                             path, hashHex);
            return std::nullopt;
        }
    }

    std::int64_t handleId = 0;
    if (reader.ReadInt("handleId", handleId))
    {
        meta.handleId = static_cast<std::uint64_t>(handleId);
    }

    // importParams 当前 v1 空 object；不读字段。v1.2+ 加字段时在此扩展。

    // subMeshMaterials（v1.1 可选段）：多 material mesh 的 .meta 才有；旧
    // v1.0 / 单 material / texture 的 .meta 无此键 → ArraySize 返回 0，列表
    // 留空（向后兼容）。string 数组无专用 helper：按 "subMeshMaterials/i"
    // 索引 path 逐条 ReadString（与 ComponentSerializers slots 同款手法）。
    const std::size_t subMatCount = reader.ArraySize("subMeshMaterials");
    meta.subMeshMaterials.reserve(subMatCount);
    for (std::size_t i = 0; i < subMatCount; ++i)
    {
        const std::string itemPath = "subMeshMaterials/" + std::to_string(i);
        std::string item;
        // 空字符串槽（无 material 的 slot 占位）也保留，维持 slot 对齐。
        reader.ReadString(itemPath, item);
        meta.subMeshMaterials.push_back(std::move(item));
    }

    return meta;
}

bool WriteTextureMeta(std::string_view path, const TextureMetaV1& meta)
{
    using ::Orange::Engine::JsonWriter;
    using ::Orange::Engine::SchemaVersion;

    JsonWriter writer;
    // 带 subMeshMaterials 时写 minor=1（v1.1）；否则维持 kSchemaMinor（0）让单
    // material / texture / obj 的 .meta 字节与历史完全一致（向后兼容）。reader
    // 只校验 namespace + major，对 minor 前向兼容，旧 reader 读 v1.1 也正常。
    const std::uint16_t writeMinor = meta.subMeshMaterials.empty()
                                         ? kSchemaMinor
                                         : static_cast<std::uint16_t>(1);
    SchemaVersion sv{kSchemaNamespace, kSchemaMajor, writeMinor};
    writer.WriteSchemaVersion("schemaVersion", sv);

    writer.WriteString("sourcePath", meta.sourcePath);
    writer.WriteString("sourceHash", HashToHexString(meta.sourceHash));
    writer.WriteInt("handleId", static_cast<std::int64_t>(meta.handleId));

    // subMeshMaterials（v1.1 可选段）：仅多 material mesh 导入填它。空则完全
    // 不写本键，让单 material / texture / obj 的 .meta 字节与历史一致（向后
    // 兼容，零行为变化）；reader 端对缺键容忍（ArraySize 返回 <=0）。
    if (!meta.subMeshMaterials.empty())
    {
        // string 数组无专用 helper：BeginArray 声明长度 + "subMeshMaterials/i"
        // 索引 path 逐条 WriteString（与 ComponentSerializers slots 同款手法）。
        writer.BeginArray("subMeshMaterials", meta.subMeshMaterials.size());
        for (std::size_t i = 0; i < meta.subMeshMaterials.size(); ++i)
        {
            const std::string itemPath = "subMeshMaterials/" + std::to_string(i);
            writer.WriteString(itemPath, meta.subMeshMaterials[i]);
        }
    }

    // importParams 写空 object 占位，未来扩字段时此处变成多个 WriteXxx 调用。
    // JsonWriter 不直接暴露 "建空 object" 入口，但首次访问 importParams/X
    // 时会自动 promote；当前没字段，先 BeginArray("importParams", 0) 写
    // 空数组也不对——schema 明确是 object。最简办法：写一个 sentinel
    // 字符串字段 + reader 忽略，但污染 schema。改用 dummy path 触发 object
    // 创建：写一个内部约定字段 "_v" 然后 reader 不读即可——但仍污染。
    // 接受当前 JsonWriter 限制：v1 .meta 没有 importParams 段也是合法的
    // （reader 容忍缺失）。等 v1.2 真要加字段时一并扩 JsonWriter API。

    auto saveResult = writer.SaveToFile(path);
    if (saveResult.IsErr())
    {
        ORANGE_LOG_ERROR("MetaSidecar: SaveToFile failed for '{}'", path);
        return false;
    }
    return true;
}

std::string MetaPathFor(std::string_view assetPath)
{
    std::string out;
    out.reserve(assetPath.size() + 5);
    out.append(assetPath);
    out.append(".meta");
    return out;
}

bool MetaSourceHashMatches(std::string_view destAssetPath, std::uint64_t newHash)
{
    const std::string metaPath = MetaPathFor(destAssetPath);
    auto meta = ReadTextureMeta(metaPath);
    if (!meta.has_value())
    {
        return false;  // 缺 .meta / 解析失败 —— 强制走完整 import 路径
    }
    return meta->sourceHash == newHash;
}

}  // namespace Orange::Editor::Import
