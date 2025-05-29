#ifndef ORANGE_LAYER_STACK_H
#define ORANGE_LAYER_STACK_H

#include "Layer.h"
#include <vector>
#include <memory>

namespace Orange
{
    namespace Core
    {
        class LayerStack
        {
        public:
            LayerStack() = default;
            ~LayerStack();

            void PushLayer(const std::shared_ptr<Layer>& layer);
            void PushOverlay(const std::shared_ptr<Layer>& overlay);
            void PopLayer(const std::shared_ptr<Layer>& layer);
            void PopOverlay(const std::shared_ptr<Layer>& overlay);

            std::vector<std::shared_ptr<Layer>>::iterator begin() { return m_Layers.begin(); }
            std::vector<std::shared_ptr<Layer>>::iterator end() { return m_Layers.end(); }
            std::vector<std::shared_ptr<Layer>>::reverse_iterator rbegin() { return m_Layers.rbegin(); }
            std::vector<std::shared_ptr<Layer>>::reverse_iterator rend() { return m_Layers.rend(); }

            std::vector<std::shared_ptr<Layer>>::const_iterator begin() const { return m_Layers.begin(); }
            std::vector<std::shared_ptr<Layer>>::const_iterator end() const { return m_Layers.end(); }
            std::vector<std::shared_ptr<Layer>>::const_reverse_iterator rbegin() const { return m_Layers.rbegin(); }
            std::vector<std::shared_ptr<Layer>>::const_reverse_iterator rend() const { return m_Layers.rend(); }

        private:
            std::vector<std::shared_ptr<Layer>> m_Layers;
            unsigned int m_LayerInsertIndex = 0;
        };
    }
}

#endif // ORANGE_LAYER_STACK_H 