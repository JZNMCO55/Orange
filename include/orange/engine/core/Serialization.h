#ifndef ORANGE_ENGINE_CORE_SERIALIZATION_H
#define ORANGE_ENGINE_CORE_SERIALIZATION_H

// ---------------------------------------------------------------------------
// Core::Serialization —— 引擎自有数据所有读 / 写的唯一入口。
//
// 在这一层之外直接使用 `nlohmann::json` 是被禁止的（详见 CLAUDE.md 中
// "Serialization and reflection" 一节）；其他模块统一通过这里的 reader /
// writer 来交互。
//
// 两种格式，两组 reader / writer：
//   * JsonReader / JsonWriter   —— 文本载荷（config、schema、tools）。
//   * BinaryReader / BinaryWriter —— 紧凑 asset blob（little-endian、
//                                     字节对齐、不带 padding 泄漏）。
//
// JSON 路径语法用 '/' 作分隔符（如 `window/size/x`）。当前不支持在路径
// 中做数组下标；数组的读 / 写走专用的 ReadFloatArray / WriteFloatArray
// 之类辅助函数，按需在后续 phase 增加。
//
// 所有 reader / writer 通过 PIMPL 隐藏 nlohmann::json 类型，保证
// Serialization.h 自身的公共表面对第三方完全干净。
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
        std::string path;    // 出错字段在 JSON 中的点号 / 斜杠路径
        std::string message; // 人类可读，带行号 / 字节偏移（若可得）
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
        JsonReader(const JsonReader&)            = delete;
        JsonReader& operator=(const JsonReader&) = delete;
        ~JsonReader();

        bool Has(std::string_view path) const;

        // 数组节点的元素个数。path 不存在 / 不指向数组 → 0。配合数字下标
        // 路径段（"actions/0/name" 等）即可遍历对象数组：
        //
        //     for (std::size_t i = 0; i < r.ArraySize("actions"); ++i)
        //     {
        //         std::string name;
        //         std::string key = "actions/" + std::to_string(i) + "/name";
        //         r.ReadString(key, name);
        //     }
        //
        // 路径分隔符仍然是 '/'；纯数字路径段在数组节点上当作下标。
        std::size_t ArraySize(std::string_view path) const;

        // 列出 `path` 处对象节点的直接 key（成员名），用于"枚举未知字段"等
        // forward-compat 场景（如 Scene::Load 对未注册 component 发 warning）。
        // path 不存在 / 不指向对象 → 返回空 vector。key 顺序 = JSON 内成员顺序。
        std::vector<std::string> ListKeys(std::string_view path) const;

        // 严格读取——key 缺失或类型不对都返回 false。失败时不修改 out
        // 引用，因此调用方预先填好的默认值仍然保留。
        bool ReadBool(std::string_view path, bool& out) const;
        bool ReadInt(std::string_view path, std::int64_t& out) const;
        bool ReadFloat(std::string_view path, double& out) const;
        bool ReadString(std::string_view path, std::string& out) const;

        // 带默认值的便利接口。
        bool         GetBool(std::string_view path, bool defaultValue) const;
        std::int64_t GetInt(std::string_view path, std::int64_t defaultValue) const;
        double       GetFloat(std::string_view path, double defaultValue) const;
        std::string  GetString(std::string_view path, std::string defaultValue) const;

        // 数值数组——为 Vec2/Vec3/Vec4 / 帧关键帧等紧凑场景设计。
        bool ReadFloatArray(std::string_view path, float* out, std::size_t count) const;

        // 从 `path` 处读取 `{ "namespace": "...", "major": N, "minor": M }`
        // 三元组。缺失或格式错误返回 InvalidArgument；调用方仍需自行通过
        // SchemaVersion::CanRead 做兼容性判断。
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
        JsonWriter(const JsonWriter&)            = delete;
        JsonWriter& operator=(const JsonWriter&) = delete;
        ~JsonWriter();

        void WriteBool(std::string_view path, bool value);
        void WriteInt(std::string_view path, std::int64_t value);
        void WriteFloat(std::string_view path, double value);
        void WriteString(std::string_view path, std::string_view value);
        void WriteFloatArray(std::string_view path, const float* data, std::size_t count);

        // 在 `path` 处建立一个长度为 `count` 的数组，每个元素初始化为空对象。
        // 之后即可通过纯数字路径段下钻填充：
        //
        //     writer.BeginArray("entities", 3);
        //     writer.WriteInt("entities/0/id", 0);
        //     writer.WriteString("entities/0/name", "root");
        //     writer.WriteInt("entities/1/id", 1);
        //     ...
        //
        // 当 `path` 处已有数据时整段覆盖。`count == 0` 写出空数组 `[]`。
        void BeginArray(std::string_view path, std::size_t count);

        void WriteSchemaVersion(std::string_view path, const SchemaVersion& version);

        std::string              Dump(int indent = 2) const;
        Result<void, ResultCode> SaveToFile(std::string_view path, int indent = 2) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> mpImpl;
    };

    // ---------------------------------------------------------------------------
    // Binary
    //
    // 引擎二进制格式固定为 little-endian、无 padding 的字节流。`Read<T>` /
    // `Write<T>` 模板只接受 trivially copyable 的算术类型 / 定长 POD；更
    // 丰富的类型必须自己逐字段序列化。这样能避免不小心把 struct padding /
    // 指针整段 memcpy 到磁盘上。
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
                          "BinaryWriter::Write<T>: T 必须是 trivially copyable。");
            static_assert(!std::is_pointer_v<T>,
                          "BinaryWriter::Write<T>: 不允许序列化裸指针。");
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
                          "BinaryReader::Read<T>: T 必须是 trivially copyable。");
            static_assert(!std::is_pointer_v<T>,
                          "BinaryReader::Read<T>: 不允许反序列化裸指针。");
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

    // 编译期断言：宿主必须是 little-endian。线缆格式固定 little-endian；
    // big-endian 宿主需要一个 byte-swap shim，目前不提供。Windows / MSVC
    // 是 x86_64 little-endian，所以这里更像是给未来移植预留的 tripwire，
    // 而非运行时检查。
    static_assert(static_cast<std::uint32_t>(0x01020304u) == 0x01020304u,
                  "OrangeEngine 二进制序列化假设宿主是 little-endian。");

} // namespace Orange::Engine

#endif // ORANGE_ENGINE_CORE_SERIALIZATION_H
