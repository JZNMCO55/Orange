// Self-containment check for App public headers. Each header is included
// here in isolation; if any of them leaks an unintended dependency,
// silently grows a transitive include, or fails to compile on its own,
// the build breaks here rather than in a downstream consumer. Add a line
// for every new header that lands under `include/orange/engine/app/`.

#include "orange/engine/app/AppConfig.h"
#include "orange/engine/app/FrameContext.h"
#include "orange/engine/app/Layer.h"
#include "orange/engine/app/LayerStack.h"

namespace Orange::Engine
{
namespace
{

[[maybe_unused]] inline constexpr int sAppHeaderCheckSentinel = 0;

}  // namespace
}  // namespace Orange::Engine
