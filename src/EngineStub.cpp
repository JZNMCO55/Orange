// ---------------------------------------------------------------------------
// Phase 1 / Task 02 — placeholder translation unit for the orange_engine
// static library.
//
// CMake's `add_library(<name> STATIC ...)` requires at least one source file
// to drive archiver invocation. This TU exists solely to satisfy that
// requirement while the engine is still being scaffolded; it will remain
// here as a low-cost anchor (real implementation files are added module by
// module starting at Phase 1 / Task 04).
//
// Do not put real engine code here. New subsystems live under their own
// `src/<module>/` directories.
// ---------------------------------------------------------------------------

namespace Orange::Engine
{
namespace
{

[[maybe_unused]] inline constexpr int sEngineStubSentinel = 0;

}  // namespace
}  // namespace Orange::Engine
