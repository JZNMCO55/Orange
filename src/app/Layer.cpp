#include "orange/engine/app/Layer.h"

namespace Orange::Engine
{

    Layer::Layer(std::string_view name) : mName(name)
    {
    }

    Layer::~Layer() = default;

} // namespace Orange::Engine
