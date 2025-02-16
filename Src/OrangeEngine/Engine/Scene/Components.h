#ifndef COMPONENTS_H
#define COMPONENTS_H

#include "pch.h"
#include "entt/entt.hpp"


namespace Orange
{
    struct ORANGE_API TransformComponent
    {
        glm::mat4 Transform{ 1.0f };

        TransformComponent() = default;
        TransformComponent(const TransformComponent&) = default;
        TransformComponent(const glm::mat4& transform)
            : Transform{ transform } {}

        operator glm::mat4() const { return Transform; }
    };

    struct ORANGE_API SpriteRendererComponent
    {
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };

        SpriteRendererComponent() = default;
        SpriteRendererComponent(const SpriteRendererComponent&) = default;
        SpriteRendererComponent(const glm::vec4& color) : Color{ color } {}
    };
}

#endif // COMPONENTS_H