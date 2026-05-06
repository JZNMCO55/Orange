#include "orange/engine/app/LayerStack.h"

#include "orange/engine/app/Layer.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace Orange::Engine
{

LayerStack::LayerStack() = default;

LayerStack::~LayerStack()
{
    // Tear layers down in reverse insertion order so that overlays detach
    // before the regular layers they were stacked on top of.
    while (!mLayers.empty())
    {
        LayerPtr last = std::move(mLayers.back());
        mLayers.pop_back();
        if (last)
        {
            last->OnDetach();
        }
    }
    mLayerInsertIndex = 0;
}

Layer* LayerStack::PushLayer(LayerPtr layer)
{
    if (!layer)
    {
        return nullptr;
    }
    Layer* raw = layer.get();
    mLayers.emplace(mLayers.begin() + static_cast<std::ptrdiff_t>(mLayerInsertIndex),
                    std::move(layer));
    ++mLayerInsertIndex;
    raw->OnAttach();
    return raw;
}

Layer* LayerStack::PushOverlay(LayerPtr overlay)
{
    if (!overlay)
    {
        return nullptr;
    }
    Layer* raw = overlay.get();
    mLayers.emplace_back(std::move(overlay));
    raw->OnAttach();
    return raw;
}

LayerStack::LayerPtr LayerStack::PopLayer(Layer* layer)
{
    if (layer == nullptr || mLayerInsertIndex == 0)
    {
        return nullptr;
    }
    auto first = mLayers.begin();
    auto last  = mLayers.begin() + static_cast<std::ptrdiff_t>(mLayerInsertIndex);
    auto it    = std::find_if(first, last,
                              [layer](const LayerPtr& slot) { return slot.get() == layer; });
    if (it == last)
    {
        return nullptr;
    }
    LayerPtr owned = std::move(*it);
    mLayers.erase(it);
    --mLayerInsertIndex;
    owned->OnDetach();
    return owned;
}

LayerStack::LayerPtr LayerStack::PopOverlay(Layer* overlay)
{
    if (overlay == nullptr)
    {
        return nullptr;
    }
    auto first = mLayers.begin() + static_cast<std::ptrdiff_t>(mLayerInsertIndex);
    auto last  = mLayers.end();
    auto it    = std::find_if(first, last,
                              [overlay](const LayerPtr& slot) { return slot.get() == overlay; });
    if (it == last)
    {
        return nullptr;
    }
    LayerPtr owned = std::move(*it);
    mLayers.erase(it);
    owned->OnDetach();
    return owned;
}

}  // namespace Orange::Engine
