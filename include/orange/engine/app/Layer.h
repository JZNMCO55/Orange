#ifndef ORANGE_ENGINE_APP_LAYER_H
#define ORANGE_ENGINE_APP_LAYER_H

// ---------------------------------------------------------------------------
// Layer: the single extension point through which game-side and tool-side
// code injects logic into the engine main loop. AppHost owns a LayerStack;
// each frame it walks the stack, dispatching events top-down and update
// bottom-up.
//
// Lifecycle:
//   OnAttach          — called once when the layer enters the stack
//   OnUpdate(frame)   — called every frame; layers should not block here
//   OnEvent(event)    — called for every platform event; return true to
//                       stop propagation to lower-priority layers
//   OnDetach          — called once when the layer leaves the stack
//
// Layers are non-copyable, non-movable. Stable identity matters because
// LayerStack::Pop takes the raw pointer that PushLayer returned, and
// because layers may register callbacks elsewhere keyed on `this`.
//
// Event propagation:
//   Returning `true` from OnEvent means "I handled this event, do not
//   forward it". Returning `false` (the default) lets the LayerStack
//   continue dispatching downward. This matches the spec's invariant
//   that overlays sit *above* layers and may consume events before
//   layers see them.
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/platform/WindowEvent.h>

#include <string>
#include <string_view>

namespace Orange::Engine
{

class ORANGE_ENGINE_API Layer
{
public:
    explicit Layer(std::string_view name = "Layer");
    virtual ~Layer();

    Layer(const Layer&)            = delete;
    Layer& operator=(const Layer&) = delete;
    Layer(Layer&&)                 = delete;
    Layer& operator=(Layer&&)      = delete;

    virtual void OnAttach() {}
    virtual void OnDetach() {}
    virtual void OnUpdate(const FrameContext& /*frame*/) {}

    virtual bool OnEvent(const Platform::WindowEvent& /*event*/) { return false; }

    const std::string& GetName() const noexcept { return mName; }

private:
    std::string mName;
};

}  // namespace Orange::Engine

#endif  // ORANGE_ENGINE_APP_LAYER_H
