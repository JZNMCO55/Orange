#ifndef ORANGE_ENGINE_CORE_SCHEMA_VERSION_H
#define ORANGE_ENGINE_CORE_SCHEMA_VERSION_H

// ---------------------------------------------------------------------------
// Core::SchemaVersion —— 每个可序列化引擎类型的声明式 {namespace, major,
// minor} 三元组。读路径在触碰任何 payload 字段之前先验证兼容性，使过期或
// 跨命名空间的文件能 fail-fast，而不是产出"看似成功但语义错乱"的结果。
//
// 兼容语义（针对引擎自有 schema 刻意保持严格）：
//   * Namespace 必须 BIT-FOR-BIT 匹配（FNV-1a 64 hash 比较）。
//     "scene/Transform" 永远无法被当作 "scene/Hierarchy" 来读，哪怕字段
//     表面看起来一样。
//   * Major 是硬墙：bump major 意味着 "旧 reader 处理不了这个文件"；
//     reader 直接拒绝，调用方应路由到 migrator。
//   * Minor 向后兼容：reader minor=N 接受 file minor<=N。新 minor 只能
//     新增 optional 字段；旧 minor 缺失这些字段时由 reader 填默认值。
//
// 一个 schema version 一旦"出厂"（发到 game 仓或者玩家手里），就视为冻结
// ——major/minor 只增不改。详见 CLAUDE.md 中 "Serialization and reflection"
// 一节。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Hash.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace Orange::Engine
{

    class ORANGE_ENGINE_API SchemaVersion
    {
    public:
        SchemaVersion() = default;

        SchemaVersion(std::string_view namespaceName,
                      std::uint16_t    major,
                      std::uint16_t    minor);

        const std::string& Namespace() const noexcept { return mNamespace; }
        std::uint64_t      NamespaceHash() const noexcept { return mNamespaceHash; }
        std::uint16_t      Major() const noexcept { return mMajor; }
        std::uint16_t      Minor() const noexcept { return mMinor; }

        bool IsValid() const noexcept { return !mNamespace.empty(); }

        // namespace 的 hash 是否相等。刻意只比 hash——hot path 不需要字符串
        // 等值比较。
        bool NamespaceMatches(const SchemaVersion& other) const noexcept
        {
            return mNamespaceHash == other.mNamespaceHash;
        }

        // Reader 端兼容性检查：
        //   `*this`  ——引擎代码期望的 SchemaVersion（即"reader"）
        //   `actual` ——从文件中读出的 SchemaVersion
        // 当且仅当 reader 可以安全消费该文件时返回 true。
        bool CanRead(const SchemaVersion& actual) const noexcept
        {
            return NamespaceMatches(actual) && mMajor == actual.mMajor && mMinor >= actual.mMinor;
        }

        friend bool operator==(const SchemaVersion& a, const SchemaVersion& b) noexcept
        {
            return a.mNamespaceHash == b.mNamespaceHash && a.mMajor == b.mMajor && a.mMinor == b.mMinor;
        }

        friend bool operator!=(const SchemaVersion& a, const SchemaVersion& b) noexcept
        {
            return !(a == b);
        }

    private:
        std::string   mNamespace;
        std::uint64_t mNamespaceHash{0};
        std::uint16_t mMajor{0};
        std::uint16_t mMinor{0};
    };

} // namespace Orange::Engine

#endif // ORANGE_ENGINE_CORE_SCHEMA_VERSION_H
