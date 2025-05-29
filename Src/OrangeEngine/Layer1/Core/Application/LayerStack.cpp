#include "LayerStack.h"
#include <algorithm>

namespace Orange
{
    namespace Core
    {
        LayerStack::~LayerStack()
        {
            for (auto& layer : m_Layers)
            {
                if (layer)
                {
                    layer->OnDetach();
                }
            }
        }

        void LayerStack::PushLayer(const std::shared_ptr<Layer>& layer)
        {
            m_Layers.emplace(m_Layers.begin() + m_LayerInsertIndex, layer);
            m_LayerInsertIndex++;
            layer->OnAttach();
        }

        void LayerStack::PushOverlay(const std::shared_ptr<Layer>& overlay)
        {
            m_Layers.emplace_back(overlay);
            overlay->OnAttach();
        }

        void LayerStack::PopLayer(const std::shared_ptr<Layer>& layer)
        {
            auto it = std::find(m_Layers.begin(), m_Layers.begin() + m_LayerInsertIndex, layer);
            if (it != m_Layers.begin() + m_LayerInsertIndex)
            {
                layer->OnDetach();
                m_Layers.erase(it);
                m_LayerInsertIndex--;
            }
        }

        void LayerStack::PopOverlay(const std::shared_ptr<Layer>& overlay)
        {
            auto it = std::find(m_Layers.begin() + m_LayerInsertIndex, m_Layers.end(), overlay);
            if (it != m_Layers.end())
            {
                overlay->OnDetach();
                m_Layers.erase(it);
            }
        }
    }
} 