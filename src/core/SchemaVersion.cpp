// ---------------------------------------------------------------------------
// Phase 1 / Task 05 — Core::SchemaVersion implementation
//
// Out-of-line because the constructor stores both the original namespace
// string AND its FNV-1a hash; keeping the hash computation in a TU avoids
// pulling Hash.h's constexpr machinery into every consumer of the class.
// ---------------------------------------------------------------------------

#include "orange/engine/core/SchemaVersion.h"

namespace Orange::Engine
{

SchemaVersion::SchemaVersion(std::string_view namespaceName,
                             std::uint16_t major,
                             std::uint16_t minor)
    : mNamespace(namespaceName)
    , mNamespaceHash(Fnv1a64(namespaceName))
    , mMajor(major)
    , mMinor(minor)
{
}

}  // namespace Orange::Engine
