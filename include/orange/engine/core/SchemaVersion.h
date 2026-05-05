#ifndef ORANGE_ENGINE_CORE_SCHEMA_VERSION_H
#define ORANGE_ENGINE_CORE_SCHEMA_VERSION_H

// ---------------------------------------------------------------------------
// Phase 1 / Task 05 — Core::SchemaVersion
//
// Declarative {namespace, major, minor} triplet stamped onto every
// serializable engine type. The read path validates compatibility before
// touching any payload field, so a stale or foreign file fails fast with a
// clear error rather than producing silently-wrong data.
//
// Compatibility semantics (deliberately strict for engine-owned schemas):
//
//   * Namespace must match BIT-FOR-BIT (compared via FNV-1a 64 hash). A
//     "scene/Transform" file is never readable as a "scene/Hierarchy" file
//     even if their fields look alike.
//   * Major version is a hard wall: bumping major means "old reader cannot
//     handle this file"; the reader rejects and the caller is expected to
//     route through a migrator.
//   * Minor version is backward-compatible: a reader at minor=N accepts
//     files at minor<=N. Newer minor adds optional fields only; older
//     minor lacks them and the reader fills defaults.
//
// Once a schema version has shipped (to the game repo or a player), it is
// frozen — bumping major/minor is the only allowed evolution. See the
// "Serialization and reflection" guardrails in CLAUDE.md.
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
                  std::uint16_t major,
                  std::uint16_t minor);

    const std::string& Namespace() const noexcept { return mNamespace; }
    std::uint64_t      NamespaceHash() const noexcept { return mNamespaceHash; }
    std::uint16_t      Major() const noexcept { return mMajor; }
    std::uint16_t      Minor() const noexcept { return mMinor; }

    bool IsValid() const noexcept { return !mNamespace.empty(); }

    // True when the namespace hashes match. Hash compare is intentional —
    // the reader does not need string equality for the hot path.
    bool NamespaceMatches(const SchemaVersion& other) const noexcept
    {
        return mNamespaceHash == other.mNamespaceHash;
    }

    // Reader-side compatibility check.
    //   `*this`  == the SchemaVersion the engine code expects (the "reader")
    //   `actual` == the SchemaVersion read from the file
    // Returns true iff the reader can safely consume the file.
    bool CanRead(const SchemaVersion& actual) const noexcept
    {
        return NamespaceMatches(actual)
            && mMajor == actual.mMajor
            && mMinor >= actual.mMinor;
    }

    friend bool operator==(const SchemaVersion& a, const SchemaVersion& b) noexcept
    {
        return a.mNamespaceHash == b.mNamespaceHash
            && a.mMajor == b.mMajor
            && a.mMinor == b.mMinor;
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

}  // namespace Orange::Engine

#endif  // ORANGE_ENGINE_CORE_SCHEMA_VERSION_H
