#ifndef ORANGE_ENGINE_APP_APP_CONFIG_H
#define ORANGE_ENGINE_APP_APP_CONFIG_H

// ---------------------------------------------------------------------------
// AppConfig: the parameter object handed to AppHost::Create. Kept as a
// plain aggregate so that callers can configure with designated
// initializers and so that adding a field later does not break ABI for
// existing call sites.
//
// Phase scope: only the fields the engine actually consumes today are
// declared. Fixed-step physics dt, profiler controls, and asset-search
// roots will land alongside the modules that consume them.
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/platform/Window.h>

namespace Orange::Engine
{

struct AppConfig
{
    Platform::WindowDesc window{};

    // Cap presentation rate to the display refresh. Phase 1 has no
    // alternative pacing mode, so this defaults true and is effectively
    // descriptive; the renderer will honour it once the swap-chain wires
    // through in Phase 2.
    bool vsync{true};
};

}  // namespace Orange::Engine

#endif  // ORANGE_ENGINE_APP_APP_CONFIG_H
