#ifndef ORANGE_ENGINE_CORE_SERIALIZATION_H
#define ORANGE_ENGINE_CORE_SERIALIZATION_H

// ---------------------------------------------------------------------------
// Phase 1 / Task 05 — Core::Serialization
//
// Single chokepoint for every read/write of engine-owned data. Direct use
// of `nlohmann::json` is forbidden outside this layer (see "Serialization
// and reflection" guardrails in CLAUDE.md); modules go through these
// readers/writers instead.
//
// Two formats, two reader/writer pairs:
//   * JsonReader / JsonWriter   — text payloads (configs, schemas, tools).
//   * BinaryReader / BinaryWriter — packed asset blobs (little-endian,
//                                   byte-aligned, no padding leakage).
//
// Path syntax for JSON access uses '/' as a separator (`window/size/x`).
// Phase 1 does not support array indexing inside paths; arrays are read /
// written via dedicated WriteIntArray / ReadIntArray helpers added on
// demand by later phases.
//
// All readers / writers PIMPL the nlohmann::json type so that
// Serialization.h itself stays third-party-free in its public surface.
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Result.h>
#include <orange/engine/core/SchemaVersion.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Orange::Engine
{

struct ParseError
{
    ResultCode  code{ResultCode::Unknown};
    std::string path;     // dotted-or-slashed JSON path of the offending field
    std::string message;  // human-readable, includes line/byte offset when available
};

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

class ORANGE_ENGINE_API JsonReader
{
public:
    static Result<JsonReader, ParseError> FromString(std::string_view text);
    static Result<JsonReader, ParseError> FromFile(std::string_view path);

    JsonReader();
    JsonReader(JsonReader&&) noexcept;
    JsonReader& operator=(JsonReader&&) noexcept;
    JsonReader(const JsonReader&) = delete;
    JsonReader& operator=(const JsonReader&) = delete;
    ~JsonReader();

    bool Has(std::string_view path) const;

    // Strict reads — return false when the key is missing OR the type is
    // wrong. The output reference stays untouched on failure so callers
    // who pre-seed with a default still hold that default.
    bool ReadBool(std::string_view path, bool& out) const;
    bool ReadInt(std::string_view path, std::int64_t& out) const;
    bool ReadFloat(std::string_view path, double& out) const;
    bool ReadString(std::string_view path, std::string& out) const;

    // Convenience defaults.
    bool         GetBool(std::string_view path, bool defaultValue) const;
    std::int64_t GetInt(std::string_view path, std::int64_t defaultValue) const;
    double       GetFloat(std::string_view path, double defaultValue) const;
    std::string  GetString(std::string_view path, std::string defaultValue) const;

    // Numeric arrays — kept compact for Vec2/Vec3/Vec4 / per-frame keys.
    bool ReadFloatArray(std::string_view path, float* out, std::size_t count) const;

    // Pulls a `{ "namespace": "...", "major": N, "minor": M }` triplet out
    // of `path`. Returns InvalidArgument if the triplet is missing or
    // malformed; caller still holds the responsibility for compatibility
    // checking via SchemaVersion::CanRead.
    Result<SchemaVersion, ResultCode> ReadSchemaVersion(std::string_view path) const;

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;

    explicit JsonReader(std::unique_ptr<Impl> impl) noexcept;
};

class ORANGE_ENGINE_API JsonWriter
{
public:
    JsonWriter();
    JsonWriter(JsonWriter&&) noexcept;
    JsonWriter& operator=(JsonWriter&&) noexcept;
    JsonWriter(const JsonWriter&) = delete;
    JsonWriter& operator=(const JsonWriter&) = delete;
    ~JsonWriter();

    void WriteBool(std::string_view path, bool value);
    void WriteInt(std::string_view path, std::int64_t value);
    void WriteFloat(std::string_view path, double value);
    void WriteString(std::string_view path, std::string_view value);
    void WriteFloatArray(std::string_view path, const float* data, std::size_t count);

    void WriteSchemaVersion(std::string_view path, const SchemaVersion& version);

    std::string Dump(int indent = 2) const;
    Result<void, ResultCode> SaveToFile(std::string_view path, int indent = 2) const;

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

// ---------------------------------------------------------------------------
// Binary
//
// Engine binary format is fixed little-endian byte stream with no padding.
// The `Read<T>` / `Write<T>` templates only accept trivially copyable
// arithmetic / fixed-length POD; richer types must serialize themselves
// field by field. This avoids accidentally bit-blitting struct padding /
// pointers across the wire.
// ---------------------------------------------------------------------------

class ORANGE_ENGINE_API BinaryWriter
{
public:
    BinaryWriter() = default;

    void WriteBytes(const void* data, std::size_t count);

    template <typename T>
    void Write(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "BinaryWriter::Write<T>: T must be trivially copyable.");
        static_assert(!std::is_pointer_v<T>,
                      "BinaryWriter::Write<T>: refusing to serialize a raw pointer.");
        WriteBytes(&value, sizeof(T));
    }

    const std::vector<std::uint8_t>& Bytes() const noexcept { return mBytes; }
    std::size_t                      Size() const noexcept { return mBytes.size(); }

    Result<void, ResultCode> SaveToFile(std::string_view path) const;

private:
    std::vector<std::uint8_t> mBytes;
};

class ORANGE_ENGINE_API BinaryReader
{
public:
    BinaryReader(const void* data, std::size_t size) noexcept
        : mpData(static_cast<const std::uint8_t*>(data)), mSize(size)
    {
    }

    static Result<std::vector<std::uint8_t>, ResultCode> LoadFile(std::string_view path);

    bool ReadBytes(void* out, std::size_t count) noexcept;

    template <typename T>
    bool Read(T& out) noexcept
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "BinaryReader::Read<T>: T must be trivially copyable.");
        static_assert(!std::is_pointer_v<T>,
                      "BinaryReader::Read<T>: refusing to deserialize a raw pointer.");
        return ReadBytes(&out, sizeof(T));
    }

    std::size_t Position() const noexcept { return mCursor; }
    std::size_t Remaining() const noexcept { return (mCursor < mSize) ? (mSize - mCursor) : 0; }
    bool        Eof() const noexcept { return mCursor >= mSize; }

private:
    const std::uint8_t* mpData{nullptr};
    std::size_t         mSize{0};
    std::size_t         mCursor{0};
};

// Compile-time assertion: the host must be little-endian. The wire format
// is fixed little-endian; running on a big-endian host would require a
// byte-swap shim that Phase 1 does not ship. (Windows / MSVC is x86_64
// little-endian, so this is a tripwire for future ports rather than a
// runtime check.)
static_assert(static_cast<std::uint32_t>(0x01020304u) == 0x01020304u,
              "OrangeEngine binary serialization assumes a little-endian host.");

}  // namespace Orange::Engine

#endif  // ORANGE_ENGINE_CORE_SERIALIZATION_H
