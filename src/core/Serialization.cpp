// Core::Serialization 实现
//
// nlohmann::json 被严格限制在本 TU 内部使用：所有触及 JSON 的引擎路径
// 都通过这里的 reader / writer 类。在本 TU 外直接 include
// <nlohmann/json.hpp> 视为越界，code review 阶段拒绝（详见 CLAUDE.md
// 中的 "Serialization and reflection" 一节）。

#include "orange/engine/core/Serialization.h"
#include "orange/engine/core/SchemaVersion.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace Orange::Engine
{
namespace
{

using Json = nlohmann::json;

// 把 "a/b/c" 拆成 ["a", "b", "c"]。空字符串返回空 vector。当前不支持
// 在 key 中出现 '/'。
std::vector<std::string_view> SplitPath(std::string_view path) noexcept
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= path.size(); ++i)
    {
        if (i == path.size() || path[i] == '/')
        {
            if (i > start)
            {
                parts.emplace_back(path.substr(start, i - start));
            }
            start = i + 1;
        }
    }
    return parts;
}

// 判 part 是不是纯数字（用作 JSON 数组下标）。空 string_view 返回 false。
bool IsAllDigits(std::string_view part) noexcept
{
    if (part.empty())
    {
        return false;
    }
    for (char c : part)
    {
        if (c < '0' || c > '9')
        {
            return false;
        }
    }
    return true;
}

const Json* FindByPath(const Json& root, std::string_view path) noexcept
{
    auto parts = SplitPath(path);
    if (parts.empty())
    {
        return &root;
    }
    const Json* node = &root;
    for (auto part : parts)
    {
        if (node->is_object())
        {
            std::string key{part};
            auto it = node->find(key);
            if (it == node->end())
            {
                return nullptr;
            }
            node = &(*it);
        }
        else if (node->is_array() && IsAllDigits(part))
        {
            // path 段是纯数字 → 当作数组下标。越界返 nullptr。
            std::size_t idx = 0;
            for (char c : part)
            {
                idx = idx * 10 + static_cast<std::size_t>(c - '0');
            }
            if (idx >= node->size())
            {
                return nullptr;
            }
            node = &(*node)[idx];
        }
        else
        {
            // node 不是 object 也不是数组（或数组但 part 非数字） → 路径无效。
            return nullptr;
        }
    }
    return node;
}

// 沿路径走 `root`，按需创建中间对象节点；返回叶节点的引用，调用方可
// 直接赋值进去。
//
// 数组语义：当当前节点已经是数组、且下一段是纯数字时，把它当作下标
// 处理（越界则用空对象补齐到该下标）。这条分支让 `BeginArray` 之后
// 的 "arr/0/name" 这类路径能往同一个数组里继续写。如果当前节点既不
// 是 object 也不是匹配的数组（例如把一个标量当成中间节点继续下钻），
// 仍按原有语义重置为 object——这与 Phase 1 / Task 05 落地时"路径
// 自动建对象树"的承诺保持一致。
Json& EnsureByPath(Json& root, std::string_view path)
{
    auto parts = SplitPath(path);
    Json* node = &root;
    for (auto part : parts)
    {
        if (node->is_array() && IsAllDigits(part))
        {
            std::size_t idx = 0;
            for (char c : part)
            {
                idx = idx * 10 + static_cast<std::size_t>(c - '0');
            }
            if (idx >= node->size())
            {
                node->insert(node->end(),
                             idx + 1 - node->size(),
                             Json::object());
            }
            node = &((*node)[idx]);
            continue;
        }

        if (!node->is_object())
        {
            *node = Json::object();
        }
        std::string key{part};
        node = &((*node)[key]);
    }
    return *node;
}

}  // namespace

// ---------------------------------------------------------------------------
// JsonReader
// ---------------------------------------------------------------------------

struct JsonReader::Impl
{
    Json root;
};

JsonReader::JsonReader() : mpImpl(std::make_unique<Impl>()) {}

JsonReader::JsonReader(std::unique_ptr<Impl> impl) noexcept : mpImpl(std::move(impl)) {}

JsonReader::JsonReader(JsonReader&&) noexcept = default;
JsonReader& JsonReader::operator=(JsonReader&&) noexcept = default;
JsonReader::~JsonReader() = default;

Result<JsonReader, ParseError> JsonReader::FromString(std::string_view text)
{
    auto impl = std::make_unique<Impl>();
    try
    {
        impl->root = Json::parse(text);
    }
    catch (const Json::parse_error& e)
    {
        ParseError err;
        err.code    = ResultCode::InvalidArgument;
        err.path    = "";
        err.message = std::string{"JSON parse error at byte "} + std::to_string(e.byte) + ": " + e.what();
        return err;
    }
    return JsonReader{std::move(impl)};
}

Result<JsonReader, ParseError> JsonReader::FromFile(std::string_view path)
{
    std::ifstream stream(std::string{path}, std::ios::binary);
    if (!stream.is_open())
    {
        ParseError err;
        err.code    = ResultCode::IoError;
        err.message = std::string{"Cannot open JSON file: "} + std::string{path};
        return err;
    }
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return FromString(buffer.str());
}

bool JsonReader::Has(std::string_view path) const
{
    return FindByPath(mpImpl->root, path) != nullptr;
}

std::size_t JsonReader::ArraySize(std::string_view path) const
{
    const Json* node = FindByPath(mpImpl->root, path);
    if (node == nullptr || !node->is_array())
    {
        return 0;
    }
    return node->size();
}

bool JsonReader::ReadBool(std::string_view path, bool& out) const
{
    const Json* node = FindByPath(mpImpl->root, path);
    if (!node || !node->is_boolean())
    {
        return false;
    }
    out = node->get<bool>();
    return true;
}

bool JsonReader::ReadInt(std::string_view path, std::int64_t& out) const
{
    const Json* node = FindByPath(mpImpl->root, path);
    if (!node || !node->is_number_integer())
    {
        return false;
    }
    out = node->get<std::int64_t>();
    return true;
}

bool JsonReader::ReadFloat(std::string_view path, double& out) const
{
    const Json* node = FindByPath(mpImpl->root, path);
    if (!node || !node->is_number())
    {
        return false;
    }
    out = node->get<double>();
    return true;
}

bool JsonReader::ReadString(std::string_view path, std::string& out) const
{
    const Json* node = FindByPath(mpImpl->root, path);
    if (!node || !node->is_string())
    {
        return false;
    }
    out = node->get<std::string>();
    return true;
}

bool JsonReader::GetBool(std::string_view path, bool defaultValue) const
{
    bool value = defaultValue;
    ReadBool(path, value);
    return value;
}

std::int64_t JsonReader::GetInt(std::string_view path, std::int64_t defaultValue) const
{
    std::int64_t value = defaultValue;
    ReadInt(path, value);
    return value;
}

double JsonReader::GetFloat(std::string_view path, double defaultValue) const
{
    double value = defaultValue;
    ReadFloat(path, value);
    return value;
}

std::string JsonReader::GetString(std::string_view path, std::string defaultValue) const
{
    std::string value = std::move(defaultValue);
    ReadString(path, value);
    return value;
}

bool JsonReader::ReadFloatArray(std::string_view path, float* out, std::size_t count) const
{
    const Json* node = FindByPath(mpImpl->root, path);
    if (!node || !node->is_array() || node->size() != count)
    {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        const Json& element = (*node)[i];
        if (!element.is_number())
        {
            return false;
        }
        out[i] = element.get<float>();
    }
    return true;
}

Result<SchemaVersion, ResultCode> JsonReader::ReadSchemaVersion(std::string_view path) const
{
    const Json* node = FindByPath(mpImpl->root, path);
    if (!node || !node->is_object())
    {
        return ResultCode::NotFound;
    }
    auto nsIt    = node->find("namespace");
    auto majorIt = node->find("major");
    auto minorIt = node->find("minor");
    if (nsIt == node->end() || !nsIt->is_string()
        || majorIt == node->end() || !majorIt->is_number_integer()
        || minorIt == node->end() || !minorIt->is_number_integer())
    {
        return ResultCode::InvalidArgument;
    }
    return SchemaVersion{
        nsIt->get<std::string>(),
        static_cast<std::uint16_t>(majorIt->get<std::int64_t>()),
        static_cast<std::uint16_t>(minorIt->get<std::int64_t>())};
}

// ---------------------------------------------------------------------------
// JsonWriter
// ---------------------------------------------------------------------------

struct JsonWriter::Impl
{
    Json root = Json::object();
};

JsonWriter::JsonWriter() : mpImpl(std::make_unique<Impl>()) {}
JsonWriter::JsonWriter(JsonWriter&&) noexcept = default;
JsonWriter& JsonWriter::operator=(JsonWriter&&) noexcept = default;
JsonWriter::~JsonWriter() = default;

void JsonWriter::WriteBool(std::string_view path, bool value)
{
    EnsureByPath(mpImpl->root, path) = value;
}

void JsonWriter::WriteInt(std::string_view path, std::int64_t value)
{
    EnsureByPath(mpImpl->root, path) = value;
}

void JsonWriter::WriteFloat(std::string_view path, double value)
{
    EnsureByPath(mpImpl->root, path) = value;
}

void JsonWriter::WriteString(std::string_view path, std::string_view value)
{
    EnsureByPath(mpImpl->root, path) = std::string{value};
}

void JsonWriter::WriteFloatArray(std::string_view path, const float* data, std::size_t count)
{
    Json& node = EnsureByPath(mpImpl->root, path);
    node = Json::array();
    for (std::size_t i = 0; i < count; ++i)
    {
        node.push_back(data[i]);
    }
}

void JsonWriter::BeginArray(std::string_view path, std::size_t count)
{
    Json& node = EnsureByPath(mpImpl->root, path);
    node = Json::array();
    for (std::size_t i = 0; i < count; ++i)
    {
        node.push_back(Json::object());
    }
}

void JsonWriter::WriteSchemaVersion(std::string_view path, const SchemaVersion& version)
{
    Json& node = EnsureByPath(mpImpl->root, path);
    node = Json::object();
    node["namespace"] = version.Namespace();
    node["major"]     = version.Major();
    node["minor"]     = version.Minor();
}

std::string JsonWriter::Dump(int indent) const
{
    return mpImpl->root.dump(indent);
}

Result<void, ResultCode> JsonWriter::SaveToFile(std::string_view path, int indent) const
{
    std::ofstream stream(std::string{path}, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        return ResultCode::IoError;
    }
    stream << mpImpl->root.dump(indent);
    return Result<void, ResultCode>{};
}

// ---------------------------------------------------------------------------
// BinaryWriter
// ---------------------------------------------------------------------------

void BinaryWriter::WriteBytes(const void* data, std::size_t count)
{
    const auto* src = static_cast<const std::uint8_t*>(data);
    mBytes.insert(mBytes.end(), src, src + count);
}

Result<void, ResultCode> BinaryWriter::SaveToFile(std::string_view path) const
{
    std::ofstream stream(std::string{path}, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        return ResultCode::IoError;
    }
    if (!mBytes.empty())
    {
        stream.write(reinterpret_cast<const char*>(mBytes.data()),
                     static_cast<std::streamsize>(mBytes.size()));
    }
    return Result<void, ResultCode>{};
}

// ---------------------------------------------------------------------------
// BinaryReader
// ---------------------------------------------------------------------------

bool BinaryReader::ReadBytes(void* out, std::size_t count) noexcept
{
    if (mCursor + count > mSize)
    {
        return false;
    }
    std::memcpy(out, mpData + mCursor, count);
    mCursor += count;
    return true;
}

Result<std::vector<std::uint8_t>, ResultCode> BinaryReader::LoadFile(std::string_view path)
{
    std::ifstream stream(std::string{path}, std::ios::binary | std::ios::ate);
    if (!stream.is_open())
    {
        return ResultCode::IoError;
    }
    const auto size = stream.tellg();
    if (size < 0)
    {
        return ResultCode::IoError;
    }
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (size > 0)
    {
        stream.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!stream)
        {
            return ResultCode::IoError;
        }
    }
    return bytes;
}

}  // namespace Orange::Engine
