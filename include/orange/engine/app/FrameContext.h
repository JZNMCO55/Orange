#ifndef ORANGE_ENGINE_APP_FRAME_CONTEXT_H
#define ORANGE_ENGINE_APP_FRAME_CONTEXT_H

// ---------------------------------------------------------------------------
// FrameContext: read-only snapshot of per-frame state passed to every Layer
// during `OnUpdate`. The contract is intentionally narrow:
//
//   * the values inside are valid for the duration of a single frame and
//     never mutate mid-frame — Layers may cache references to fields safely
//     within their `OnUpdate`, but must not retain pointers across frames;
//   * `pWindow` is a non-owning observer. Layers may query window state
//     (size, close request) through it, but must not destroy it. AppHost
//     owns the Window's lifetime.
//
// Why not pass the Window directly?
//   Layers should mostly read time + framebuffer dimensions; surfacing
//   those as plain values keeps the common path free of indirection and
//   makes layer unit tests trivial (construct a FrameContext literal,
//   leave `pWindow` null). The pointer is there for the rare case a layer
//   genuinely needs to talk back to the platform (e.g. request close).
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Time.h>

#include <cstdint>

namespace Orange::Engine
{

namespace Platform
{
class Window;
}

struct FrameContext
{
    TimeStamp        time{};
    std::uint32_t    framebufferWidth{0};
    std::uint32_t    framebufferHeight{0};
    Platform::Window* pWindow{nullptr};
};

}  // namespace Orange::Engine

#endif  // ORANGE_ENGINE_APP_FRAME_CONTEXT_H
