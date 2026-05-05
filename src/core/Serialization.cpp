// ---------------------------------------------------------------------------
// Phase 1 / Task 05 — Core::Serialization implementation
//
// nlohmann::json is wrapped here exclusively. Every JSON-touching engine
// path routes through these classes; direct includes of <nlohmann/json.hpp>
// outside this TU are rejected at code review (see CLAUDE.md guardrails).
// ---------------------------------------------------------------------------

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

// Splits "a/b/c" into ["a", "b", "c"]. Empty string -> empty vector.
// '/' inside keys is not supported in Phase 1.
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
        if (!node->is_object())
        {
            return nullptr;
        }
        std::string key{part};
        auto it = node->find(key);
        if (it == node->end())
        {
            return nullptr;
        }
        node = &(*it);
    }
    return node;
}

// Walks `root`, creating nested objects as needed, and returns a reference
// to the leaf node so the caller can assign a value into it.
Json& EnsureByPath(Json& root, std::string_view path)
{
    auto parts = SplitPath(path);
    Json* node = &root;
    for (auto part : parts)
    {
        std::string key{part};
        if (!node->is_object())
        {
            *node = Json::object();
        }
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
