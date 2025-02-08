#include "pch.h"
#include "LayerStack.h"

namespace Orange
{
    LayerStack::LayerStack()
    {
    }

    LayerStack::~LayerStack()
    {
        mLayers.clear();
    }

    void LayerStack::PushLayer(const Ref<Layer>& layer)
    {
        mLayers.emplace(mLayers.begin() + mLayerInsertIndex, layer);
    }

    void LayerStack::PushOverlay(const Ref<Layer>& overlay)
    {
        mLayers.emplace_back(overlay);
    }

    void LayerStack::PopLayer(const Ref<Layer>& layer)
    {
        auto it = std::find(mLayers.begin(), mLayers.end(), layer);
        if (it != mLayers.end())
        {
            mLayers.erase(it);
            mLayerInsertIndex--;
        }
    }

    void LayerStack::PopOverlay(const Ref<Layer>& overlay)
    {
        auto it = std::find(mLayers.begin(), mLayers.end(), overlay);
        if (it != mLayers.end())
        {
            mLayers.erase(it);
        }
    }

}