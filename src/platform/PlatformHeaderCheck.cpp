// ---------------------------------------------------------------------------
// Phase 1 / Task 06 — Platform public-header self-containment check.
//
// Every Platform public header is included from this single TU, in
// isolation from any other engine code. Mirrors the Core equivalent in
// `src/core/CoreHeaderCheck.cpp`. If any Platform public header leaks a
// GLFW type, fails to be self-contained, or grows an accidental
// dependency on a sibling private header, the build fails here rather
// than in some downstream consumer.
// ---------------------------------------------------------------------------

#include "orange/engine/platform/Window.h"
#include "orange/engine/platform/WindowEvent.h"

namespace Orange::Engine::Platform
{
namespace
{

[[maybe_unused]] inline constexpr int sPlatformHeaderCheckSentinel = 0;

}  // namespace
}  // namespace Orange::Engine::Platform
