#ifndef LAYE_STACK_H
#define LAYE_STACK_H

#include "Layer.h"
#include "OrangeExport.h"

namespace Orange
{
    class ORANGE_API LayerStack
    {
    public:
        LayerStack();
        ~LayerStack();

        void PushLayer(const Ref<Layer>& layer);
        void PushOverlay(const Ref<Layer>& overlay);
        void PopLayer(const Ref<Layer>& layer);
        void PopOverlay(const Ref<Layer>& overlay);

        std::vector<Ref<Layer>>::iterator begin() { return mLayers.begin(); }
        std::vector<Ref<Layer>>::iterator end() { return mLayers.end(); }

    private:
        std::vector<Ref<Layer>> mLayers;
        unsigned int mLayerInsertIndex = 0;
    };
}

#endif // LAYE_STACK_H