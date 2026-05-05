// ---------------------------------------------------------------------------
// Phase 1 / Task 04 — Core public-header self-containment check.
//
// Every Core public header is included from this single TU, in isolation
// from any other engine code. If any header is missing an include, depends
// on declarations leaked by a sibling header, or accidentally references
// a private identifier, the build fails here rather than in some
// downstream consumer.
//
// This file holds no runtime logic; the sentinel below exists only so the
// translation unit produces a non-empty object and the linker keeps it.
// Add a line for every new header that lands under
// `include/orange/engine/core/`.
// ---------------------------------------------------------------------------

#include "orange/engine/core/Config.h"
#include "orange/engine/core/Hash.h"
#include "orange/engine/core/Handle.h"
#include "orange/engine/core/Log.h"
#include "orange/engine/core/Result.h"
#include "orange/engine/core/SchemaVersion.h"
#include "orange/engine/core/Serialization.h"
#include "orange/engine/core/Time.h"

namespace Orange::Engine::Core
{
namespace
{

[[maybe_unused]] inline constexpr int sCoreHeaderCheckSentinel = 0;

}  // namespace
}  // namespace Orange::Engine::Core
