#ifndef ORANGE_LAYER_H
#define ORANGE_LAYER_H

#include <string>

// 前向声明
namespace Orange
{
    namespace Core
    {
        class Event;
    }
}

namespace Orange
{
    namespace Core
    {
        class Layer
        {
        public:
            Layer(const std::string& name = "Layer");
            virtual ~Layer() = default;

            virtual void OnAttach() {}
            virtual void OnDetach() {}
            virtual void OnUpdate() {}
            virtual void OnEvent(Event& event) {}

            const std::string& GetName() const { return m_DebugName; }

        protected:
            std::string m_DebugName;
        };
    }
}

#endif // ORANGE_LAYER_H
