#ifndef ORANGE_ENGINE_APP_LAYER_STACK_H
#define ORANGE_ENGINE_APP_LAYER_STACK_H

// ---------------------------------------------------------------------------
// LayerStack: ordered storage for the Layers that AppHost ticks each frame.
//
// The container is conceptually two zones laid out in a single contiguous
// vector:
//
//   [ regular layers ... ][ overlays ... ]
//                         ^
//                         mLayerInsertIndex — first overlay slot
//
// Why one vector, not two? Iteration is the hot path. AppHost walks the
// whole stack in order for OnUpdate (front-to-back, regular layers first
// so overlays render last) and in reverse for OnEvent (overlays first so
// they get the chance to consume an event before regular layers see it).
// A single vector keeps both walks branch-free.
//
// Ownership:
//   LayerStack owns its layers via unique_ptr. Push* takes ownership and
//   returns a non-owning pointer for the caller to keep around (so that
//   later Pop* calls can reference the layer by identity). Detach is
//   driven from the destructor in reverse insertion order, mirroring the
//   construction sequence.
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace Orange::Engine
{

class Layer;

class ORANGE_ENGINE_API LayerStack
{
public:
    using LayerPtr  = std::unique_ptr<Layer>;
    using Container = std::vector<LayerPtr>;

    LayerStack();
    ~LayerStack();

    LayerStack(const LayerStack&)            = delete;
    LayerStack& operator=(const LayerStack&) = delete;
    LayerStack(LayerStack&&)                 = delete;
    LayerStack& operator=(LayerStack&&)      = delete;

    Layer* PushLayer(LayerPtr layer);
    Layer* PushOverlay(LayerPtr overlay);

    LayerPtr PopLayer(Layer* layer);
    LayerPtr PopOverlay(Layer* overlay);

    std::size_t Size() const noexcept { return mLayers.size(); }
    bool        Empty() const noexcept { return mLayers.empty(); }

    Container::iterator       begin() noexcept { return mLayers.begin(); }
    Container::iterator       end() noexcept { return mLayers.end(); }
    Container::const_iterator begin() const noexcept { return mLayers.begin(); }
    Container::const_iterator end() const noexcept { return mLayers.end(); }

    Container::reverse_iterator       rbegin() noexcept { return mLayers.rbegin(); }
    Container::reverse_iterator       rend() noexcept { return mLayers.rend(); }
    Container::const_reverse_iterator rbegin() const noexcept { return mLayers.rbegin(); }
    Container::const_reverse_iterator rend() const noexcept { return mLayers.rend(); }

private:
    Container   mLayers;
    std::size_t mLayerInsertIndex{0};
};

}  // namespace Orange::Engine

#endif  // ORANGE_ENGINE_APP_LAYER_STACK_H
